#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <iostream>
#include <print>

using namespace boost::asio;
using namespace boost::system;
using namespace experimental;
using namespace awaitable_operators;
using namespace std::chrono_literals;

using enum cancellation_type;

awaitable<void> task(cancellation_type filter)
{
   co_await this_coro::reset_cancellation_state(
      [filter](cancellation_type type)
      {
         auto filtered = type & filter;
         std::print(std::cout, "FILTER({}): {} -> {}\n", std::to_underlying(filter), std::to_underlying(type), std::to_underlying(filtered));
         return filtered;
      });

   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(2s);

   std::print("waiting...\n");
   auto [ec] = co_await timer.async_wait(as_tuple);
   std::print("waiting... {}\n", ec.what());
}

awaitable<void> group()
{
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());
   auto executor = co_await this_coro::executor;
   auto group = make_parallel_group(co_spawn(executor, task(terminal | partial | total)),
                                    co_spawn(executor, task(terminal)));
   co_await std::move(group).async_wait(wait_for_one(), deferred);
}

int main()
{
   boost::asio::io_context context;
   auto executor = context.get_executor();
   co_spawn(executor, group(), cancel_after(1ms, cancellation_type::total, detached));
   context.run();
}
