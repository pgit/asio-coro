#include "asio-coro.hpp"

#include <boost/asio.hpp>
#include <boost/cobalt.hpp>

using namespace boost;
using namespace boost::asio;
using namespace std::chrono_literals;

cobalt::promise<void> sleep(std::string message, steady_timer::duration timeout)
{
   steady_timer timer(co_await cobalt::this_coro::executor);
   timer.expires_after(timeout);
   std::println("sleeping: {}...", message);
   auto [ec] = co_await timer.async_wait(as_tuple);
   std::println("sleeping: {}... done ({})", message, ec.message());
}

cobalt::promise<void> worker(int id)
{
   for (int i = 0; i < 10; ++i)
   {
      std::printf("worker %d tick %d A\n", id, i);
      co_await sleep("200ms", 200ms);
      std::printf("worker %d tick %d V\n", id, i);
   }
}

cobalt::task<void> main_task()
{
   auto ex = co_await this_coro::executor;
   co_await cobalt::with(ex,
                         [](any_io_executor ex) -> cobalt::promise<void>
   {
      // auto p1 = worker(1);
      // auto p2 = worker(2); // cobalt::spawn(ex, worker(2), cobalt::use_op);
      co_await race(worker(1), worker(2));

      // Let workers run for a bit
      co_await sleep("8000ms", 800ms);

      std::puts("leaving scope");
      co_return;
   }, [](any_io_executor ex, std::exception_ptr& ep) -> cobalt::promise<void>
   {
      std::println("\x1b[1;31m{}\x1b[0m: {}", "teardown", what(ep));
      // co_await sleep("100ms", 100ms);
      // co_await post();
      std::println("\x1b[1;31m{}\x1b[0m: done", "teardown");
      // ep = nullptr;
      co_return;
   });

   // workers are cancelled & joined here
   std::puts("scope fully cleaned up");
}

int main()
{
   asio::io_context context;
   cobalt::this_thread::set_executor(context.get_executor());
   cobalt::spawn(context, main_task(), cancel_after(300ms, [](const std::exception_ptr& ep){
      std::println("completed ({})", what(ep));
   }));
   context.run();
}
