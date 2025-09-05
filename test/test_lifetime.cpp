#include "asio-coro.hpp"
#include "utils.hpp"

#include <gtest/gtest.h>

#include <boost/scope/scope_exit.hpp>

using namespace std::chrono_literals;
using namespace boost::asio::experimental::awaitable_operators;
using boost::scope::make_scope_exit;

// =================================================================================================

/**
 * Simple, self-contained example that shows that "work" is tracked across coroutines automatically.
 * If it wasn't, running the IO context would complete before the timer finished.
 */
TEST(Lifetime, WHEN_task_is_spawned_THEN_work_is_tracked)
{
   boost::asio::io_context context;
   bool ok = false;
   co_spawn(
      context.get_executor(),
      [&]() -> asio::awaitable<void>
      {
         co_await yield();
         ok = true;
         co_return;
      },
      asio::detached);
   ::run(context);
   EXPECT_TRUE(ok);
}

TEST(Lifetime, WHEN_task_is_finished_THEN_sets_future)
{
   boost::asio::io_context context;
   auto future = co_spawn(
      context.get_executor(),
      [&]() -> asio::awaitable<bool>
      {
         co_await yield();
         co_return true;
      },
      asio::use_future);
   ::run(context);
   EXPECT_TRUE(future.get());
}

// ================================================================================================

TEST(Lifetime, WHEN_lambda_is_invoked_THEN_body_is_executed_immediately) // duh
{
   auto answer = [] -> int { return 143; }();
   EXPECT_EQ(answer, 143);
}

//
// ASIO coroutines are lazy!
//
TEST(Lifetime, WHEN_coroutine_is_invoked_THEN_body_is_not_executed_until_awaited)
{
   auto awaitable = [] -> asio::awaitable<int>
   {
      ADD_FAILURE() << "body is not executed until awaited";
      co_return 143;
   }();
}

// ================================================================================================

//
// CP.51: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcoro-capture
//
TEST(Lifetime, WHEN_get_awaitable_from_lambda_THEN_closure_is_destroyed)
{
   static bool alive = true;
   auto scope_exit = make_scope_exit([] { alive = false; });

   auto awaitable = [scope_exit = std::move(scope_exit)]() -> asio::awaitable<int>
   {
      ADD_FAILURE() << "body is not executed until awaited";
      co_return 143;
   }();

   EXPECT_FALSE(alive);
}

TEST(Lifetime, WHEN_get_awaitable_from_lambda_THEN_coroutine_frame_is_still_alive)
{
   static bool alive = true;
   auto scope_exit = make_scope_exit([] { alive = false; });

   auto awaitable = [](auto) -> asio::awaitable<int>
   {
      ADD_FAILURE() << "body is not executed until awaited";
      co_return 143;
   }(std::move(scope_exit));

   EXPECT_TRUE(alive); // now part of coroutine frame
}

TEST(Lifetime, WHEN_spawn_lambda_awaitable_THEN_closure_is_deleted)
{
   static bool alive = true;
   auto scope_exit = make_scope_exit([] { alive = false; });

   boost::asio::io_context context;
   auto future = co_spawn(
      context.get_executor(),
      [scope_exit = std::move(scope_exit)]() -> asio::awaitable<int>
      {
         EXPECT_FALSE(alive);
         co_return 143;
      }(),
      asio::use_future);
   context.run();

   EXPECT_EQ(future.get(), 143);
}

TEST(Lifetime, WHEN_await_lambda_awaitable_THEN_closure_is_kept_alive_by_full_expression)
{
   boost::asio::io_context context;
   auto future = co_spawn(
      context.get_executor(),
      []() -> asio::awaitable<int>
      {
         static bool alive = true;
         auto scope_exit = make_scope_exit([] { alive = false; });
         co_return co_await [scope_exit = std::move(scope_exit)]() -> asio::awaitable<int>
         {
            EXPECT_TRUE(alive);
            co_return 143;
         }();
      }(),
      asio::use_future);
   context.run();

   EXPECT_EQ(future.get(), 143);
}

TEST(Lifetime, WHEN_spawn_lambda_THEN_closure_is_kept_alive)
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

// =================================================================================================
