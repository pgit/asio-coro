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
   void SetUp() override { EXPECT_CALL(*this, on_stdout(_)).Times(AtLeast(1)); }
   void TearDown() override { runDebug(); } // or runDebug()

   using ProcessBase::run;

   /// Execute process \p path with given \p args, then runs the awaitable #test.
   awaitable<ExitCode> execute(std::filesystem::path path, std::vector<std::string> args)
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
      readable_pipe out{executor};
      if (testWithErr)
      {
         readable_pipe err{executor};
         bp::process child(executor, path, args, bp::process_stdio{.out = out, .err = err});
         co_return co_await testWithErr(std::move(out), std::move(err), std::move(child));
      }
      else
      {
         bp::process child(executor, path, args, bp::process_stdio{.out = out});
         co_return co_await test(std::move(out), std::move(child));
      }
   }

   /// Returns an awaitable<ExitCode> of a coroutine executing a test ping.
   awaitable<ExitCode> ping() { return execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}); }

   /// The test payload, awaited by execute() after the process has been started.
   std::function<awaitable<ExitCode>(readable_pipe out, bp::process child)> test;
   std::function<awaitable<ExitCode>(readable_pipe out, readable_pipe err, bp::process child)>
      testWithErr;
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
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      std::println("execute: communicating...");
      co_await log_stdout(out);
      std::println("execute: communicating... done");

      std::println("execute: waiting for process...");
      auto exit_code = co_await child.async_wait();
      std::println("execute: waiting for process... done, exit code {}", exit_code);
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt")));
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
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      co_await co_spawn(executor, log_stdout(out));
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt")));
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), token());
}

// -------------------------------------------------------------------------------------------------

TEST_F(Cancellation, PingParallelGroup)
{
   testWithErr = [this](readable_pipe out, readable_pipe err,
                        bp::process child) -> awaitable<ExitCode>
   {
      co_await (log_stdout(out) && log_stderr(err));
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Next, we start cancelling the ping. We use a timer with an old-fashioned lambda completion
// handler. Cancellation is done through the .cancel() operation on the IO object.
//
TEST_F(Cancellation, WHEN_io_object_is_cancelled_THEN_exception_is_thrown)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      steady_timer timer(executor);
      timer.expires_after(150ms);
      timer.async_wait([&](error_code ec)
      {
         if (!ec)
            out.cancel(); // cancel any operation on this IO object
      });

      co_await log_stdout(out);
      ADD_FAILURE(); // will not be executed on timeout
      co_return 0;
   };

   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), token());
}

//
// IO object based cancellation operates is handled separately from per-operation cancellation.
// So, if we catch the cancellation error, after cancellation, the coroutine's cancellation state is
// still 'none'.
//
// This means that the coroutine can continue to run normally.
//
TEST_F(Cancellation, WHEN_io_object_is_cancelled_THEN_cancellation_state_is_none)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      steady_timer timer(executor);
      timer.expires_after(150ms);
      timer.async_wait([&](error_code) { out.cancel(); });

      co_await co_spawn(co_await this_coro::executor, log_stdout(out), as_tuple); // doesn't throw
      EXPECT_EQ((co_await this_coro::cancellation_state).cancelled(), cancellation_type::none);
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0)); // only the log output was cancelled, but not the process
   co_spawn(executor, ping(), token());
}

//
// Test to verify that the cancellation handler of log() actually prints the remaining buffer that
// is not yet terminated by a newline.
//
// PLEASE NOTE: This testcase actually suffers from the ASIO cancellation race:
//              https://www.youtube.com/watch?v=hHk5OXlKVFg&t=484s
//
// The request for cancellation (out.cancel()) may be ignored by the the readable pipe iff there is
// a pending asynchronous operation that is already scheduled for completion! That operation will
// still complete successfully and the request for cancellation is ignored.
//
// The only reason this testcase runs stable is that with the timeout of 150ms, we aimed right
// between two "pings" so it is very unlikely that there is something to read from the pipe at
// time of cancellation.
//
TEST_F(Cancellation, WHEN_io_object_is_cancelled_THEN_remaining_buffer_is_printed)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      steady_timer timer(executor);
      timer.expires_after(150ms);
      timer.async_wait([&](error_code ec)
      {
         if (!ec)
            out.cancel(); // cancel any operation on this IO object
      });

      co_await log_stdout(out);
      ADD_FAILURE(); // will not be executed on timeout
      co_return 0;
   };

   EXPECT_CALL(*this, on_stdout("No newline"));
   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, execute("/usr/bin/bash", {"-c", "echo -ne 'First\nNo newline'; sleep 1"}),
            token());
}

// -------------------------------------------------------------------------------------------------

