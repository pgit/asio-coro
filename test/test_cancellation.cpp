#include "asio-coro.hpp"
#include "process_base.hpp"

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <print>

using namespace boost::asio;
using namespace experimental;

namespace bp = boost::process::v2;
using namespace ::testing;

// =================================================================================================

class Cancellation : public ProcessBase, public testing::Test
{
protected:
   void SetUp() override { EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1)); }
   void TearDown() override { run(); }

   using ProcessBase::run;

   /// Execute process \p path with given \p args, then runs the awaitable #test.
   awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args)
   {
      std::println("execute: {} {}", path.generic_string(), join(args, " "));

      //
      // By default, a coroutine reacts to 'terminal' cancellation only. But async_execute()
      // supports the other cancellation types ('partial' and 'total') as well, translating them
      // to different types of signals being sent to the child process.
      //
      // We enable support for 'total' cancellation here, which includes 'partial' and 'terminal'.
      //
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());

      //
      // Start the external process and pass both the STDOUT pipe and the process to the testcase.
      //
      auto executor = co_await this_coro::executor;
      readable_pipe out(executor);
      bp::process child(executor, path, args, bp::process_stdio{.out = out});

      //
      // Finally, run the test payload.
      //
      co_return co_await test(std::move(out), std::move(child));
   }

   /// Returns an awaitable<int> of a coroutine executing a test ping.
   awaitable<int> ping() { return execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}); }

   /// The test payload, awaited by execute() after the process has been started.
   std::function<awaitable<int>(readable_pipe out, bp::process child)> test;
};

// =================================================================================================

//
// PING ::1(::1) 56 data bytes
// 64 bytes from ::1: icmp_seq=1 ttl=64 time=0.019 ms
// 64 bytes from ::1: icmp_seq=2 ttl=64 time=0.083 ms
// 64 bytes from ::1: icmp_seq=3 ttl=64 time=0.058 ms
// 64 bytes from ::1: icmp_seq=4 ttl=64 time=0.074 ms
// 64 bytes from ::1: icmp_seq=5 ttl=64 time=0.082 ms
//
// --- ::1 ping statistics ---
// 5 packets transmitted, 5 received, 0% packet loss, time 414ms
// rtt min/avg/max/mdev = 0.019/0.063/0.083/0.023 ms
//
TEST_F(Cancellation, Ping)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      std::println("execute: communicating...");
      co_await log(out);
      std::println("execute: communicating... done");

      std::println("execute: waiting for process...");
      auto exit_code = co_await child.async_wait();
      std::println("execute: waiting for process... done, exit code {}", exit_code);
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt")));
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), token());
}

//
// Equivalent, but using co_spawn() to turn the coroutine into an asynchronous operation.
// The completion token is the default 'deferred' token, which is awaitable, too.
//
// This example is for exposition only. There is no real reason to use co_spawn() here with the
// `deferred` completion token co_await, because we can await log() directly.
//
TEST_F(Cancellation, PingSpawned)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      co_await co_spawn(executor, log(out));
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt")));
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), token());
}

// -------------------------------------------------------------------------------------------------

//
// Next, we start cancelling the ping. We use a timer with an old-fashioned lambda completion
// handler. Cancellation is done through the .cancel() operation on the IO object.
//
TEST_F(Cancellation, CancelPipe)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      steady_timer timer(executor);
      timer.expires_after(150ms);
      timer.async_wait(
         [&](error_code ec)
         {
            if (!ec)
               out.cancel(); // cancel any operation on this IO object
         });

      // co_await co_spawn(executor, log(out));
      co_await log(out);
      co_return co_await child.async_wait(); // will not be executed on timeout
   };

   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), token());
}

//
// The more flexible, modern approach is to use per-operation cancellation instead of per-object
// cancellation. For this, you can bind a 'cancellation slot' to a completion token. This is similar
// to binding an executor or allocator to a token.
//
// Then, when you want to cancel an operation, you can 'emit' a cancellation signal.
//
TEST_F(Cancellation, CancellationSlot)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      cancellation_signal signal;
      steady_timer timer(executor);
      timer.expires_after(150ms);
      timer.async_wait(
         [&](error_code ec)
         {
            if (!ec)
               signal.emit(cancellation_type::terminal);
         });

      co_await co_spawn(executor, log(out), bind_cancellation_slot(signal.slot()));
      co_return co_await async_execute(std::move(child)); // will not be executed on timeout
   };

   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), token());
}

