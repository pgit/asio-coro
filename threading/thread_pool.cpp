#include <boost/asio.hpp>

#include <chrono>
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
         //
         // To move execution to the thread pool, we can use post() with a completion token that
         // is bound to the desired target executor. Note we use the "nullary" overload of post()
         // here -- it has no parameters except the completion token. Post itself is a no-op.
         //
         co_await post(bind_executor(pool));
         std::this_thread::sleep_for(100ms); // simulate synchronous work

         //
         // We cannot increment the counter while still in the thread pool, this is a data race.
         // So, we move execution of this coroutine back to the original executor. For this, we
         // can simply post and complete with the default 'deferred' token, even without explicitly
         // binding an executor to it.
         //
         // post() will fall back to the coroutine's executor, which has been set at spawn time.
         //
         co_await post(deferred);
         count++;
         co_return;
      }, detached);

   auto t0 = system_clock::now();
   context.run();
   auto t1 = system_clock::now();
   std::println("count={}, ran for {}ms", count, duration_cast<milliseconds>(t1 - t0).count());
}
