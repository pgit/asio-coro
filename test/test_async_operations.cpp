#include "asio-coro.hpp"
#include "run.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <print>
#include <thread>

using namespace std::chrono_literals;
using namespace boost::asio::experimental::awaitable_operators;
namespace errc = boost::system::errc;
using errc::make_error_code;

// =================================================================================================

class NoDefault
{
public:
   NoDefault() = delete;
   NoDefault(const NoDefault&) = default;
   NoDefault(NoDefault&&) noexcept = default;
   NoDefault& operator=(const NoDefault&) = default;
   NoDefault& operator=(NoDefault&&) noexcept = default;
   explicit NoDefault(int) {}
};

using Sleep = void(boost::system::error_code);
using SleepHandler = asio::any_completion_handler<Sleep>;

using CompleteNullary = void();
using CompleteNoDefault = void(boost::system::error_code, NoDefault);

using Duration = std::chrono::nanoseconds;

using DefaultCompletionToken = asio::default_completion_token_t<asio::any_io_executor>;

// =================================================================================================

/// https://www.boost.org/doc/libs/1_88_0/doc/html/boost_asio/example/cpp20/type_erasure/sleep.hpp
/**
 * In this example we are defining an async operation that is a free function. As there is no
 * I/O object that we can get an executor from, we have to pass it explicitly.
 *
 * We could also try to use the executor associated with the completion token. But that is only an
 * "completion executor", which is only a subset of an "I/O executor". We cannot create a timer
 * object on it.
 *
 * In the end, the idea to use the completion tokens associated executor for intermediate async
 * operations is misguided, anyway: The executor for those operations should be a property of the
 * operation, not the completion token.
 */
class ComposedAny : public testing::Test
{
   static void async_sleep_impl(SleepHandler handler, boost::asio::any_io_executor ex,
                                Duration duration)
   {
      auto timer = std::make_shared<asio::steady_timer>(ex, duration);
      timer->async_wait(asio::consign(std::move(handler), timer));
      // equivalent to
      // timer->async_wait([timer = std::move(timer), handler = std::move(handler)](error_code ec) {
      //    std::move(handler)(ec);
      // });
   }

public:
   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   static auto async_sleep(boost::asio::any_io_executor ex, Duration duration,
                           CompletionToken&& token)
   {
      return boost::asio::async_initiate<CompletionToken, Sleep>(async_sleep_impl, token,
                                                                 std::move(ex), duration);
   }

   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(CompleteNullary) CompletionToken>
   static auto async_complete_nullary(CompletionToken&& token)
   {
      return boost::asio::async_initiate<CompletionToken, CompleteNullary>([](auto handler)
      {
         auto executor = get_associated_executor(handler);
         post(executor, [handler = std::move(handler)]() mutable
         {
            std::move(handler)(); //
         });
      }, token);
   }

   //
   // FIXME: Have a look at how operator&& is implemented in asio to see how they handle
   //        non-default-constructible types. There is a wrapper called awaitable_wrap<> that
   //        wraps coroutines returning non-default-constructible types in std::optional.
   //
   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(CompleteNoDefault) CompletionToken>
   static auto async_complete_no_default(CompletionToken&& token)
   {
      return boost::asio::async_initiate<CompletionToken, CompleteNoDefault>([](auto handler)
      {
         auto executor = get_associated_executor(handler);
         post(executor, [handler = std::move(handler)]() mutable
         {
            std::move(handler)(error_code{}, NoDefault(42)); //
         });
      }, token);
   }
};

TEST_F(ComposedAny, WHEN_async_op_is_deferred_THEN_is_lazy)
{
   boost::asio::io_context context;
   co_spawn(context, []() -> awaitable<void>
   {
      auto ex = co_await this_coro::executor;
      co_return co_await async_sleep(ex, 100ms, asio::experimental::use_promise);
   }, detached);
   ::run(context);
}

TEST_F(ComposedAny, WHEN_async_op_is_initiated_THEN_tracks_work)
{
   boost::asio::io_context context;
   async_sleep(context.get_executor(), 100ms, asio::detached);
   ::run(context);
}

