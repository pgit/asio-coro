#include "asio-coro.hpp"
#include "run.hpp"

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

   milliseconds runtime() const { return floor<milliseconds>(steady_clock::now() - t0); }

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

//
// WARNING: The following tests use std::{promise,future} and can produce false positive when run
//          with thread sanitizer. Also, this seems to be a problem specific to libc++ only.
//
//          https://github.com/llvm/llvm-project/issues/104892
//
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

TEST_F(CompletionToken, WHEN_timer_is_cancelled_THEN_error_is_returned_as_tuple)
{
   steady_timer timer(executor);
   timer.expires_after(100ms);
   auto future = timer.async_wait(as_tuple(use_future));
   timer.cancel();
   std::tuple<error_code> result;
   EXPECT_NO_THROW(result = future.get());
   EXPECT_EQ(std::get<0>(result), boost::system::errc::operation_canceled);
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

// =================================================================================================

template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void()) CompletionToken>
auto no_result(CompletionToken&& token)
{
   return boost::asio::async_initiate<CompletionToken, void()>([](auto handler) {}, token);
}

static_assert(std::is_same_v<decltype(no_result(use_future)), std::future<void>>);
static_assert(std::is_same_v<decltype(no_result(as_tuple(use_future))), std::future<std::tuple<>>>);

// -------------------------------------------------------------------------------------------------

template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code)) CompletionToken>
auto error_code_only(CompletionToken&& token)
{
   return boost::asio::async_initiate<CompletionToken, void(error_code)>([](auto handler) {},
                                                                         token);
}

static_assert(std::is_same_v<decltype(error_code_only(use_future)), std::future<void>>);
static_assert(std::is_same_v<decltype(error_code_only(as_tuple(use_future))),
                             std::future<std::tuple<error_code>>>);
static_assert(std::is_same_v<decltype(error_code_only(deferred)(use_future)), std::future<void>>);

// -------------------------------------------------------------------------------------------------

template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, size_t)) CompletionToken>
auto error_and_size(CompletionToken&& token)
{
   return boost::asio::async_initiate<CompletionToken, void(error_code, size_t)>(
      [](auto handler) {}, token);
}

static_assert(std::is_same_v<decltype(error_and_size(use_future)), std::future<size_t>>);

// -------------------------------------------------------------------------------------------------

template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, size_t, int)) CompletionToken>
auto error_and_size_and_int(CompletionToken&& token)
{
   return boost::asio::async_initiate<CompletionToken, void(error_code, size_t, int)>(
      [](auto handler) {}, token);
}

static_assert(std::is_same_v<decltype(error_and_size_and_int(use_future)),
                             std::future<std::tuple<size_t, int>>>);
static_assert(std::is_same_v<decltype(error_and_size_and_int(as_tuple(use_future))),
                             std::future<std::tuple<error_code, size_t, int>>>);

// =================================================================================================