//
// Only if we actually close() the pipe, cancellation is reliable. If an asynchronous operation is
// already scheduled for completion when closing the pipe, that operation still completes
// successfully. But on the next attempt to read something we get 'bad_file_descriptor'.
//
TEST_F(Cancellation, WHEN_io_object_is_closed_THEN_log_function_is_cancelled_reliably)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      steady_timer timer(executor);
      timer.expires_after(150ms);
      timer.async_wait([&](error_code ec)
      {
         if (!ec)
            out.close();
      });

      co_await log_stdout(out);
      ADD_FAILURE(); // will not be executed on timeout
      co_return 0;
   };

   EXPECT_CALL(*this, on_error(_)); // can be either operation_canceled or bad_file_descriptor
   co_spawn(executor, execute("/usr/bin/bash", {"-c", "while date; do true; done"}), token());
}

// -------------------------------------------------------------------------------------------------

//
// The more flexible, modern approach is to use per-operation cancellation instead of per-object
// cancellation. For this, you can bind a 'cancellation slot' to a completion token. This is similar
// to binding an executor or allocator to a token.
//
// Then, when you want to cancel an operation, you can 'emit' a cancellation signal.
//
TEST_F(Cancellation, CancellationSlot)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      cancellation_signal signal;
      steady_timer timer(executor);
      timer.expires_after(150ms);
      timer.async_wait([&](error_code ec)
      {
         if (!ec)
            signal.emit(cancellation_type::terminal);
      });

      co_await co_spawn(executor, log_stdout(out), bind_cancellation_slot(signal.slot()));
      ADD_FAILURE();
      co_return co_await child.async_wait(); // will not be executed on timeout
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
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      co_await co_spawn(executor, log_stdout(out), cancel_after(150ms));
      ADD_FAILURE();
      co_return co_await child.async_wait(); // will not be executed on timeout
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
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      co_await log_stdout(out);
      co_return co_await child.async_wait(); // will not be executed on timeout
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
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      try
      {
         co_await log_stdout(out);
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
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto [ec] = co_await co_spawn(executor, log_stdout(out), as_tuple);
      std::println("log completed with: {}", what(ec));
      auto exit_code = co_await child.async_wait(); // throws if there was a timeout before
      ADD_FAILURE();
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// -------------------------------------------------------------------------------------------------

//
// To fix this, we can reset the cancellation state before the next co_await.
//
TEST_F(Cancellation, WHEN_cancellation_state_is_reset_THEN_does_not_throw_on_next_await)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto [ec] = co_await co_spawn(executor, log_stdout(out), as_tuple);
      EXPECT_TRUE((co_await this_coro::cancellation_state).cancelled() != cancellation_type::none);
      co_await this_coro::reset_cancellation_state();
      EXPECT_FALSE((co_await this_coro::cancellation_state).cancelled() != cancellation_type::none);
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

//
// Alternatively, the automatic throwing when cancelled can be disabled.
// Unlike reset_cancellation_state() above, this doesn't reset the state.
//
TEST_F(Cancellation, WHEN_throw_if_cancelled_is_set_to_false_THEN_does_not_throw_on_next_await)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto [ec] = co_await co_spawn(executor, log_stdout(out), as_tuple);
      co_await this_coro::throw_if_cancelled(false);
      EXPECT_TRUE((co_await this_coro::cancellation_state).cancelled() != cancellation_type::none);
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Resetting the cancellation state also allows us to re-start the log() coroutine.
//
TEST_F(Cancellation, WHEN_log_is_resumed_after_cancellation_THEN_ping_completes)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      co_await co_spawn(executor, log_stdout(out), as_tuple);
      co_await this_coro::reset_cancellation_state();
      child.interrupt();
      co_await co_spawn(executor, log_stdout(out));
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(1);
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

//
// If we want cancellation to arrive at async_execute() instead of log(), we could try to
// spawn the logging into the background...
//
TEST_F(Cancellation, DISABLED_WHEN_log_is_detached_THEN_continues_reading_from_pipe) // UB
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      co_spawn(executor, log_stdout(out), detached);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// The use_promise completion token has two very interesting properties:
// 1) Similar to the 'detached' completion token, the operation is started eagerly.
// 2) When the promise is destroyed, the operation is cancelled.
//
// This means tht we can use it to help with 'structured concurrency'.
//
// Caveat: Cancelling the operation is not equal to waiting for it's completion: If the
//         async operation catches the cancellation errors and decides to continue anyway,
//         that will happen later, after the parent frame has been destroyed.
//
//         Because of this, it is good practice to react to cancellation immediately.
//         For async shutdown, non-terminal cancellation types should be used.
//
//         There is no such thing as an "async destructor".
//
TEST_F(Cancellation, WHEN_log_uses_promise_THEN_is_started_immediately)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto promise = co_spawn(executor, log_stdout(out), use_promise);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("PING"))).Times(1);
   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(Between(0, 1));

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
// This test is similar to the one before, but destroys the promise earlier, after a sleep().
// This leads to cancellation of the log() coroutine.
//
TEST_F(Cancellation, WHEN_log_uses_promise_THEN_is_cancelled_on_destruction)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      {
         auto promise = co_spawn(executor, log_stdout(out), use_promise);
         co_await sleep(50ms);
         std::println("destroying promise...");
      }
      std::println("destroying promise... done");
      EXPECT_CALL(*this, on_stdout(_)).Times(Between(0, 1));
      co_return co_await child.async_wait();
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), token());
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
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      co_await this_coro::throw_if_cancelled(false);
      auto promise = co_spawn(executor, log_stdout(out), use_promise);
      auto [ec, exit_code] = co_await async_execute(std::move(child), as_tuple);
      co_await std::move(promise);
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// This does also what we want: The 'total' cancellation is delivered to the parallel group, which
// forwards it to both subtasks. Only async_execute() reacts to it, signalling SIGINT to the
// process and completing with the exit code eventually. The other operation in the parallel group,
// log(), does not react to the cancellation because it only supports 'terminal|partial'.
//
TEST_F(Cancellation, WHEN_parallel_group_is_cancelled_total_THEN_logging_continues)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto [order, ec, exit_code, ep] =
         co_await make_parallel_group( // on 'total' cancellation, ...
            async_execute(std::move(child)), // ... signals SIGINT and waits for process and ...
            co_spawn(executor, log_stdout(out)) // ... ignores the 'total' cancellation signal
            )
            .async_wait(wait_for_one_error(), deferred);
      std::println("order=[{}] ec={} ex={}", order, what(ec), what(ep));
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Awaitable wrapper for async_execute(). It should be possible to just use 'use_awaitable' as
// a completion token for async_execute instead. But doing so will return an awaitable that reacts
// to 'terminal' cancellation only!
//
// It's debatable whether or not this is a bug. ASIO documentation of co_spawn() mentions:
//
//    The new thread of execution is created with a cancellation state that supports
//    cancellation_type::terminal values only. To change the cancellation state, call
//    this_coro::reset_cancellation_state.
//
// https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/co_spawn/overload1.html
//
// This seems to be true for the co_spawn() that is used by operator&&() internally as well.
// We can work around this by introducing a wrapper that enables 'total' cancellation again.
//
static awaitable<ExitCode> async_execute_enable_total(bp::process&& child)
{
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());
   co_return co_await async_execute(std::move(child));
}

