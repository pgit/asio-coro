#include "asio-coro.hpp"

awaitable<size_t> echo_once(tcp::socket& socket)
{
   std::array<char, 1460> data;
   std::size_t n = co_await socket.async_read_some(boost::asio::buffer(data), deferred);
   co_await async_write(socket, boost::asio::buffer(data, n), deferred);
   co_return n;
}

awaitable<void> session(tcp::socket socket)
{
   std::println("new connection from {}", socket.remote_endpoint());
   size_t total = 0;
   try
   {
      for (;;)
      {
         // The asynchronous operations to echo a single chunk of data have been
         // refactored into a separate function. When this function is called, the
         // operations are still performed in the context of the current
         // coroutine, and the behaviour is functionally equivalent.
         total += co_await echo_once(socket);
      }
   }
   catch (error_code& ec)
   {
      std::println("{}", ec.message());
   }
   
   std::println("echoed {} bytes total", total);
}

awaitable<void> server(tcp::endpoint ep)
{
   auto executor = co_await this_coro::executor;
   tcp::acceptor acceptor(executor, ep);
   std::println("listening on {}", acceptor.local_endpoint());
   for (;;)
   {
      tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, session(std::move(socket)), detached);
   }
}

int main()
{
   io_context io_context;
   co_spawn(io_context, server({tcp::v6(), 55555}), detached);
   io_context.run();
}