TEST_F(ComposedAny, WHEN_async_op_finishes_THEN_sets_future)
{
   boost::asio::io_context context;
   auto future = async_sleep(context.get_executor(), 100ms, asio::use_future);
   ::run(context);
   EXPECT_NO_THROW(future.get());
}

// -------------------------------------------------------------------------------------------------

TEST_F(ComposedAny, WHEN_nullary_op_finishes_THEN_yields_nothing)
{
   boost::asio::io_context context;
   co_spawn(context.get_executor(), async_complete_nullary(use_awaitable), detached);
   auto future = async_complete_nullary(bind_executor(context.get_executor(), use_future));
   context.run();
   EXPECT_NO_THROW(future.get());
}

TEST_F(ComposedAny, WHEN_no_default_op_finishes_THEN_yields_nothing)
{
   boost::asio::io_context context;
   //
   // 'co_spawn' and 'use_promise' don't work with types that are not default-constructible
   //
   // https://github.com/chriskohlhoff/asio/blob/231cb29bab30f82712fcd54faaea42424cc6e710/asio/include/asio/impl/co_spawn.hpp#L187
   // co_spawn(context.get_executor(), async_complete_no_default(use_awaitable), detached);
   // https://github.com/chriskohlhoff/asio/blob/231cb29bab30f82712fcd54faaea42424cc6e710/asio/include/asio/experimental/impl/promise.hpp#L167
   // auto async_promise = async_complete_no_default(boost::asio::experimental::use_promise);
   auto future = async_complete_no_default(bind_executor(context.get_executor(), use_future));
   context.run();
   EXPECT_NO_THROW(future.get());
}

// =================================================================================================

class ComposedIndirectExplicit : public testing::Test
{
private:
   auto async_sleep_impl(SleepHandler token, boost::asio::any_io_executor ex, Duration duration)
   {
      return asio::async_initiate<SleepHandler, Sleep>(
         [](auto handler, boost::asio::any_io_executor ex, Duration duration)
      {
         auto timer = std::make_shared<asio::steady_timer>(ex, duration);
         return timer->async_wait(consign(std::move(handler), timer));
      }, token, std::move(ex), duration);
   }

public:
   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   auto async_sleep(boost::asio::any_io_executor ex, Duration duration, CompletionToken&& token)
   {
      return boost::asio::async_initiate<CompletionToken, Sleep>(
         [this](SleepHandler handler, boost::asio::any_io_executor ex, Duration duration)
      {
         async_sleep_impl(std::move(handler), std::move(ex), duration); //
      }, std::forward<CompletionToken>(token), std::move(ex), duration);
   }
};

// -------------------------------------------------------------------------------------------------

TEST_F(ComposedIndirectExplicit, WHEN_async_op_is_initiated_THEN_tracks_work)
{
   boost::asio::io_context context;
   async_sleep(context.get_executor(), 100ms, asio::detached);
   ::run(context);
}

TEST_F(ComposedIndirectExplicit, WHEN_async_op_finishes_THEN_sets_future)
{
   boost::asio::io_context context;
   auto future = async_sleep(context.get_executor(), 100ms, asio::use_future);
   ::run(context);
   EXPECT_NO_THROW(future.get());
}

// =================================================================================================

/**
 * What happens if we delete the completion handler without invoking it?
 */
class DropHandler : public testing::Test
{
public:
   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   static auto drop_handler(CompletionToken&& token)
   {
      return boost::asio::async_initiate<CompletionToken, Sleep>([](SleepHandler handler)
      {
         std::println("drop_handler");
         std::ignore = handler; // what happens if we delete the handler without calling it?
      }, token);
   }
};

