#include "asio-coro.hpp"
#include "run.hpp"

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace boost::asio;
using namespace experimental;

using namespace ::testing;

// =================================================================================================

class Cancellation : public testing::Test
{
protected:
   auto token()
   {
      return [this](const std::exception_ptr& ep) { on_complete(code(ep)); };
   }

   void TearDown() override
   {
      if (test)
         co_spawn(executor, std::move(test), token());
      runDebug();
   }

   MOCK_METHOD(void, on_complete, (error_code ec), ());

   auto make_system_error(boost::system::errc::errc_t error)
   {
      return boost::system::error_code(error, boost::system::system_category());
   }

   std::function<awaitable<void>()> test;

private:
   io_context context;

protected:
   any_io_executor executor{context.get_executor()};
   void run() { context.run(); }
   void runDebug() { ::run(context); }
};

// =================================================================================================

//
// This testcase simulates what can happen when cancelling any asynchronous operation:
// If the completion handler is already scheduled for execution, then cancellation cannot
// reach it in time any more. The operation completes successfully, albeit the cancellation
// does become visible in the cancellation state.
//
// To simulate an asynchronous operation that is already scheduled for completion, we just post().
// This completes unconditionally, so the continuation of the coroutine is scheduled immediately.
//
TEST_F(Cancellation, WHEN_task_is_cancelled_when_already_scheduled_THEN_is_resumed)
{
   test = [this]() -> awaitable<void>
   {
      auto ex = co_await this_coro::executor;
      bool resumed = false;
      auto promise = std::make_optional(co_spawn(ex, [&]() -> awaitable<void>
      {
         auto cs = co_await this_coro::cancellation_state;
         EXPECT_EQ(cs.cancelled(), cancellation_type::none);
         co_await post(deferred); // yield, is scheduled for completion immediately
         // ...
         resumed = true;
         EXPECT_EQ(cs.cancelled(), cancellation_type::terminal);
         co_return;
      }, use_promise));

      promise.reset();
      EXPECT_FALSE(resumed);
      co_await yield();
      EXPECT_TRUE(resumed);
   };

   EXPECT_CALL(*this, on_complete(error_code{}));
}

// -------------------------------------------------------------------------------------------------

//
// For comparison, if an asynchronous operation is actually still waiting and not scheduled for
// completion, it can be cancelled properly and will never be resumed.
//
TEST_F(Cancellation, WHEN_task_is_cancelled_THEN_is_not_resumed)
{
   test = [this]() -> awaitable<void>
   {
      auto ex = co_await this_coro::executor;
      bool resumed = false;
      auto promise = std::make_optional(co_spawn(ex, [&]() -> awaitable<void>
      {
         auto cs = co_await this_coro::cancellation_state;
         EXPECT_EQ(cs.cancelled(), cancellation_type::none);
         co_await sleep(1s);
         // ...
         resumed = true;
         EXPECT_EQ(cs.cancelled(), cancellation_type::terminal);
         EXPECT_TRUE(false);
         co_return;
      }, use_promise));

      promise.reset();
      EXPECT_FALSE(resumed);
      co_await yield();
      EXPECT_FALSE(resumed); // still false, never was continued
   };

   EXPECT_CALL(*this, on_complete(error_code{}));
}

// -------------------------------------------------------------------------------------------------

//
// However, if you catch the cancellation error, the coroutine is always resumed:
//
TEST_F(Cancellation, WHEN_task_is_cancelled_and_error_is_caught_THEN_is_resumed)
{
   test = [this]() -> awaitable<void>
   {
      auto ex = co_await this_coro::executor;
      bool resumed = false;
      auto promise = std::make_optional(co_spawn(ex, [&]() -> awaitable<void>
      {
         auto cs = co_await this_coro::cancellation_state;
         EXPECT_EQ(cs.cancelled(), cancellation_type::none);
         steady_timer timer(co_await this_coro::executor);
         timer.expires_after(1s);
         auto [ec] = co_await timer.async_wait(as_tuple);
         // ...
         resumed = true;
         EXPECT_EQ(ec, make_system_error(boost::system::errc::operation_canceled));
         EXPECT_EQ(cs.cancelled(), cancellation_type::terminal);
         co_return;
      }, use_promise));

      promise.reset();
      EXPECT_FALSE(resumed);
      co_await yield();
      EXPECT_TRUE(resumed);
   };

   EXPECT_CALL(*this, on_complete(error_code{}));
}

// =================================================================================================
