#include "asio-coro.hpp"
#include "program_options.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>

#include <map>
#include <random>

using enum cancellation_type;
using namespace experimental::awaitable_operators;

awaitable<void> echo(tcp::socket& socket)
{
   // by default, an awaitable<> coroutine reacts to 'terminal' cancellation only
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());

   std::array<char, 64 * 1024> data;
   for (;;)
   {
      size_t n = co_await socket.async_read_some(buffer(data));
      co_await async_write(socket, buffer(data, n));
   }
}

awaitable<void> shutdown(tcp::socket& socket)
{
   // std::println("shutdown...");
   co_await async_write(socket, buffer("goodbye\n"sv));
   static thread_local std::mt19937 rng{std::random_device{}()};
   static thread_local std::uniform_int_distribution<int> dist(100, 1500);
   co_await sleep(std::chrono::milliseconds(dist(rng)));
   socket.shutdown(boost::asio::socket_base::shutdown_both);
   // std::println("shutdown... done");
   co_return;
}

awaitable<void> session(tcp::socket socket)
{
   auto cs = co_await this_coro::cancellation_state;
   auto ex = co_await this_coro::executor;

   //
   // For maximum flexibility, allow all cancellation types (total, partial terminal) and
   // don't throw when co_wait'ing something after cancellation. With this setup, we run
   // the actual echo task.
   //
   co_await this_coro::throw_if_cancelled(false);
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());

   // Finally, run echo loop. Catch errors and re-throw anything that is not about cancellation.
   auto [ep] = co_await co_spawn(ex, echo(socket), as_tuple);
   if (ep && code(ep) != error::eof && cs.cancelled() == none)
      throw system_error{code(ep)};

   // React to terminal cancellation immediately.
   if ((cs.cancelled() & cancellation_type::terminal) != cancellation_type::none)
      throw system_error{error::operation_aborted};

   // During shutdown, react to 'terminal' cancellation only.
   co_await this_coro::reset_cancellation_state(enable_terminal_cancellation());
   co_await shutdown(socket);
}

awaitable<void> server(tcp::acceptor acceptor)
{
   auto cs = co_await this_coro::cancellation_state;
   auto ex = co_await this_coro::executor;

   co_await this_coro::throw_if_cancelled(false);

   //
   // The fact that cancellation_signal is not copyable is not surprising, but it is also not
   // movable. This makes it a little difficult to handle, and we have to use a unique ptr.
   //
   std::map<size_t, std::unique_ptr<cancellation_signal>> sessions;
   experimental::channel<void(error_code)> channel(ex);

   //
   // Main accept loop.
   //
   // For each accepted connection, move the socket into a new coroutine. For cancellation, create
   // a cancellation signal and bind it's slot to the completion handler of the coroutine.
   //
   for (size_t id = 0;;)
   {
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      auto [ec, socket] = co_await acceptor.async_accept(as_tuple);

      //
      // If accepting succeeded, always store the new connection so that it can be properly
      // cancelled if needed. This may also happen despite cancellation.
      //
      if (!ec)
      {
         auto signal = std::make_unique<cancellation_signal>();

         co_spawn(ex, session(std::move(socket)),
                  bind_cancellation_slot(signal->slot(), [&, id](const std::exception_ptr& ep)
         {
            sessions.erase(id);
            std::println("session {} finished with {}, {} sessions left", //
                         id, what(ep), sessions.size());
            std::ignore = channel.try_send(error_code{});
         }));

         sessions.emplace(id, std::move(signal));
         std::println("session {} created, number of active sessions: {}", id, sessions.size());
         ++id;
      }

      //
      // On 'total' cancellation, cancel all currently active sessions, but continue accepting.
      // Note that async_accept() may even still have completed successfully before. In this
      // situation, the newly accepted connection is cancelled immediately.
      //
      if (cs.cancelled() == cancellation_type::total)
      {
         std::println("forwarding '{}' to {} sessions", cs.cancelled(), sessions.size());
         for (auto& session : sessions)
            session.second->emit(cs.cancelled());
         continue;
      }

      //
      // Again, it is not enough to just check for 'ec', as we might run into the infamous ASIO
      // cancellation race condition: After signalling cancellation, async_accept() may still
      // complete successfully if already scheduled for completion. Thus, we still have to check
      // if we have been cancelled. ASIO's coroutine support helps with that and checks
      // automatically on the next suspension point, unless we disabled throw_if_cancelled().
      //
      else if (ec || cs.cancelled() != cancellation_type::none)
      {
         std::println("accept: {} (cancellation {})", ec.message(), cs.cancelled());
         break;
      }
   }

   std::println("-----------------------------------------------------------------------------");

   //
   // Forward cancellation to spawned coroutines.
   //
   std::println("forwarding '{}' to {} sessions", cs.cancelled(), sessions.size());
   for (auto& session : sessions)
      session.second->emit(cs.cancelled());

   std::println("-----------------------------------------------------------------------------");

   //
   // Wait until all coroutines have finished.
   //
   // The channel is notified every time after a session is removed.
   //
   std::println("server: waiting for sessions to complete...");
   while (!sessions.empty())
   {
      co_await this_coro::reset_cancellation_state(enable_terminal_cancellation());
      std::ignore = co_await channel.async_receive(as_tuple);
      if (cs.cancelled() != cancellation_type::none)
      {
         std::println("forwarding '{}' to {} sessions", cs.cancelled(), sessions.size());
         for (auto& session : sessions)
            session.second->emit(cs.cancelled());
      }
   }
   std::println("server: waiting for sessions to complete... done");

   std::println("==============================================================================");
}

awaitable<void> signal_handling(cancellation_signal& signal)
{
   signal_set signals(co_await this_coro::executor, SIGINT, SIGTERM, SIGTSTP);
   for (;;)
   {
      auto signum = co_await signals.async_wait();
      std::println(" {}", strsignal(signum));

      switch (signum)
      {
      case SIGTSTP:
         signal.emit(cancellation_type::total);
         break;
      case SIGINT:
         signal.emit(cancellation_type::partial);
         break;
      case SIGTERM:
         signal.emit(cancellation_type::terminal);
         break;
      }
   }
}

awaitable<void> with_signal_handling(awaitable<void> task)
{
   cancellation_signal signal;
   co_await (signal_handling(signal) ||
             co_spawn(co_await this_coro::executor, std::move(task),
                      bind_cancellation_slot(signal.slot(), use_awaitable)));
}

int main(int argc, char** argv)
{
   io_context context;
   co_spawn(context, with_signal_handling(server({context, {tcp::v6(), 55555}})), detached);
   return run(context, argc, argv);
}
