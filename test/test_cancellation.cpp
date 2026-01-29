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
// Wrapp a task to log cancellation events.
//
#if 1
template <AwaitableOrCallableAwaitable T>
awaitable<void> log_cancellation(T&& task)
{
   auto ex = co_await this_coro::executor;
   auto cs = co_await this_coro::cancellation_state;

   auto scope_exit = make_scope_exit([] { std::println("session coroutine frame destroyed"); });

   cancellation_signal signal;
   cs.slot().assign([&](cancellation_type ct)
   {
      std::println("session cancelled ({}), emitting signal...", ct);
      signal.emit(ct);
      std::println("session cancelled ({}), emitting signal... done", ct);
   });

   co_return co_await co_spawn(ex, std::forward<T>(task), bind_cancellation_slot(signal.slot()));
}
#else
#define log_cancellation(task) task
#endif

//
// This testcase simulates what can happen when cancelling any asynchronous operation:
// If the completion handler is already scheduled for execution, then cancellation cannot
// reach it in time any more. The operation completes successfully, albeit the cancellation
// does become visible in the cancellation state.
//
// To simulate an asynchronous operation that is already scheduled for completion, we just post().
// This completes unconditionally, so the continuation of the coroutine is scheduled immediately.
//
/**
 # Consider enabling BOOST_ASIO_ENABLE_HANDLER_TRACKING in toplevel CMakeLists.txt for this
 * https://think-async.com/Asio/asio-1.36.0/doc/asio/overview/core/handler_tracking.html
 *
 * @asio|1765835122.024152|0*1|io_context@0x63b002a3a0f0.execute
 * ------------------------------------------------------------------------------
 * @asio|1765835122.024168|>1|
 * started
 * @asio|1765835122.024186|1^2|in '...' (/workspaces/asio-coro/test/test_cancellation.cpp:121)
 * @asio|1765835122.024186|1*2|io_context@0x63b002a3a0f0.execute
 * cancelled (terminal)
 * @asio|1765835122.024208|1^3|in '...' (/workspaces/asio-coro/test/test_cancellation.cpp:130)
 * @asio|1765835122.024208|1*3|io_context@0x63b002a3a0f0.execute
 * @asio|1765835122.024212|<1|
 * --- 0 ------------------------------------------------------------------------
 * @asio|1765835122.024229|>2|
 * resumed
 * @asio|1765835122.024237|<2|
 * --- 1 ------------------------------------------------------------------------
 * @asio|1765835122.024259|>3|
 * done
 * @asio|1765835122.024272|<3|
 * --- 2 ------------------------------------------------------------------------
 */
TEST_F(Cancellation, WHEN_task_is_cancelled_when_already_scheduled_THEN_is_resumed)
{
   co_spawn(executor, []() -> awaitable<void> // |0*1|
   {
      // |>1|
      auto ex = co_await this_coro::executor;
      bool resumed = false;
      auto promise = std::make_optional(co_spawn(ex,
                                                 log_cancellation(
                                                    [&]() -> awaitable<void>
      {
         // still in |1| -- with use_promise, co_spawn executes the awaitable eagerly!
         std::println("started");
         auto cs = co_await this_coro::cancellation_state;
         cs.slot().assign([&](cancellation_type ct) { //
            std::println("cancelled ({})", ct);
         });

         EXPECT_EQ(cs.cancelled(), cancellation_type::none);
         co_await post(deferred); // |1*2| ... |>2|
         std::println("resumed");
         resumed = true;
         EXPECT_EQ(cs.cancelled(), cancellation_type::terminal);
         co_return; // |<2|
      }),
                                                 use_promise));

      promise.reset(); // executes cancellation handler above and prints "cancelled (terminal)"
      EXPECT_FALSE(resumed);
      co_await post(deferred); // |1*3|, |<1| ... |>3|
      std::println("done");
      EXPECT_TRUE(resumed);
      co_return; // |<3|
   }, detached);
}

// -------------------------------------------------------------------------------------------------

//
// For comparison, if an asynchronous operation is actually still waiting and not scheduled for
// completion, it can be cancelled properly and will never be resumed.
//
//  -----------------------------
//   sleeping
//  ---- 0 ----------------------
//   cancelling...
//   cancelling... done
//  ---- 1 ----------------------
//   frame destroyed
//   closure destroyed
//  ---- 2 ----------------------
//   done
//  ---- 3 ----------------------
//
TEST_F(Cancellation, WHEN_task_is_cancelled_THEN_is_not_resumed)
{
   test = []() -> awaitable<void>
   {
      auto ex = co_await this_coro::executor;
      auto promise = std::make_optional(co_spawn(ex,
                                                 [&, closure = make_scope_exit([]
      { std::println("closure destroyed"); })]() -> awaitable<void>
      {
         auto cs = co_await this_coro::cancellation_state;
         auto frame =
            make_scope_exit([&] { std::println("frame destroyed ({})", cs.cancelled()); });
         EXPECT_EQ(cs.cancelled(), cancellation_type::none);
         std::println("sleeping");
         co_await sleep(1s);
         EXPECT_TRUE(false) << "this code is never executed";
      },
                                                 use_promise));

      // co_await yield();
      std::println("cancelling...");
      promise.reset();
      std::println("cancelling... done");
      // co_await yield();
      std::println("done");
   };

   EXPECT_CALL(*this, on_complete(error_code{}));
}

// -------------------------------------------------------------------------------------------------

//
// However, if you catch the cancellation error, the coroutine is always resumed:
// This time, there is no cancellation race, and we get both the error code 'operation_canceled'
// and cancellation type 'terminal'.
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
         auto [ec] = co_await timer.async_wait(as_tuple); // catch cancellation error
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
