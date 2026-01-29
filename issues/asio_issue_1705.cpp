/**
 * https://github.com/chriskohlhoff/asio/issues/1705
 *
 * Demonstrates that cancelling a promise via cancel_after doesn't work properly, as opposed to
 * cancelling it via a parallel group. The reason seems to be that the completion handler is
 * invoked immediately and not posted().
 */
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <print>

using namespace boost::asio;
using namespace experimental;
using namespace awaitable_operators;
using namespace std::chrono_literals;

awaitable<void> cancel_promise(std::string variant)
{
   auto ex = co_await this_coro::executor;

   steady_timer forever(ex);
   forever.expires_after(steady_timer::duration::max());
   auto promise = forever.async_wait(use_promise);

   std::println("{} awaiting promise...", variant);
   steady_timer timer(ex);
   timer.expires_after(1ms);

   // doesn't compile with Boost 1.90 because of missing executor_type / get_executor()
   // co_await std::move(promise)(as_tuple(cancel_after(1ms)));

   if (variant == "cancel_after")
      co_await std::move(promise)(as_tuple(cancel_after(timer, 1ms)));
   else if (variant == "parallel group")
      co_await (std::move(promise)(use_awaitable) || timer.async_wait(use_awaitable));

   std::println("{} awaiting promise... STILL THERE", variant);
}

static_assert(is_async_operation<promise<>>::value);

int main()
{
   boost::asio::io_context context;
   co_spawn(context, cancel_promise("cancel_after"),
            [](const std::exception_ptr&) { std::println("cancel_after completed"); });
   co_spawn(context, cancel_promise("parallel group"),
            [](const std::exception_ptr&) { std::println("parallel group completed"); });
   context.run();
}