TEST_F(DropHandler, DropHandler)
{
   boost::asio::io_context context;

   class Class
   {
   public:
      Class() { std::println("constructor"); }
      ~Class() { std::println("destructor"); }
   };

   bool completed = false;
   co_spawn(context.get_executor(),
            []() -> asio::awaitable<void>
   {
      Class object1;
      co_await drop_handler(asio::deferred);
      Class object2;
   }, [&](const std::exception_ptr& ex)
   {
      if (ex)
         std::println("drop_handler: {}", what(ex));
      else
         std::println("drop_handler:: done");
      completed = true;
   });
   ::run(context);
   EXPECT_FALSE(completed); // completion handler is never invoked
}

TEST_F(DropHandler, DropHandlerFutureSpawned)
{
   boost::asio::io_context context;

   auto future = co_spawn(context.get_executor(),
                          [&]() -> asio::awaitable<void>
   { //
      std::println("dropping handler...");
      co_await drop_handler(asio::deferred);
      std::println("dropping handler... done");
   }, asio::use_future);
   ::run(context);
   EXPECT_THROW(future.get(), std::future_error); // value was never assigned
}

TEST_F(DropHandler, DropHandlerFuture)
{
   boost::asio::io_context context;

   auto future = drop_handler(use_future);
   ::run(context);
   EXPECT_THROW(future.get(), std::future_error); // value was never assigned
}

// =================================================================================================

//
// This variant using co_composed<> does not register work properly: If used with a 'detached'
// completion token, the sleep will not register work in the executor. At least not in the one
// fetchted using get_io_executor()...
//
// UPDATE: Actually, this works. When calling the initiating function, we have to bind the
//         executor to the completion token, as usual. No surprises here. Comparing to
//         async_initiate<>, the only difference is that we get the executor from the state object
//         instead of calling get_associated_executor().
//
// BUT: Even though work is now properly registered to the executor and the testcase works, we have
//      introduced a race condition that becomes visible when running the test with TSAN: When we
//      create a timer on the executor returned by state.get_io_context(), it will run on the
//      system executor! Doing this will lazily create a new thread and introduce the TSAN issues.
//
// We can work around this by creating the timer on the executor of the handler, as returned by
//
//    get_associated_executor(state.handler())
//
// This is a bit awkward, but, in the end, logical. The reason behind this is that this is an async
// operation that does not have an IO object -- but it does use IO (well, at least a timer)
// internally. This makes it difficult to reason about where the timer is scheduled.
//
// In practice, a free-standing asynchronous operation that internally does IO should have an
// explicit executor parameter.
//
class ComposedCoro : public testing::Test
{
public:
   boost::asio::io_context context;

   template <typename CompletionToken>
   auto async_sleep(Duration duration, CompletionToken&& token)
   {
      return asio::async_initiate<CompletionToken, Sleep>( //
         asio::co_composed<Sleep>(
            [this](auto state, Duration duration) -> void
      {
         // auto ex = state.get_io_executor();
         // auto ex = co_await this_coro::executor;
         // auto ex = context.get_executor();

         //
         // Only by using the associated executor from the handler we can avoid starting
         // the timer in the system executor. And we have to construct the timer with that
         // executor. If we do this, we don't need bind_executor() any more.
         //
         auto ex = get_associated_executor(state.handler());

         auto thread_id = std::this_thread::get_id();
         std::println("waiting in thread {}...", thread_id);
         co_await asio::steady_timer(ex, duration).async_wait();
         if (thread_id == std::this_thread::get_id())
            std::println("waiting in thread {}... done", thread_id);
         else
            std::println("waiting in thread {}... done, but now in {}!", thread_id,
                         std::this_thread::get_id());
         ++done;
         co_return {boost::system::error_code{}};
      }),
         token, duration);
   }

   // std::atomic<size_t> done = 0;
   std::size_t done = 0;
};

// -------------------------------------------------------------------------------------------------

