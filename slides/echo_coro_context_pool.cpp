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
   struct Context
   {
      io_context context;
      std::thread thread;
   };

   std::vector<Context> threads(std::thread::hardware_concurrency());
   for (auto& thread : threads)
      thread.thread = std::thread(
         [&thread]()
         {
            auto work = make_work_guard(thread.context);
            thread.context.run();
         });

   for (size_t i = 0;; ++i)
   {
      auto executor = threads[i % threads.size()].context.get_executor();
      auto socket = co_await a.async_accept();
      auto fd = socket.release();
      socket = tcp::socket(executor);
      socket.assign(tcp::v4(), fd);
      co_spawn(executor, session(std::move(socket)), detached);
   }
}

int main()
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   context.run();
}
