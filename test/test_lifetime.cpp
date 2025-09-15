#include "asio-coro.hpp"
#include "utils.hpp"

#include <expected>
#include <gtest/gtest.h>

#include <boost/scope/scope_exit.hpp>

using namespace std::chrono_literals;
using namespace boost::asio::experimental::awaitable_operators;
using boost::scope::make_scope_exit;

// =================================================================================================

/**
 * Simple, self-contained example that shows that "work" is tracked across coroutines automatically.
 * If it wasn't, running the IO context would complete before the coroutine finished.
 */
TEST(Lifetime, WHEN_task_is_spawned_THEN_work_is_tracked)
{
   bool ok = false;
   boost::asio::io_context context;
   co_spawn(context.get_executor(), [&]() -> asio::awaitable<void>
   {
      co_await yield();
      ok = true;
      co_return;
   }, asio::detached);
   ::run(context);
   EXPECT_TRUE(ok);
}

TEST(Lifetime, WHEN_task_is_finished_THEN_sets_future)
{
   bool ok = false;
   boost::asio::io_context context;
   auto future = co_spawn(context.get_executor(), [&]() -> asio::awaitable<bool>
   {
      co_await yield();
      co_return true;
   }, asio::use_future);
   ::run(context);
   EXPECT_TRUE(future.get());
}

// ================================================================================================

//
// Did weird things to the indentation after adding std::invoke(), so disabled here:
// clang-format off
//

//
// This must be the most obvious testcase. But we have it anyway, just to illustrate the difference
// between invoking normal functions (here: a lambda)...
//
TEST(Lifetime, WHEN_lambda_is_invoked_THEN_body_is_executed_immediately) // duh
{
   auto answer = std::invoke([] -> int { return 42; });
   EXPECT_EQ(answer, 42);
}

//
// ... and invoking coroutines: ASIO coroutines are lazy!
//
TEST(Lifetime, WHEN_coroutine_is_invoked_THEN_body_is_not_executed_until_awaited)
{
   auto awaitable = std::invoke([] -> asio::awaitable<int>
   {
      ADD_FAILURE() << "body is not executed until awaited";
      co_return 143;
   });
}

// ================================================================================================

//
// CP.51: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcoro-capture
//
TEST(Lifetime, WHEN_get_awaitable_from_lambda_THEN_closure_is_destroyed)
{
   static bool alive = true;
   auto scope_exit = make_scope_exit([] { alive = false; });

   auto awaitable = std::invoke([scope_exit = std::move(scope_exit)]() -> asio::awaitable<int>
   {
      ADD_FAILURE() << "body is not executed until awaited";
      co_return 143;
   });

   EXPECT_FALSE(alive);
}

TEST(Lifetime, WHEN_get_awaitable_from_lambda_THEN_coroutine_frame_is_still_alive)
{
   static bool alive = true;
   auto scope_exit = make_scope_exit([] { alive = false; });

   auto awaitable = std::invoke([](auto) -> asio::awaitable<int>
   {
      ADD_FAILURE() << "body is not executed until awaited";
      co_return 143;
   }, std::move(scope_exit));

   EXPECT_TRUE(alive); // now part of coroutine frame
}

TEST(Lifetime, WHEN_spawn_lambda_awaitable_THEN_closure_is_deleted)
{
   static bool alive = true;
   auto scope_exit = make_scope_exit([] { alive = false; });

   boost::asio::io_context context;
   auto future = co_spawn(
      context.get_executor(),
      std::invoke([scope_exit = std::move(scope_exit)]() -> asio::awaitable<int>
      {
         EXPECT_FALSE(alive);
         co_return 143;
      }),
      asio::use_future);
   context.run();

   EXPECT_EQ(future.get(), 143);
}

TEST(Lifetime, WHEN_await_lambda_awaitable_THEN_closure_is_kept_alive_by_full_expression)
{
   boost::asio::io_context context;
   auto future = co_spawn(
      context.get_executor(),
      std::invoke([]() -> asio::awaitable<int>
      {
         static bool alive = true;
         auto scope_exit = make_scope_exit([] { alive = false; });
         co_return co_await std::invoke([scope_exit = std::move(scope_exit)]() -> asio::awaitable<int>
         {
            EXPECT_TRUE(alive);
            co_return 143;
         });
      }),
      asio::use_future);
   context.run();

   EXPECT_EQ(future.get(), 143);
}

TEST(Lifetime, WHEN_spawn_lambda_without_invoking_it_THEN_closure_is_kept_alive)
{
   static bool alive = true;
   auto scope_exit = make_scope_exit([] { alive = false; });

   boost::asio::io_context context;
   auto future = co_spawn(
      context.get_executor(),
      [scope_exit = std::move(scope_exit)]() -> asio::awaitable<int>
      {
         EXPECT_TRUE(alive);
         co_return 143;
      },
      asio::use_future);
   context.run();

   EXPECT_EQ(future.get(), 143);
}

// -------------------------------------------------------------------------------------------------

//
// Even if capturing everything by reference using [&], ASAN flags this:
//
TEST(Lifetime, DISABLED_WHEN_spawn_lambda_captures_everything_by_reference_THEN_is_flagged_by_asan)
{
   bool alive = true;

   boost::asio::io_context context;
   auto future = co_spawn(
      context.get_executor(),
      std::invoke([&]() -> asio::awaitable<int>
      {
         alive = false;
         co_return 143;
      }),
      asio::use_future);
   context.run();

   EXPECT_EQ(future.get(), 143);
}

// =================================================================================================

TEST(Lifetime, DISABLED_WHEN_coroutine_holds_lock_across_suspension_point_THEN_locks_up)
{
   std::mutex mutex;
   boost::asio::io_context context;
   for (size_t i = 0; i < 2; ++i)
      co_spawn(context.get_executor(), [&]() -> asio::awaitable<void>
      {
         auto lock = std::unique_lock(mutex);
         co_await sleep(100ms);
      },
      detached);
   context.run();

   std::expected<size_t, boost::system::system_error> test;
}

// =================================================================================================
