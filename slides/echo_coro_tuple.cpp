#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;
using namespace std::chrono_literals;

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
   io_context ctx;
   co_spawn(ctx, server({ctx, {tcp::v6(), 55555}}), detached);
   ctx.run();
}
