#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;

awaitable<void> session(tcp::socket socket)
{
   std::array<char, 64 * 1024> data;
   for (;;)
   {
      auto [ec, n] = co_await socket.async_read_some(buffer(data), as_tuple);
      if (ec)
         break;

      std::tie(ec, n) = co_await async_write(socket, buffer(data, n), as_tuple);
      if (ec)
         break;
   }
}

awaitable<void> server(tcp::acceptor a)
{
   for (;;)
      co_spawn(a.get_executor(), session(co_await a.async_accept()), detached);
}

int main()
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   context.run();
}
