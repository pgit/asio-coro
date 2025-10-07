#include "asio-coro.hpp"
#include "async_invoke.hpp"
#include "run.hpp"

#include <boost/asio.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace boost::asio;
using boost::system::error_code;

using namespace ::testing;
using namespace std::chrono_literals;

using Duration = std::chrono::milliseconds;

// =================================================================================================

class AsyncInvoke : public ::testing::Test
{
   io_context context;
   thread_pool thread_pool{10};

protected:
   io_context::executor_type executor{context.get_executor()};
   thread_pool::executor_type pool{thread_pool.get_executor()};
   [[maybe_unused]] size_t run() { return ::run(context); }
};

// -------------------------------------------------------------------------------------------------

/**
 * Create a simple synchronous task to be run in the thread pool. If the counter is incremented
 * from within the task, it must be made atomic or otherwise protected.
 */
TEST_F(AsyncInvoke, WHEN_in_pool_THEN_counter_needs_protection)
{
   std::atomic<size_t> count = 0;
   auto token = bind_executor(executor, detached);
   for (size_t i = 0; i < 20; ++i)
      async_invoke(pool, token, [&](Duration duration) -> error_code
      {
         std::println("Sleeping...");
         std::this_thread::sleep_for(duration);
         std::println("Sleeping... done");
         count++;
         return error_code{};
      }, 100ms);
   run();
   EXPECT_EQ(count, 20);
}

// -------------------------------------------------------------------------------------------------

/**
 * If the completion token (here, a lambda) is bound to the single-threaded 'executor',
 * incrementing the counter does not need to be protected.
 *
 * Note that async_invoke() needs to register work with the handler's executor. If we didn't do
 * that, the executor would right after the jobs have been submitted to the pool.
 */
TEST_F(AsyncInvoke, WHEN_in_completion_handler_THEN_counter_does_not_need_protection)
{
   size_t count = 0;
   auto token = bind_executor(executor, [&](error_code) { count++; });
   for (size_t i = 0; i < 20; ++i)
      async_invoke(pool, token, [](Duration duration) -> error_code
      {
         std::println("Sleeping...");
         std::this_thread::sleep_for(duration);
         std::println("Sleeping... done");
         return error_code{};
      }, 100ms);
   run();
   EXPECT_EQ(count, 20);
}

// -------------------------------------------------------------------------------------------------

/**
 * Manually move execution of a coroutine to another executor. Note that it is not enough to just
 * post on the destination pool -- the completion handler itself has to be bound to it. This makes
 * sense because we are not actually posting any work, but only scheduling a completion handler.
 */
TEST_F(AsyncInvoke, WHEN_post_to_different_executor_THEN_coroutine_continues_there)
{
   size_t count = 0;
   auto executor_thread_id = std::this_thread::get_id();
   for (size_t i = 0; i < 20; ++i)
      co_spawn(executor, [&] mutable -> awaitable<void>
      {
         co_await dispatch(executor, bind_executor(pool));
         EXPECT_NE(executor_thread_id, std::this_thread::get_id());

         std::println("Sleeping...");
         std::this_thread::sleep_for(100ms);
         std::println("Sleeping... done");

         co_await dispatch(pool, bind_executor(executor));
         EXPECT_EQ(executor_thread_id, std::this_thread::get_id());
         count++;
      }, detached);
   run();
   EXPECT_EQ(count, 20);
}

// -------------------------------------------------------------------------------------------------

/**
 * This may be a little surprising: When using co_await with the 'deferred' completion token, that
 * token will inherit the completion executor of the coroutine. It is not the current executor.
 *
 * ChatGPT 5 has a surprisingly good explanation on the subject:
 *
 * When you co_await an Asio async operation (e.g., async_invoke(pool, ...)),
 * two executors are involved:
 *
 * - Initiation executor — the one used to start the async operation.
 *   This is determined by where the call to async_invoke executes (in this case, a pool thread).
 *
 * - Completion executor — the one used to resume the awaiting coroutine when the operation
 *   completes. This is determined by the associated executor of the coroutine’s completion
 *   handler — which, for a coroutine using use_awaitable or deferred, is the coroutine’s own
 *   original executor, not the one you’re currently running on.
 *
 * So even though the async operation is invoked fromthe  pool thread, when it completes, the
 * coroutine is resumed via the executor it was originally spawned with (the one passed to
 * co_spawn). This way, when the coroutine is resumed, it will continue in the same executor
 * where it was suspended.
 *
 * This behavior can be overriden by binding an executor to the \c deferred completion token.
 */
TEST_F(AsyncInvoke, WHEN_count_after_await_THEN_counter_does_not_need_protection)
{
   size_t count = 0;
   auto executor_thread_id = std::this_thread::get_id();
   for (size_t i = 0; i < 20; ++i)
      co_spawn(executor, [&] mutable -> awaitable<void>
      {
         co_await post(executor, bind_executor(pool));
         EXPECT_NE(executor_thread_id, std::this_thread::get_id()); // in pool
         co_await async_invoke(pool, deferred, [&](Duration duration) -> error_code
         {
            std::println("Sleeping...");
            std::this_thread::sleep_for(duration);
            std::println("Sleeping... done");
            return error_code{};
         }, 100ms);
         EXPECT_EQ(executor_thread_id, std::this_thread::get_id()); // back in executor
         count++;
         co_return;
      }, detached);
   run();
   EXPECT_EQ(count, 20);
}

// -------------------------------------------------------------------------------------------------

TEST_F(AsyncInvoke, WHEN_async_invoke_without_token_THEN_uses_default_token)
{
   size_t count = 0;
   for (size_t i = 0; i < 20; ++i)
      co_spawn(executor, [&] mutable -> awaitable<void>
      {
         co_await async_invoke(pool, [&](Duration duration) -> error_code
         {
            std::this_thread::sleep_for(duration);
            return error_code{};
         }, 100ms);
         count++;
         co_return;
      }, detached);
   run();
   EXPECT_EQ(count, 20);
}

// -------------------------------------------------------------------------------------------------

/// This has ~30% TSAN failure with libc++, possibly because of missing TSAN instrumentation.
TEST_F(AsyncInvoke, DISABLED_WHEN_job_returns_error_THEN_is_thrown_by_future)
{
   namespace errc = boost::system::errc;
   auto future = async_invoke(pool, asio::use_future, [&](Duration duration) -> error_code
   {
      std::this_thread::sleep_for(duration);
      return errc::make_error_code(errc::timed_out);
   }, 100ms);
   EXPECT_THROW(future.get(), boost::system::system_error);
}

// =================================================================================================
