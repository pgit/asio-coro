#include <boost/asio.hpp>
#include <iostream>

using namespace boost::asio;
using ip::tcp;

awaitable<size_t> echo_once(tcp::socket& socket)
{
   std::array<char, 1024> buffer;
   std::size_t n = co_await socket.async_read_some(boost::asio::buffer(buffer), deferred);
   co_await async_write(socket, boost::asio::buffer(buffer, n), deferred);
   co_return n;
}

awaitable<void> echo(tcp::socket socket)
{
   std::cout << "new connection from " << socket.remote_endpoint() << '\n';
   size_t total = 0;
   try
   {
      for (;;)
      {
#if 1
         // The asynchronous operations to echo a single chunk of data have been
         // refactored into a separate function. When this function is called, the
         // operations are still performed in the context of the current
         // coroutine, and the behaviour is functionally equivalent.
         total += co_await echo_once(socket);
#else
         std::array<char, 1024> buffer;
         std::size_t n = co_await socket.async_read_some(boost::asio::buffer(buffer), deferred);
         co_await async_write(socket, boost::asio::buffer(buffer, n), deferred);
         total += n;
#endif
      }
   }
   catch (std::exception& ex)
   {
      std::cout << ex.what() << '\n';
      std::cout << "echoed " << total << " bytes\n";
   }
}

awaitable<void> server(tcp::endpoint ep)
{
   auto executor = co_await this_coro::executor;
   tcp::acceptor acceptor(executor, ep);
   std::cout << "listening on " << acceptor.local_endpoint() << '\n';
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
