#include <boost/asio.hpp>

#include <thread>

using namespace boost::asio;
using namespace std::chrono;
using namespace std::chrono_literals;

int main()
{
   io_context context;
   thread_pool pool{10};

   size_t count = 0;
   for (size_t i = 0; i < 20; ++i)
      co_spawn(context, [&] -> awaitable<void>
      {
         co_await post(bind_executor(pool));
         std::this_thread::sleep_for(100ms);
         co_await post(deferred);
         count++;
         co_return;
      }, detached);

   auto t0 = std::chrono::system_clock::now();
   context.run();
   auto t1 = std::chrono::system_clock::now();
   std::println("count={}, ran for {}ms", count, duration_cast<milliseconds>(t1 - t0).count());
}
