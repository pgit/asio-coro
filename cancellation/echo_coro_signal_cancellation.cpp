#include "asio-coro.hpp"
#include "program_options.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <map>

using enum cancellation_type;
using namespace experimental;
using namespace experimental::awaitable_operators;

awaitable<void> session(tcp::socket socket)
{
   std::array<char, 64 * 1024> data;
   for (;;)
   {
      size_t n = co_await socket.async_read_some(buffer(data));
      co_await async_write(socket, buffer(data, n));
   }
}

template <typename T>
awaitable<T> log_cancellation(awaitable<T>&& task)
{
   auto ex = co_await this_coro::executor;
   auto cs = co_await this_coro::cancellation_state;

   auto scope_exit = make_scope_exit([] { std::println("session coroutine frame destroyed"); });

   cancellation_signal signal;
   cs.slot().assign([&](cancellation_type ct)
   {
      std::println("session cancelled ({})", ct);
      signal.emit(ct);
   });

   co_return co_await co_spawn(ex, std::forward<awaitable<T>>(task),
                               bind_cancellation_slot(signal.slot()));
}

awaitable<void> server(tcp::acceptor acceptor)
{
   auto ex = co_await this_coro::executor;
   auto cs = co_await this_coro::cancellation_state;

   //
   // Create a map of active sessions. We only store the promise here.
   //
   struct Session
   {
      promise<void(std::exception_ptr)> promise;
   };
   std::map<size_t, Session> sessions;

   //
   // Main accept loop.
   //
   // For each accepted connection, move the socket into a new coroutine. For cancellation, create
   // a cancellation signal and bind it's slot to the completion handler of the coroutine.
   //
   for (size_t id = 0;;)
   {
      auto [ec, socket] = co_await acceptor.async_accept(as_tuple);

      //
      // If accepting succeeded, always store the new connection so that it can be properly
      // cancelled if needed. This may also happen despite cancellation.
      //
      if (!ec)
      {
         auto task = [&sessions, id, socket = std::move(socket)] mutable -> awaitable<void>
         {
            auto ex = co_await this_coro::executor;
            auto cs = co_await this_coro::cancellation_state;
            co_await co_spawn(ex, log_cancellation(session(std::move(socket))), as_tuple);
            if (cs.cancelled() != none)
            {
               std::println("session {} cancelled", id);
               co_return; // 'sessions' is out of scope now
            }

            sessions.erase(id);
            std::println("session {} finished, number of active sessions: {}", id, sessions.size());
         };

         sessions.emplace(id, co_spawn(ex, std::move(task), use_promise));
         std::println("session {} created, number of active sessions: {}", id, sessions.size());
         ++id;
      }

      //
      // Again, it is not enough to just check for 'ec', as we might run into the infamous ASIO
      // cancellation race condition: After signalling cancellation, async_accept() may still
      // complete successfully if it was already scheduled for completion. Thus, we still have to
      // check if we have been cancelled. ASIO's coroutine support helps with that and checks
      // automatically on the next suspension point, unless we disabled throw_if_cancelled().
      //
      if (ec || cs.cancelled() != cancellation_type::none)
      {
         std::println("accept: {} (cancellation {})", ec.message(), cs.cancelled());
         break;
      }
   }

   std::println("-----------------------------------------------------------------------------");

   //
   // Even if we didn't clear the sessions here explicitly, the destructor would.
   //
   sessions.clear();

   std::println("==============================================================================");
}

awaitable<void> wait_for_signal()
{
   signal_set signals(co_await this_coro::executor, SIGINT);
   int signum = co_await signals.async_wait();
   std::println(" {}", strsignal(signum));
}

awaitable<void> with_signal_handling(awaitable<void> task)
{
   co_await (std::move(task) || wait_for_signal());
   co_return;
}

int main(int argc, char** argv)
{
   io_context context;
   co_spawn(context, with_signal_handling(server({context, {tcp::v6(), 55555}})), detached);
   return run(context, argc, argv);
}
