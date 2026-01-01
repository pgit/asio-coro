#include "asio-coro.hpp"
#include "formatters.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

using namespace boost::asio;
using namespace boost::system;
using namespace experimental;
using namespace awaitable_operators;
using namespace std::chrono_literals;

using enum cancellation_type;

awaitable<void> throw_error()
{
   co_await yield();
   std::println("throwing");
   throw errc::make_error_code(errc::io_error);
}

awaitable<void> task(cancellation_type filter)
{
#if 1
   co_await this_coro::reset_cancellation_state([filter](cancellation_type type)
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

#if 0
   std::println("waiting...");
   if (auto [ec] = co_await timer.async_wait(as_tuple); !ec)
      std::println("waiting... done");
   else
   {
      std::println("waiting... CANCELLED: {} ({})", cs.cancelled(), ec.what());
      throw system_error(ec);
   }
#else // equivalent
   try
   {
      std::println("waiting...");
      co_await timer.async_wait();
      std::println("waiting... done");
   }
   catch (system_error& ex)
   {
      std::println("waiting... CANCELLED: {} ({})", cs.cancelled(), ex.what());
      throw;
   }
#endif
   co_await this_coro::reset_cancellation_state();
}

/// This task will be cancelled with type 'total', the weakest type of cancellation.
awaitable<void> group()
{
   //
   // An ASIO coroutine reacts to 'terminal' cancellation only, by default. If we want it to
   // forward non-terminal cancellation to any subtasks it starts, we have to enable the other
   // cancellation types as well:
   //
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());

   //
   // When waiting on a single task, cancellation happens only if the requested cancellation type
   // matches the filter set in the task.
   //
   // When grouping a task that reacts to 'total' cancellation with other tasks that don't,
   // only that task will be cancelled, which seems correct.
   //
   // However, when that task completes as a result of the cancellation, the other tasks should
   // be cancelled nevertheless, just as if the task had thrown an error.
   //
#if 0
   co_await (task(terminal | partial | total) && task(terminal));
   // co_await (task(terminal | partial | total) && throw_error()); // OK
   // co_await (task(terminal | partial | total) && yield());  // OK
   // co_await (task(total) && throw_error()); // OK
#else // mostly equivalent
   auto executor = co_await this_coro::executor;
   auto group = make_parallel_group(co_spawn(executor, task(terminal | partial | total)),
                                    co_spawn(executor, task(terminal)));
   // co_await std::move(group).async_wait(wait_for_all(), deferred);
   // co_await std::move(group).async_wait(wait_for_one(), deferred);
   // co_await std::move(group).async_wait(wait_for_one_success(), deferred);
   co_await std::move(group).async_wait(wait_for_one_error(), deferred);
   std::println("group completed");
#endif
}

size_t run(boost::asio::io_context& context)
{
#if defined(NDEBUG)
   return context.run();
#else
   size_t i = 0;
   using namespace std::chrono;
   for (auto t0 = steady_clock::now(); context.run_one(); ++i)
   {
      auto t1 = steady_clock::now();
      auto dt = duration_cast<milliseconds>(t1 - t0);
      // clang-format off
      if (dt < 100ms)
         std::println("--- {} ------------------------------------------------------------------------", i);
      else
         std::println("\x1b[1;31m--- {} ({}) ----------------------------------------------------------------\x1b[0m", i, dt);
      // clang-format off
      t0 = t1;
   }
   return i;
#endif
}

int main()
{
   boost::asio::io_context context;
   auto executor = context.get_executor();
   co_spawn(executor, group(), cancel_after(1ms, cancellation_type::total, detached));
   ::run(context);
}
