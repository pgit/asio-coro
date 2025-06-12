#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;

awaitable<void> session(tcp::socket socket)
{
   std::array<char, 1024*64> data;
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
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   
   std::vector<std::thread> threads(std::thread::hardware_concurrency());
   for (auto& thread : threads)
      thread = std::thread([&]() { context.run(); });
   context.run();
   for (auto& thread : threads)
      thread.join();   
}
