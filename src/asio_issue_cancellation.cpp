#include "formatters.hpp" // IWYU pragma: keep

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

using namespace boost::asio;
using namespace experimental;
using namespace awaitable_operators;
using namespace std::chrono_literals;

using enum cancellation_type;

awaitable<void> task(cancellation_type filter)
{
#if 1
   co_await this_coro::reset_cancellation_state(
      [filter](cancellation_type type)
      {
         auto filtered = type & filter;
         if (filtered == none)
            std::println("FILTER({}): {} -> \x1b[1;31m{}\x1b[0m", filter, type, filtered);
         else
            std::println("FILTER({}): {} -> {}", filter, type, filtered);
         return filtered;
      });
#else
   co_await this_coro::reset_cancellation_state(asio::enable_total_cancellation());
#endif

   auto cs = co_await this_coro::cancellation_state;
   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(2s);

   try
   {
      co_await timer.async_wait();
   }
   catch (boost::system::system_error& ex)
   {
      std::println("CANCELLED: {}", cs.cancelled());
      throw;
   }
   std::println("timer completed");
}

/// This task will be cancelled with type 'total', the weakest type of cancellation.
awaitable<void> group()
{
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());
   auto executor = co_await this_coro::executor;

   //
   // When waiting on a single task, cancellation happens only if the requested cancellation type
   // matches the filter set in the task.
   //
   // When grouping a task that reacts to 'total' cancellation with other tasks that don't,
   // only that task will be cancelled.
   //
   // However, the parallel group does not complete, regardless of the wait type.
   //
#if 0
   co_await (task(terminal | partial | total) && task(terminal));
#else // mostly equivalent
   auto group = make_parallel_group(co_spawn(executor, task(terminal | partial | total), deferred),
                                    co_spawn(executor, task(terminal), deferred));
   co_await std::move(group).async_wait(wait_for_one_error(), deferred);
#endif
}

int main()
{
   boost::asio::io_context context;
   auto executor = context.get_executor();
   co_spawn(executor, group(), cancel_after(0s, cancellation_type::total, detached));
   context.run();
}
