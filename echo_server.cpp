#include "common.hpp"

awaitable<void> echo(tcp::socket socket)
{
   std::println("new connection from {}", socket.remote_endpoint());
   size_t total = 0;
   try
   {
      std::array<char, 64*1024> buffer;
      for (;;)
      {
         std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), deferred);
         total += n;
         co_await async_write(socket, asio::buffer(buffer, n), deferred);
      }
   }
   catch (const boost::system::system_error& ec)
   {
      std::println("error: {}", ec.code().message());
   }
   
   std::println("echoed {} bytes total", total);
}

awaitable<void> listener(tcp::endpoint ep)
{
   auto executor = co_await this_coro::executor;
   tcp::acceptor acceptor(executor, ep);
   std::println("listening on {}", acceptor.local_endpoint());
   for (;;)
   {
      tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo(std::move(socket)), detached);
   }
}

int main()
{
   io_context io_context;
   co_spawn(io_context, listener({tcp::v6(), 55555}), detached);
   io_context.run();
}