TEST_F(Cancellation, WHEN_parallel_group_operator_THEN_cancellation_fails)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto cs = co_await this_coro::cancellation_state;
      // auto awaitable = (async_execute(std::move(child), use_awaitable) && log(out)); // FAILS
      auto awaitable = (async_execute_enable_total(std::move(child)) && log_stdout(out));
      auto status = co_await (std::move(awaitable));
      std::println("CANCELLED: {}", cs.cancelled());
      co_return status;
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Here, we redirect the cancellation slot of the 'test' coroutine directly to async_execute(), and
// to async_execute() ONLY. That is easy to do, but we also have to make sure that the default
// forwarding behaviour is not applied to co_spawn() as well. So we bind an empty cancellation slot
// to the co_spawn() completion token instead:
//
TEST_F(Cancellation, WHEN_redirect_cancellation_slot_manually_THEN_output_is_complete)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto cs = co_await this_coro::cancellation_state;
      auto awaitable =
         log_stdout(out) &&
         async_execute(std::move(child), bind_cancellation_slot(cs.slot(), use_awaitable));
      auto status = co_await co_spawn(executor, std::move(awaitable),
                                      bind_cancellation_slot(cancellation_slot()));
      std::println("CANCELLED: {}", cs.cancelled());
      co_return status;
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Cancellation type 'partial': child.request_exit() (SIGTERM)
//
TEST_F(Cancellation, WHEN_child_is_terminated_THEN_exits_with_sigterm)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto promise = co_spawn(executor, log_stdout(out), use_promise);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(SIGTERM));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::partial, token()));
}

// -------------------------------------------------------------------------------------------------

//
// Sending a SIGKILL should always result in immediate termination of the process.
//
TEST_F(Cancellation, WHEN_child_is_killed_THEN_exits_with_sigkill)
{
   test = [this](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      auto promise = co_spawn(executor, log_stdout(out), use_promise);
      co_return co_await async_execute(std::move(child));
   };

   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt"))).Times(0);
#if BOOST_VERSION < 108900
   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::no_child_process)));
#else
   EXPECT_CALL(*this, on_exit(9)); // https://github.com/boostorg/process/issues/503
#endif
   co_spawn(executor, ping(), cancel_after(150ms, token()));
}

// =================================================================================================
