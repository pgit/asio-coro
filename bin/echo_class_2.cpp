#include "asio-coro.hpp"
#include "program_options.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <map>

using enum cancellation_type;
using namespace experimental;
using namespace experimental::awaitable_operators;

/**
 * This is a more traditional approach using a shared_ptr<> to solve the lifetime issues.
 *
 * However, it affects the interface: You have to explicitly call \c start(). Even more importantly,
 * to stop the server, you have to call \c stop() explicitly. Just dropping the shared pointer is
 * not enough. This could be solved with a wrapper class (which might exist already for PIMPL).
 *
 */
class EchoServer : public std::enable_shared_from_this<EchoServer>
{
public:
   EchoServer(any_io_executor executor, tcp::endpoint endpoint) // NOLINT
      : acceptor(executor, endpoint)
   {
   }

   void start()
   {
      auto self = shared_from_this();
      co_spawn(acceptor.get_executor(), run(),
               [self = shared_from_this()](const std::exception_ptr& ep)
      {
         std::println("acceptor finished: {}", what(ep));

         std::println("stopping sessions...");
         for (auto& session : self->sessions)
            session.second->stop();
         std::println("stopping sessions... done");
      });
   }

   void stop()
   {
      std::println("stop, closing acceptor...");
      acceptor.close();
      std::println("stop, closing acceptor... done");
   }

   ~EchoServer() { std::println("dtor"); }

private:
   tcp::acceptor acceptor;

   struct Session
   {
      size_t id;
      tcp::socket socket;

      void stop() { socket.close(); }

      awaitable<void> echo()
      {
         std::array<char, 64 * 1024> data;
         for (;;)
         {
            size_t n = co_await socket.async_read_some(buffer(data));
            co_await async_write(socket, buffer(data, n));
         }
      }
   };
   std::map<size_t, std::shared_ptr<Session>> sessions;

   awaitable<void> run()
   {
      auto ex = co_await this_coro::executor;
      auto cs = co_await this_coro::cancellation_state;

      //
      // Main accept loop.
      //
      // For each accepted connection, move the socket into a new coroutine. For cancellation,
      // resort to the .close() functions of both acceptor and the session sockets. Use a
      // shared_ptr<> to manage the session lifetime and capture a weak_ptr<> in the completion
      // handler.
      //
      for (size_t id = 0;;)
      {
         auto socket = co_await acceptor.async_accept();

         //
         //
         //
         auto session = std::make_shared<Session>(id, std::move(socket));
         auto [it, _] = sessions.emplace(id, session);
         std::println("session {} created, number of active sessions: {}", id, sessions.size());

         co_spawn(ex, session->echo(),
                  [weak_server = weak_from_this(), session](const std::exception_ptr& ep) mutable
         {
            std::println("session {} finished with {}", session->id, what(ep));
            if (auto server = weak_server.lock())
            {
               server->sessions.erase(session->id);
               std::println("number of active sessions left: {}", server->sessions.size());
            }
         });

         ++id;
      }
   }
};

awaitable<void> wait_for_signal(std::shared_ptr<EchoServer>& server)
{
   signal_set signals(co_await this_coro::executor, SIGINT);
   int signum = co_await signals.async_wait();
   std::println(" {}, destroying server...", strsignal(signum));
   server->stop();
   server.reset();
   std::println(" {}, destroying server... done", strsignal(signum));
   co_await signals.async_wait(); // wait once more
}

int main(int argc, char** argv)
{
   io_context context;
   auto server =
      std::make_shared<EchoServer>(context.get_executor(), tcp::endpoint{tcp::v6(), 55555});
   co_spawn(context, wait_for_signal(server), detached);
   server->start();
   return run(context, argc, argv);
}
