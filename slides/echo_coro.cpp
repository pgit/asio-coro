#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;

awaitable<void> session(tcp::socket socket)
{
   std::array<char, 64 * 1024> data;
   for (;;)
   {
      size_t n = co_await socket.async_read_some(buffer(data));
      co_await async_write(socket, buffer(data, n));
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
