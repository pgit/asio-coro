/**
 * Tasks are lazy, so they cannot be used to start work in background.
 */
#include "run.hpp"

#include <boost/asio.hpp>

#include <boost/cobalt.hpp>
#include <boost/cobalt/main.hpp>

#include <print>

using namespace boost;
using namespace boost::asio;
using namespace std::chrono_literals;

cobalt::task<void> sleep(std::string message, steady_timer::duration timeout)
{
   steady_timer timer(co_await cobalt::this_coro::executor);
   timer.expires_after(timeout);
   std::println("sleeping: {}...", message);
   auto [ec] = co_await timer.async_wait(as_tuple);
   std::println("sleeping: {}... done ({})", message, ec.message());
}

// Task-based version for manual spawn - no eager execution
cobalt::task<void> task()
{
   auto ex = co_await cobalt::this_coro::executor;

   // With tasks, both start when race() begins (not eager)
   auto long_task = sleep("long time -- this is NOT started eagerly", 10s);

   co_await sleep("delay", 1s);

   // Both tasks start here
   co_await race(std::move(long_task), sleep("short time", 1s));
}

int main()
{
   boost::asio::io_context context;
   cobalt::spawn(context, task(), detached);
   runDebug(context);
}
