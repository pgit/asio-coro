/**
 * When using promises, `cobalt::this_thread::set_executor()` has to be called in advance.
 */
#include <boost/asio.hpp>

#include <boost/cobalt.hpp>
#include <boost/cobalt/main.hpp>

#include <print>
#include "run.hpp"

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

cobalt::task<void> task()
{
   auto ex = co_await cobalt::this_coro::executor;
   auto promise = sleep("long time", 10s);
   co_await sleep("delay", 1s);
   co_await race(std::move(promise), sleep("short time", 1s));
}

int main()
{
   boost::asio::io_context context;

   // Mirror cobalt::main by giving this thread its own executor + PMR pool.
   cobalt::this_thread::set_executor(context.get_executor());

   cobalt::spawn(context, task(), [](const std::exception_ptr&) {
      std::println("cancel_after completed");
   });
   runDebug(context);
}
