#include "asio-coro.hpp"

#include <map>

awaitable<void> session(tcp::socket& socket)
{
   std::array<char, 64 * 1024> data;
   for (;;)
   {
      size_t n = co_await socket.async_read_some(buffer(data));
      co_await async_write(socket, buffer(data, n));
   }
}

awaitable<void> server(tcp::acceptor acceptor)
{
   std::map<size_t, tcp::socket> sockets;

   auto executor = co_await this_coro::executor;
   signal_set signals(executor, SIGINT, SIGTERM);
   signals.async_wait([&](error_code ec, auto signum)
   {
      if (ec == boost::system::errc::operation_canceled)
         return;

      std::println(" INTERRUPTED (signal {})", signum);
      acceptor.cancel();

      //
      // Stop existing sessions. This is tricky, as we may run into the classic ASIO cancellation
      // race condition: If we cancel() pending async operations on the socket at a time where
      // one of those operations is already scheduled for completion, cancellation does not reach
      // that operation any more. The operation just completes normally, without error, and the
      // session() loop continues without being aware that it has been cancelled.
      //
      // To solve this, we usually would need some kind of flag that needs to be checked
      // in addition to looking at the operation's result error_code.
      //
      // Here, we just shutdown() the socket. Any subsequent operation on the socket will fail.
      // So here, putting the socket in 'closed' state plays the role of that flag mentioned above.
      //
      for (auto& socket : sockets)
         socket.second.shutdown(socket_base::shutdown_both);
   });

   //
   // Main accept loop. For each new connection, record the socket in a map for cancellation.
   // The session() coroutine is passed a reference only.
   //
   for (size_t id = 0;; id++)
   {
      auto [ec, socket] = co_await acceptor.async_accept(as_tuple);
      if (ec)
      {
         std::println("accept: {}", ec.message());
         break;
      }

      auto [it, inserted] = sockets.emplace(id, std::move(socket));
      std::println("session {} created, number of active sessions: {}", id, sockets.size());
      co_spawn(executor, session(it->second), [&sockets, id](const std::exception_ptr& ep)
      {
         sockets.erase(id);
         std::println("session {} finished with {}, {} sessions left", //
                      id, what(ep), sockets.size());
      });
   }

   std::println("-----------------------------------------------------------------------------");

   //
   // Simple busy waiting until all coroutines have finished.
   //
   // If cancellation takes longer (for example, when doing a graceful TLS disconnect), this
   // must be replaced by a better mechanism.
   //
   // TODO: Consider use_promise.
   //
   while (!sockets.empty())
      co_await post(deferred);

   std::println("==============================================================================");
}

int main()
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   context.run();
}