//
// Using cancellation slots manually is verbose, so there is a completion token adapter
// called `cancel_after` that does the grunt work of creating the timer four you.
//
TEST_F(Cancellation, CancelAfter)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      co_await co_spawn(executor, log(out), cancel_after(150ms));
      co_return co_await async_execute(std::move(child)); // will not be executed on timeout
   };

   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), token());
}

//
// We can also apply the timeout to the top level coroutine. it will be forwarded to the innermost
// currently active coroutine and any asynchronous operations within.
//
TEST_F(Cancellation, CancelFixture)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      co_await co_spawn(executor, log(out));
      co_return co_await async_execute(std::move(child)); // will not be executed on timeout
   };

   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Until now, the cancellation request always reaches the log() coroutine. What if we want to
// exercise more control over the cancellation process, for example to do some (possibly
// asynchronous) cleanup?
//
// Try to catch the 'operation_cancelled' exception raised from running log().
// If we do that, the coroutine continues, but the error will be re-thrown on the next co_await.
//
TEST_F(Cancellation, WHEN_exception_from_log_is_caught_THEN_rethrows_on_next_await)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      try
      {
         co_await log(out);
      }
      catch (const system_error& ec)
      {
         std::println("log completed with error: {}", what(ec.code()));
      }
      auto exit_code = co_await child.async_wait(); // throws if there was a timeout before
      ADD_FAILURE();
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

//
// Same as above, but with as_tuple instead of catch().
//
TEST_F(Cancellation, WHEN_log_returns_error_as_tuple_THEN_rethrows_on_next_await)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto [ec] = co_await co_spawn(executor, log(out), as_tuple);
      std::println("log completed with: {}", what(ec));
      auto exit_code = co_await child.async_wait(); // throws if there was a timeout before
      ADD_FAILURE();
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// -------------------------------------------------------------------------------------------------

//
// To fix this, we can reset the cancellation state before the next co_await.
//
TEST_F(Cancellation, WHEN_cancellation_state_is_reset_THEN_does_not_throw_on_next_await)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto ec = co_await co_spawn(executor, log(out), as_tuple);
      EXPECT_TRUE((co_await this_coro::cancellation_state).cancelled() != cancellation_type::none);
      co_await this_coro::reset_cancellation_state();
      EXPECT_FALSE((co_await this_coro::cancellation_state).cancelled() != cancellation_type::none);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

//
// Alternatively, the automatic throwing when cancelled can be disabled.
// Unlike reset_cancellation_state() above, this doesn't reset the state.
//
TEST_F(Cancellation, WHEN_throw_if_cancelled_is_set_to_false_THEN_does_not_throw_on_next_await)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto ec = co_await co_spawn(executor, log(out), as_tuple);
      co_await this_coro::throw_if_cancelled(false);
      EXPECT_TRUE((co_await this_coro::cancellation_state).cancelled() != cancellation_type::none);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Resetting the cancellation state also allows us to re-start the log() coroutine.
//
TEST_F(Cancellation, WHEN_log_is_resumed_after_cancellation_THEN_ping_completes)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      co_await co_spawn(executor, log(out), as_tuple);
      co_await this_coro::reset_cancellation_state();
      co_await co_spawn(executor, log(out));
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// =================================================================================================

