/**
 * https://github.com/boostorg/asio/issues/458
 *
 * Issue: non-terminal cancellation of parallel_group does not cancel remaining operations
 *
 * If only one of two operations in parallel group react to a cancellation signal, the other one
 * is not cancelled -- at all. If the reacting operation completes and, because of that, the group
 * completes, the remaining operation is not cancelled again.
 *
 * The only workaround for now it to ensure that all operations in a parallel group react to the
 * same cancellation signals.
 *
 * This issue is still open and present in Boost.ASIO 1.89 as of 2024-11-22. It is not fully clear
 * if this is really a bug or just complex behaviour when mixing different cancellation types.
 */
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
   co_await this_coro::reset_cancellation_state([filter](cancellation_type type)
   {
      auto filtered = type & filter;
      std::print(std::cout, "FILTER({}): {} -> {}\n", std::to_underlying(filter),
                 std::to_underlying(type), std::to_underlying(filtered));
      return filtered;
   });

   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(2s);

   std::print("waiting...\n");
   auto [ec] = co_await timer.async_wait(as_tuple);
   std::print("waiting... {}\n", ec.what());
   if (ec)
      throw system_error(ec);
}

awaitable<void> group()
{
   auto ex = co_await this_coro::executor;
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());
   auto group = make_parallel_group(co_spawn(ex, task(terminal | partial | total)),
                                    co_spawn(ex, task(terminal)));
   co_await std::move(group).async_wait(wait_for_one(), deferred);
}

int main()
{
   boost::asio::io_context context;
   co_spawn(context, group(), cancel_after(1ms, total, detached));
   context.run();
}
