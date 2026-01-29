/**
 * Use co_main and start (eager) promises. Also supports signals.
 */
#include <boost/asio.hpp>

#include <boost/cobalt.hpp>
#include <boost/cobalt/main.hpp>

#include <print>

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

cobalt::main co_main(int, char**)
{
   auto ex = co_await cobalt::this_coro::executor;
   auto promise = sleep("long time", 10s);
   co_await sleep("delay", 1s);
   co_await race(std::move(promise), sleep("short time", 1s));
   co_return 0;
}