//
// The default cancellation type is 'terminal', which is the strongest and can leave the
// operation in a state that cannot be resumed. What this means for each specific operation
// is very different.
//
// bp::async_execute() maps the cancellation types to the following operations on the process:
//
//  cancellation_type  results in      equivalent to sending
// ==========================================================
//   terminal          terminate()     SIGKILL
//   partial           request_exit()  SIGTERM
//   total             interrupt()     SIGINT
//
TEST_F(Cancellation, DISABLED_WHEN_log_is_detached_THEN_continues_reading_from_pipe)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      co_spawn(executor, log(out), detached);
      co_return co_await async_execute(std::move(child));
   };
   EXPECT_CALL(*this, on_log(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// std::promise has two very interesting properties:
// 1) Similar to the 'detached' completion token, the operation is started eagerly.
// 2) When the promise is destroyed, the operation is cancelled.
//
// This means tht we can use it to help with 'structured concurrency'.
//
// Caveat: Cancelling the operation is not equal to waiting for it's completion: If the
//         async operation catches the cancellation errors and decides to continue anyway,
//         that will happen later, after the parent frame has been destroyed.
//
TEST_F(Cancellation, WHEN_log_uses_promise_THEN_is_started_immediately)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto promise = co_spawn(executor, log(out), use_promise);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_log(HasSubstr("PING"))).Times(1);

   //
   // We can't expect "rtt" here -- even though the ping will complete gracefully, there is a
   // race condition in the code above, between reading all of the log output and the log()
   // coroutine being cancelled.
   //
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// This test is similar to the one before, but destroys the promise earlier, leading to
// cancellation of the log() coroutine.
//
TEST_F(Cancellation, WHEN_log_uses_promise_THEN_is_cancelled_on_destruction)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      {
         auto promise = co_spawn(executor, log(out), use_promise);
         co_await sleep(50ms);
      }
      EXPECT_CALL(*this, on_log(_)).Times(0);
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// If we actually wait on the promise, finally, we have what we want:
// 1) The process is cancelled using SIGINT
// 2) We can read the remaining output
// 3) Exit is handled gracefully
//
TEST_F(Cancellation, WHEN_promise_is_awaited_THEN_output_is_complete)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto promise = co_spawn(executor, log(out), use_promise);
      auto [ec, rc] = co_await async_execute(std::move(child), as_tuple);
      co_await this_coro::reset_cancellation_state();
      co_await std::move(promise);
      co_return rc;
   };

   EXPECT_CALL(*this, on_log(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

TEST_F(Cancellation, WHEN_parallel_group_waits_for_all_THEN_output_is_complete)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto [order, ep, exit_code, ex] =
         co_await make_parallel_group(async_execute(std::move(child)), co_spawn(executor, log(out)))
            .async_wait(wait_for_all(), deferred);
      std::println("order=[{}] ep={} ex={}", order, what(ep), what(ex));
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_log(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

TEST_F(Cancellation, WHEN_parallel_group_waits_for_one_error_THEN_output_is_complete)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto [order, ep, exit_code, ex] =
         co_await make_parallel_group(async_execute(std::move(child)),
                                      co_spawn(executor, log(out))) //
            .async_wait(wait_for_one_error(), deferred);
      std::println("order=[{}] ep={} ex={}", order, what(ep), what(ex));
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_log(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

TEST_F(Cancellation, WHEN_parallel_group_operator_and_THEN_output_is_complete)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      auto cs = co_await this_coro::cancellation_state;
#if 1
      cancellation_signal term;
      cancellation_signal signal;
      cs.slot().assign(
         [&](auto type)
         {
            std::println("CANCELLED: {}", type);
            signal.emit(cancellation_type::total);
            term.emit(cancellation_type::total);
         });
#endif
      std::println("CANCELLED: {} (before operation)", cs.cancelled());
      // co_await async_execute(std::move(child)); // ok
      // auto status = co_await async_execute(std::move(child), use_awaitable); // ok
      // co_return co_await async_execute(std::move(child), bind_cancellation_slot(signal.slot(),
      // use_awaitable)); co_return co_await (
      //    async_execute(std::move(child), bind_cancellation_slot(signal.slot(), use_awaitable)) &&
      //    co_spawn(executor, log(out), use_awaitable));
      // co_await co_spawn(executor,
      // boost::asio::experimental::awaitable_operators::detail::awaitable_wrap(async_execute(std::move(child),
      // use_awaitable)));
      auto awaitable =
         (async_execute(std::move(child), bind_cancellation_slot(term.slot(), use_awaitable)) &&
          log("STDOUT", out));
      auto status =
         co_await co_spawn(executor, std::move(awaitable), bind_cancellation_slot(signal.slot()));
      std::println("CANCELLED: {}", cs.cancelled());
      co_return status;
   };

   EXPECT_CALL(*this, on_log(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
   // co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}),
   //          cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Cancellation type 'partial': child.request_exit() (SIGTERM)
//
TEST_F(Cancellation, WHEN_child_is_terminated_THEN_exits_with_sigterm)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto promise = co_spawn(executor, log(out), use_promise);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(SIGTERM));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::partial, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Sending a SIGKILL should always result in immediate termination of the process.
//
TEST_F(Cancellation, WHEN_child_is_killed_THEN_exits_with_sigkill)
{
   test = [&](readable_pipe out, bp::process child) -> awaitable<int>
   {
      auto promise = co_spawn(executor, log(out), use_promise);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(0);
#if BOOST_VERSION >= 108900
   EXPECT_CALL(*this, on_exit(9));
#else
   // https://github.com/boostorg/process/issues/496
   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::no_child_process)));
#endif
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// =================================================================================================