//
// This one does not register work properly, because async_sleep() is called with a token that
// is NOT bound to the executor. The async operation will then uses the system executor as fallback.
//
// If this test is run and other tests follow that keep asio running for 100ms, then the
// asynchronous operation will continue to run. Eventually, it will call ++done, which is
// a use-after-free. Or the ++done bleeds into one of the next testcases, as has happened here:
// https://github.com/pgit/asio-coro/actions/runs/18305296533/job/52121061667#step:6:231
//
// Starting with Boost 1.90, this doesn't even compile any more.
//
#if BOOST_VERSION < 109000
TEST_F(ComposedCoro, DISABLED_Unbound)
{
   boost::asio::io_context context;
   async_sleep(100ms, asio::detached);
   ::run(context);
   EXPECT_EQ(done, 0);
}
#endif

//
// This test is only (somewhat) safe because there is a longer sleep in the test that follows.
// If you run it under very high load, it may still fail, very seldomly.
//
// TSAN also reports this, and fails because of TSAN_OPTIONS "halt_on_error=1", so it disabled.
//
// Note that even with BOOST_ASIO_NO_TS_EXECUTORS, the system executor is used as fallback.
//
// Starting with Boost 1.90, this doesn't even compile any more.
//
#if BOOST_VERSION < 109000
TEST_F(ComposedCoro, DISABLED_DefaultExecutor)
{
   boost::asio::io_context context;
   async_sleep(100ms, asio::detached); // this not safe, does not register work with 'context'
   async_sleep(120ms, bind_executor(context, asio::detached));
   ::run(context);
   EXPECT_EQ(done, 2);
}
#endif

//
// Only when binding both tasks to the executor, even with a 'detached' completion token, this
// is safe.
//
TEST_F(ComposedCoro, AnyDetached)
{
   boost::asio::io_context context;
   async_sleep(100ms, bind_executor(context, asio::detached));
   async_sleep(100ms, bind_executor(context, asio::detached));
   ::run(context);
   EXPECT_EQ(done, 2);
}

//
// Also with futures, we need to bind the executor to the completion token. If we don't do this,
// there is a rare chance that the async operation will complete after the test has finished. The
// handler is executed in the system executor, which is out of our control. TSAN shows this, at
// least sometimes.
//
TEST_F(ComposedCoro, AnyFuture)
{
   boost::asio::io_context context;
   auto f1 = async_sleep(100ms, bind_executor(context, asio::use_future));
   auto f2 = async_sleep(100ms, bind_executor(context, asio::use_future));
   ::run(context);
   EXPECT_NO_THROW(f1.get());
   EXPECT_NO_THROW(f2.get());
   EXPECT_EQ(done, 2);
}

// =================================================================================================

//
// Another way to pass the executor to the composed function is to pass the executor as an argument.
// But given that binding the executor works as intended (see ComposedCoro.*) I don't see why we
// would want to it like this.
//
// UPDATE: While using the associated executor of the handler for intermediate asynchronous
//         operations works, it may not be good practice to not have an explicit executor for
//         the operation in the first place. Usually, with I/O objects, there is an executor
//         that the async operation can use.
//
class ComposedExecutor : public testing::Test
{
public:
   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   auto async_sleep(boost::asio::any_io_executor ex, Duration duration, CompletionToken&& token)
   {
      return asio::async_initiate<CompletionToken, Sleep>(
         asio::co_composed<Sleep>(
            [this](auto state, boost::asio::any_io_executor ex, Duration duration) -> void
      {
         asio::steady_timer timer(ex, duration);
         timer.expires_after(100ms);
         std::println("waiting (with ex)...");
         auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::deferred));
         std::println("waiting (with ex)... done, {}", ec.what());
         ++done;
         co_return {boost::system::error_code{}};
      }),
         token, ex, duration);
   }

   size_t done = 0;
};

// -------------------------------------------------------------------------------------------------

TEST_F(ComposedExecutor, AnyDetached)
{
   boost::asio::io_context context;
   async_sleep(context.get_executor(), 100ms, asio::detached);
   async_sleep(context.get_executor(), 100ms, asio::detached);
   ::run(context);
   EXPECT_EQ(done, 2);
}

