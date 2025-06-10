#include "asio-coro.hpp"
#include "utils.hpp"

#include <gtest/gtest.h>

#include <latch>
#include <ranges>
#include <thread>

using namespace std::chrono_literals;
using std::chrono::milliseconds;
using std::chrono::steady_clock;
namespace ranges = std::ranges;
namespace rv = ranges::views;

// =================================================================================================

class CompletionToken : public testing::Test
{
public:
   void SetUp() override
   {
      executor = context.get_executor();
      work.emplace(executor);
      thread = std::thread([this]() { ::run(context); });
      t0 = steady_clock::now();
   }

   inline milliseconds runtime() const { return floor<milliseconds>(steady_clock::now() - t0); }

   milliseconds shutdown()
   {
      EXPECT_TRUE(work);
      work.reset();
      thread.join();
      return runtime();
   }

   void TearDown() override
   {
      if (work)
         shutdown();
   }

protected:
   steady_clock::time_point t0;
   io_context context;
   any_io_executor executor;
   std::optional<executor_work_guard<any_io_executor>> work;
   std::thread thread;
};

// -------------------------------------------------------------------------------------------------

TEST_F(CompletionToken, Empty) {}

TEST_F(CompletionToken, WHEN_timer_is_destroyed_THEN_is_cancelled)
{
   steady_timer timer(executor);
   timer.expires_after(100ms);
   timer.async_wait(detached);
   EXPECT_LE(runtime(), 100ms);
}

TEST_F(CompletionToken, WHEN_timer_is_shared_using_consign_THEN_keeps_context_alive)
{
   auto timer = std::make_shared<steady_timer>(executor);
   timer->expires_after(100ms);
   timer->async_wait(consign(detached, timer));
   EXPECT_GE(shutdown(), 100ms);
}

TEST_F(CompletionToken, WHEN_timer_is_shared_using_lambda_THEN_keeps_context_alive)
{
   auto timer = std::make_shared<steady_timer>(executor);
   timer->expires_after(100ms);
   timer->async_wait([timer](error_code) {});
   EXPECT_GE(shutdown(), 100ms);
}

// -------------------------------------------------------------------------------------------------

TEST_F(CompletionToken, WHEN_timer_completes_THEN_latch_is_decremented)
{
   steady_timer timer(executor);
   timer.expires_after(100ms);
   std::latch latch(1);
   timer.async_wait([&](error_code) mutable { latch.count_down(); });
   latch.wait();
}

TEST_F(CompletionToken, WHEN_multiple_timers_are_created_THEN_latch_is_decremented)
{
   constexpr size_t N = 100;
   std::latch latch(N);
   std::vector<steady_timer> timers;
   timers.reserve(N);
   for (size_t i = 0; i < N; ++i)
   {
      timers.emplace_back(executor);
      timers.back().expires_after(i * 1ms);
      timers.back().async_wait([&](error_code) mutable { latch.count_down(); });
   }
   latch.wait();
}

TEST_F(CompletionToken, WHEN_multiple_timers_are_created_with_ranges_THEN_latch_is_decremented)
{
   constexpr size_t N = 100;
   std::latch latch(N);
   auto timers = rv::iota(0u, N) |
                 rv::transform(
                    [&](auto i) -> steady_timer
                    {
                       steady_timer timer(executor);
                       timer.expires_after(i * 1ms);
                       timer.async_wait([&](error_code) mutable { latch.count_down(); });
                       return timer;
                    }) |
                 ranges::to<std::vector>();
   latch.wait();
}

// -------------------------------------------------------------------------------------------------

TEST_F(CompletionToken, WHEN_timer_completes_THEN_lambda_is_called)
{
   steady_timer timer(executor);
   timer.expires_after(100ms);
   std::promise<void> promise;
   auto future = promise.get_future();
   timer.async_wait([promise = std::move(promise)](error_code) mutable { promise.set_value(); });
   EXPECT_NO_THROW(future.get());
}

// -------------------------------------------------------------------------------------------------

TEST_F(CompletionToken, WHEN_timer_completes_THEN_future_is_fulfilled)
{
   steady_timer timer(executor);
   timer.expires_after(100ms);
   auto future = timer.async_wait(use_future);
   EXPECT_NO_THROW(future.get());
   EXPECT_GE(runtime(), 100ms);
}

TEST_F(CompletionToken, WHEN_timer_is_cancelled_THEN_future_throws)
{
   steady_timer timer(executor);
   timer.expires_after(100ms);
   auto future = timer.async_wait(use_future);
   timer.cancel();
   EXPECT_THROW(future.get(), system_error);
   EXPECT_LE(runtime(), 100ms);
}

// -------------------------------------------------------------------------------------------------

TEST_F(CompletionToken, WHEN_timer_completes_THEN_coroutine_is_resumed)
{
   co_spawn(
      executor,
      [&]() -> awaitable<void>
      {
         steady_timer timer(executor);
         timer.expires_after(100ms);
         co_await timer.async_wait();
      },
      detached);
}

// -------------------------------------------------------------------------------------------------

TEST_F(CompletionToken, WHEN_timer_completes_THEN_deferred_invokes_handler_later)
{
   steady_timer timer(executor);
   timer.expires_after(50ms);

   bool handler_called = false;
   auto deferred_op = timer.async_wait(deferred);

   // The operation is lazy, so it is not started until we invoke it.
   std::this_thread::sleep_for(60ms);
   EXPECT_FALSE(handler_called);

   // Now, invoke the deferred operation, which will initiate the async operation.
   deferred_op([&](error_code) { handler_called = true; });
   EXPECT_FALSE(handler_called);
   std::this_thread::sleep_for(1ms);
   EXPECT_FALSE(handler_called);

   std::this_thread::sleep_for(60ms);
   EXPECT_TRUE(handler_called);
}

// =================================================================================================
