#include "asio-coro.hpp"
#include "utils.hpp"

#include <gtest/gtest.h>

#include <print>
#include <thread>

using namespace std::chrono_literals;
using namespace boost::asio::experimental::awaitable_operators;

// =================================================================================================

using Sleep = void(boost::system::error_code);
using SleepHandler = asio::any_completion_handler<Sleep>;

using Duration = std::chrono::nanoseconds;

// =================================================================================================

class Asio : public testing::Test
{
};

// -------------------------------------------------------------------------------------------------

TEST_F(Asio, WHEN_task_is_spawned_THEN_work_is_tracked)
{
   boost::asio::io_context context;
   bool ok = false;
   co_spawn(
      context.get_executor(),
      [&]() -> asio::awaitable<void>
      {
         co_await sleep(100ms);
         ok = true;
         co_return;
      },
      asio::detached);
   ::run(context);
   EXPECT_TRUE(ok);
}

TEST_F(Asio, WHEN_task_is_finished_THEN_sets_future)
{
   boost::asio::io_context context;
   auto future = co_spawn(
      context.get_executor(),
      [&]() -> asio::awaitable<bool>
      {
         co_await sleep(100ms);
         co_return true;
      },
      asio::use_future);
   ::run(context);
   EXPECT_TRUE(future.get());
}

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
   static inline auto async_sleep(boost::asio::any_io_executor ex, Duration duration,
                                  CompletionToken&& token)
   {
      return boost::asio::async_initiate<CompletionToken, Sleep>(async_sleep_impl, token,
                                                                 std::move(ex), duration);
   }
};

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
         },
         token, std::move(ex), duration);
   }

public:
   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   auto async_sleep(boost::asio::any_io_executor ex, Duration duration, CompletionToken&& token)
   {
      return boost::asio::async_initiate<CompletionToken, Sleep>(
         [this](SleepHandler handler, boost::asio::any_io_executor ex, Duration duration)
         {
            async_sleep_impl(std::move(handler), std::move(ex), duration); //
         },
         std::forward<CompletionToken>(token), std::move(ex), duration);
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
 * Same as before, but this works only if BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT is defined.
 *
 * Attempting to get the associated executor from the completion token and using that for
 * intermediate async operations is not a good idea!
 */
class DropHandler : public testing::Test
{
public:
   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   static auto drop_handler(CompletionToken&& token)
   {
      return boost::asio::async_initiate<CompletionToken, Sleep>(
         [](SleepHandler handler)
         {
            std::ignore = handler; // what happens if we delete the handler without calling it?
         },
         token);
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

   co_spawn(
      context.get_executor(),
      [&]() -> asio::awaitable<void>
      {
         Class object1;
         co_await drop_handler(asio::deferred);
         Class object2;
      },
      [](const std::exception_ptr& ex)
      {
         if (ex)
            std::println("TCP accept loop: {}", what(ex));
         else
            std::println("TCP accept loop: done");
      });
   ::run(context);
}

TEST_F(DropHandler, DropHandlerFuture)
{
   boost::asio::io_context context;

   auto f = co_spawn(
      context.get_executor(),
      [&]() -> asio::awaitable<void>
      { //
         std::println("dropping handler...");
         co_await drop_handler(asio::deferred);
         std::println("dropping handler... done");
      },
      asio::use_future);
   ::run(context);
   EXPECT_THROW(f.get(), std::future_error);
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
               co_await asio::steady_timer(ex, duration).async_wait(asio::deferred);
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
// a use-after-free.
//
TEST_F(ComposedCoro, DISABLED_Unbound)
{
   boost::asio::io_context context;
   async_sleep(100ms, asio::detached);
   ::run(context);
   EXPECT_EQ(done, 1);
}

//
// This test is only (somewhat) safe because there is a longer sleep in the test that follows.
// If you run it under very high load, it may still fail, very seldomly.
//
// TSAN also reports this, and fails because of TSAN_OPTIONS "halt_on_error=1", so it disabled.
//
TEST_F(ComposedCoro, DISABLED_DefaultExecutor)
{
   boost::asio::io_context context;
   async_sleep(100ms, asio::detached); // this not safe, does not register work with 'context'
   async_sleep(120ms, bind_executor(context, asio::detached));
   ::run(context);
   EXPECT_EQ(done, 2);
}

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
      return asio::async_initiate<CompletionToken, Sleep>(
         [this](SleepHandler handler) -> void
         {
            // this works only if we associate our executor with the handler before
            // but I guess that makes sense... how else would it know about the executor?
            auto ex = boost::asio::get_associated_executor(handler);
            this->handler = std::move(handler);
         },
         token);
   }

   template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
   auto wait_for_handler_with_work(CompletionToken&& token)
   {
      return asio::async_initiate<CompletionToken, Sleep>(
         [this](SleepHandler handler) -> void
         {
            auto ex = boost::asio::get_associated_executor(handler);
            this->work.emplace(ex);
            this->handler = std::move(handler);
         },
         token);
   }
};

// -------------------------------------------------------------------------------------------------

TEST_F(ComposedHandler, Coma)
{
   boost::asio::io_context context;
   wait_for_handler(asio::bind_executor(context.get_executor(), asio::detached));
   EXPECT_EQ(context.run_for(100ms), 0);
   dispatch(context.get_executor(), [handler = std::move(handler)]() mutable
            { std::move(handler)(boost::system::error_code{}); });
   EXPECT_EQ(::run(context), 0);
}

TEST_F(ComposedHandler, ComaPoll)
{
   boost::asio::io_context context;
   wait_for_handler_with_work(asio::bind_executor(context.get_executor(), detached));
   EXPECT_EQ(context.run_for(100ms), 0);
   dispatch(context.get_executor(), [handler = std::move(handler)]() mutable
            { std::move(handler)(boost::system::error_code{}); });
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

   co_spawn(
      context[0],
      [&]() -> awaitable<void>
      {
         EXPECT_EQ(std::this_thread::get_id(), this_thread_id);

         co_await post(context[1], bind_executor(context[1], use_awaitable));
         EXPECT_EQ(std::this_thread::get_id(), other_thread_id);

         co_await post(context[0], bind_executor(context[0], use_awaitable));
         EXPECT_EQ(std::this_thread::get_id(), this_thread_id);
      },
      detached);

   context[0].run();

   work.reset();
   thread.join();
}

// -------------------------------------------------------------------------------------------------

//
// FIXME: This has address sanitizer errors.
//
TEST(Threads, DISABLED_Strand)
{
   io_context context;
   any_io_executor executor = context.get_executor();
   auto work = make_work_guard(executor);
   auto strand = make_strand(executor);

   std::array<std::thread, 10> threads;
   for (auto& thread : threads)
      thread = std::thread([&]() { context.run(); });

   size_t counter = 0;
   constexpr size_t N = 100;
   for (size_t i = 0; i < N; ++i)
      co_spawn(
         executor,
         [&]() -> awaitable<void>
         {
            co_await post(executor, bind_executor(strand));
            counter++;
         }(),
         detached);

   work.reset();

   for (auto& thread : threads)
      thread.join();

   EXPECT_EQ(counter, N);
}

// =================================================================================================