TEST_F(ComposedExecutor, AnyFuture)
{
   boost::asio::io_context context;
   auto f1 = async_sleep(context.get_executor(), 100ms, asio::use_future);
   auto f2 = async_sleep(context.get_executor(), 100ms, asio::use_future);
   ::run(context);
   EXPECT_NO_THROW(f1.get());
   EXPECT_NO_THROW(f2.get());
   EXPECT_EQ(done, 2);
}

// =================================================================================================

class ComposedHandler : public testing::Test
{
public:
   using WorkGuard = boost::asio::executor_work_guard<asio::any_completion_executor>;
   std::optional<WorkGuard> work;
   SleepHandler handler;

   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   auto wait_for_handler(CompletionToken&& token)
   {
      return asio::async_initiate<CompletionToken, Sleep>([this](SleepHandler handler) -> void
      {
         // this works only if we associate our executor with the handler before
         // but I guess that makes sense... how else would it know about the executor?
         auto ex = boost::asio::get_associated_executor(handler);
         this->handler = std::move(handler);
      }, token);
   }

   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   auto wait_for_handler_with_work(CompletionToken&& token)
   {
      return asio::async_initiate<CompletionToken, Sleep>([this](SleepHandler handler) -> void
      {
         auto ex = boost::asio::get_associated_executor(handler);
         this->work.emplace(ex);
         this->handler = std::move(handler);
      }, token);
   }

   void complete(boost::system::error_code ec = {})
   {
      auto ex = boost::asio::get_associated_executor(handler);
      dispatch(ex, [handler = std::move(handler), ec]() mutable { std::move(handler)(ec); });
   }
};

// -------------------------------------------------------------------------------------------------

TEST_F(ComposedHandler, Coma)
{
   boost::asio::io_context context;
   wait_for_handler(asio::bind_executor(context.get_executor(), asio::detached));
   EXPECT_EQ(context.run_for(100ms), 0);
   complete();
   EXPECT_EQ(::run(context), 0);
}

TEST_F(ComposedHandler, ComaPoll)
{
   boost::asio::io_context context;
   wait_for_handler_with_work(asio::bind_executor(context.get_executor(), detached));
   EXPECT_EQ(context.run_for(100ms), 0);
   complete();
   work.reset();
   EXPECT_EQ(::run(context), 1);
}

// =================================================================================================

TEST(Threads, WHEN_posting_between_contexts_THEN_execution_switches_threads)
{
   std::array<io_context, 2> context;

   auto work = make_work_guard(context[1]);
   auto thread = std::thread([&]() { context[1].run(); });

   const auto this_thread_id = std::this_thread::get_id();
   const auto other_thread_id = thread.get_id();

   co_spawn(context[0], [&]() -> awaitable<void>
   {
      EXPECT_EQ(std::this_thread::get_id(), this_thread_id);

      co_await dispatch(bind_executor(context[1]));
      EXPECT_EQ(std::this_thread::get_id(), other_thread_id);

      co_await dispatch(deferred);
      EXPECT_EQ(std::this_thread::get_id(), this_thread_id);
   }, detached); // don't invoke lambda here and let co_spawn do that -- keeps the closure alive

   context[0].run();

   work.reset();
   thread.join();
}

// -------------------------------------------------------------------------------------------------

//
// This test had address sanitizer errors, until the ()) invoking the lambda passed to co_spawn
// was removed. This is CP.51, see testcases
//
//   WHEN_spawn_lambda_awaitable_THEN_closure_is_deleted vs.
//   WHEN_spawn_lambda_THEN_closure_is_kept_alive.
//
// Note that using strands like this, when a lot of synchronization is required (here, for each
// single increment), is very inefficient. Even the atomic<size_t> approach is one magnitude faster.
//
TEST(Threads, Strand)
{
   io_context context;
   any_io_executor executor = context.get_executor();
   auto work = make_work_guard(executor);

   std::array<std::thread, 10> threads;
   for (auto& thread : threads)
      thread = std::thread([&]() { context.run(); });

#if 1
   auto strand = make_strand(executor);
   size_t counter = 0;
#else
   auto strand = executor;
   std::atomic<size_t> counter = 0;
#endif

   constexpr size_t N = 100, C = 100;
   for (size_t i = 0; i < N; ++i)
      co_spawn(executor, [&, i]() -> awaitable<void>
      {
         for (size_t u = 0; u < C; ++u)
         {
            co_await post(executor, bind_executor(strand));
            // std::println("{} {} {}", i, u, std::this_thread::get_id());
            counter++;
         }
      }, detached); // don't invoke lambda here and let co_spawn do that -- keeps the closure alive

   work.reset();

   for (auto& thread : threads)
      thread.join();

   EXPECT_EQ(counter, N * C);
}

