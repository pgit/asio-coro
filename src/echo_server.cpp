#include <boost/asio/error.hpp>
#include "asio-coro.hpp"

awaitable<void> echo(tcp::socket socket)
{
   std::println("new connection from {}", socket.remote_endpoint());
   size_t total = 0;
   try
   {
      std::array<char, 1024> data;
      for (;;)
      {
         std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
         total += n;
         co_await async_write(socket, asio::buffer(data, n), use_awaitable);
      }
   }
   catch (const boost::system::system_error& ec)
   {
      if (ec.code() != asio::error::eof)
         throw;
   }

   std::println("echoed {} bytes total", total);
}

awaitable<void> server(tcp::endpoint endpoint)
{
   auto executor = co_await this_coro::executor;
   tcp::acceptor acceptor(executor, endpoint);

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
   co_spawn(io_context, server({tcp::v6(), 55555}), detached);
   io_context.run();
}
