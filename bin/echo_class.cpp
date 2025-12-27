#include "asio-coro.hpp"
#include "program_options.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <map>

using enum cancellation_type;
using namespace experimental;
using namespace experimental::awaitable_operators;

class EchoServer
{
public:
   EchoServer(any_io_executor executor, tcp::endpoint endpoint) // NOLINT
      : promise_(co_spawn(executor, run({executor, endpoint}), use_promise))
   {
   }

   ~EchoServer()
   {
      std::println("Cancelling promise...");
      promise_.cancel();
      std::println("Cancelling promise... done");
   }

private:
   promise<void(std::exception_ptr)> promise_;

   //
   // Create a map of active sessions. We only store the promise here.
   //
   struct Session
   {
      promise<void(std::exception_ptr)> promise;
      ~Session() { std::println("Session destroyed"); }
   };
   std::map<size_t, Session> sessions;

   static awaitable<void> echo(tcp::socket socket)
   {
      std::array<char, 64 * 1024> data;
      for (;;)
      {
         size_t n = co_await socket.async_read_some(buffer(data));
         co_await async_write(socket, buffer(data, n));
      }
   }

   awaitable<void> run(tcp::acceptor acceptor)
   {
      auto ex = co_await this_coro::executor;
      auto cs = co_await this_coro::cancellation_state;

      //
      // Main accept loop.
      //
      // For each accepted connection, move the socket into a new coroutine. For cancellation,
      // spawn the coroutine with a the 'use_promise' completion token and store the returned
      // promise.
      //
      for (size_t id = 0;;)
      {
         auto socket = co_await acceptor.async_accept();

         // Check for cancellation race condition.
         if (cs.cancelled() != cancellation_type::none)
         {
            std::println("accept: cancelled ({})", cs.cancelled());
            break;
         }

         //
         // If accepting succeeded, always store the new connection so that it can be properly
         // cancelled if needed. This may also happen despite cancellation.
         //
         auto task = [this, id, socket = std::move(socket)] mutable -> awaitable<void>
         {
            auto ex = co_await this_coro::executor;
            auto cs = co_await this_coro::cancellation_state;
            // co_await echo(std::move(socket));
            auto [ec] = co_await co_spawn(ex, echo(std::move(socket)), as_tuple);
            // std::println("session {}: {} ({})", id, what(ec), cs.cancelled());
            if (cs.cancelled() != none)
            {
               std::println("session {} cancelled", id);
               co_return; // 'this' may be gone now
            }

            sessions.erase(id);
            std::println("session {} finished, number of active sessions: {}", id, sessions.size());
         };
         auto promise = co_spawn(ex, std::move(task), use_promise);

         auto [it, _] = sessions.emplace(id, std::move(promise));
         std::println("session {} created, number of active sessions: {}", id, sessions.size());
         ++id;
      }
   }
};

awaitable<void> wait_for_signal(std::optional<EchoServer>& server)
{
   signal_set signals(co_await this_coro::executor, SIGINT);
   int signum = co_await signals.async_wait();
   std::println(" {}, destroying server...", strsignal(signum));
   server.reset();
   std::println(" {}, destroying server... done", strsignal(signum));
   co_await signals.async_wait();
}

int main(int argc, char** argv)
{
   io_context context;
   std::optional<EchoServer> server;
   auto ex = make_strand(context.get_executor());
   server.emplace(ex, tcp::endpoint{tcp::v6(), 55555});
   co_spawn(ex, wait_for_signal(server), detached);
   return run(context, argc, argv);
}
