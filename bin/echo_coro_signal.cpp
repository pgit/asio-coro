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
      for (auto& socket : sockets)
         socket.second.cancel();
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
      std::println("number of active sessions: {}", sockets.size());
      co_spawn(executor, session(it->second), [&sockets, id](const std::exception_ptr& ep)
      {
         sockets.erase(id);
         std::println("session {} finished: {}, {} sessions left", id, what(ep), sockets.size());
      });
   }

   std::println("-----------------------------------------------------------------------------");

   while (!sockets.empty())
      co_await post(executor, asio::deferred);

   std::println("==============================================================================");
}

int main()
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   context.run();
}