// =================================================================================================

class CustomCancellationSlot : public testing::Test
{
private:
   boost::asio::io_context context;

protected:
   boost::asio::any_io_executor executor{context.get_executor()};
   using executor_type = boost::asio::any_io_executor;
   executor_type get_executor() const noexcept { return executor; }

   void TearDown() override { ::run(context); }

   auto token()
   {
      return [this](const std::exception_ptr& ep) { on_complete(code(ep)); };
   }

   MOCK_METHOD(void, on_complete, (boost::system::error_code ec), ());
   static constexpr auto Success = boost::system::error_code{};

protected:
   asio::steady_timer timer{executor};
   SleepHandler handler;
   void async_sleep_impl(SleepHandler handler_, Duration duration)
   {
      this->handler = std::move(handler_);
      auto slot = get_associated_cancellation_slot(handler);
      if (slot.is_connected() && !slot.has_handler())
      {
         slot.assign([this](asio::cancellation_type_t ct) mutable { //
            std::println("async_sleep: \x1b[1;31m{}\x1b[0m ({})", "cancelled", ct);
            auto exec = get_associated_executor(this->handler);
            post(executor, [h = std::move(this->handler)] mutable { //
               std::move(h)(errc::make_error_code(errc::operation_not_supported));
            });;
            timer.cancel();
         });
      }

      timer.expires_after(duration);
      timer.async_wait([this](error_code ec) { //
         if (handler)
            std::move(handler)(ec);
      });
   }

public:
   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken = DefaultCompletionToken>
   auto async_sleep(Duration duration, CompletionToken&& token = CompletionToken())
   {
      auto executor = boost::asio::get_associated_executor(token, get_executor());
      return boost::asio::async_initiate<CompletionToken, Sleep>(
         bind_executor(executor, [&](SleepHandler&& handler, Duration duration) { //
            async_sleep_impl(std::move(handler), duration);
         }),
         token, duration);
   }
};

// -------------------------------------------------------------------------------------------------

TEST_F(CustomCancellationSlot, WHEN_async_op_is_cancelled_THEN_completes_with_operation_cancelled)
{
   co_spawn(executor, [this]() -> awaitable<void>
   {
      auto ex = co_await this_coro::executor;
      auto [ec] = co_await async_sleep(100ms, cancel_after(20ms, as_tuple));
      std::println("async_sleep: {}", what(ec));
      EXPECT_EQ(ec, errc::operation_not_supported);
   }, token());

   EXPECT_CALL(*this, on_complete(Success));
}

// =================================================================================================

class MinimalAsyncFixture : public testing::Test
{
   boost::asio::io_context context;

protected:
   boost::asio::any_io_executor executor{context.get_executor()};

   std::function<awaitable<void>()> test;
   void TearDown() override
   {
      co_spawn(executor, std::move(test)(), [this](auto&& ep) { on_complete(code(ep)); });
      ::run(context);
   }
   MOCK_METHOD(void, on_complete, (boost::system::error_code ec), ());
};

// -------------------------------------------------------------------------------------------------

TEST_F(MinimalAsyncFixture, WHEN_coroutine_finishes_THEN_completes_with_success)
{
   test = [] -> awaitable<void>
   {
      co_await sleep(1ms);
      std::println("slept for a short time");
   };

   EXPECT_CALL(*this, on_complete(boost::system::error_code{}));
}

// =================================================================================================
