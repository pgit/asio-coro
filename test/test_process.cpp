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

namespace bp = boost::process::v2;
using namespace ::testing;

// =================================================================================================

class Process : public ProcessBase, public testing::Test
{
protected:
   //
   // Running the IO context as late as in the TearDown() function works fine if we set all
   // expectations before. We only have to be careful not to capture any local variables from
   // the testcase body.
   //
   // However, if a testcase really needs to do that, it can call run() manually.
   //
   void TearDown() override { run(); }

   awaitable<ExitCode> execute(std::filesystem::path path, std::vector<std::string> args)
   {
      std::println("execute: {} {}", path.generic_string(), join(args, " "));

      //
      // Create pipes for STDOUT and STDERR and start the child process.
      //
      auto executor = co_await this_coro::executor;
      readable_pipe out(executor), err(executor);
      bp::process child(executor, path, args, bp::process_stdio{.out = out, .err = err});

      //
      // We support all three types of cancellation, total, partial and terminal.
      // Note that total cancellation implies partial and terminal cancellation.
      //
      // We have to explicitly enable all three types of cancellation here, as there is no way
      // no way to deduce the set of supported cancellation types from the inner async operation
      // (bp::async_execute). The default is to support terminal cancellation only.
      //
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      auto cs = co_await this_coro::cancellation_state;

      //
      // Reading from the pipe is done as a background task, so it is not affected by the
      // cancellation signal immediately and we can read the remaining output. This is like
      // a parallel group that forwards cancellation only to one of the tasks.
      //
      // However, this introduces a problem with structured concurrency: The spawned thread of
      // execution may outlive this coroutine if we don't join it properly.
      //
      // Solution: experimental::use_promise.
      //
      auto group =
         experimental::make_parallel_group(co_spawn(executor, log("STDOUT", out), as_tuple),
                                           co_spawn(executor, log("STDERR", err), as_tuple));
      auto promise = group.async_wait(experimental::wait_for_all(), experimental::use_promise);

      //
      // Use async_execute() from boost::process. It supports all three cancellation types 'total',
      // 'partial' and 'terminal', and transforms them into sending SIGINT, SIGTERM and SIGKILL,
      // respectively.
      //
      auto [ec, exit_status] = co_await bp::async_execute(std::move(child), as_tuple);
      std::println("execute: finished, cancelled={}, rc={}", cs.cancelled(), exit_status);
      if ((cs.cancelled() & cancellation_type::terminal) != cancellation_type::none)
      {
#if BOOST_VERSION < 108900
         exit_status = SIGKILL; // work around https://github.com/boostorg/process/issues/503
#endif
         //
         // The pipes may still be open if any descendant of the child process has inherited the
         // writing ends. For example, this happens when running a sleep command inside bash.
         //
         // This is normal behaviour, we just have to decide how to handle it. One option, at least
         // for terminal cancellation, is to just close the reading end so that we don't block.
         //
         // Another option is to kill the whole process group (using the PGID), but that requires
         // 1) a custom initializer for calling setpgid(0, 0) and
         // 2) ::kill(-PGID) to kill the group instead of only the process.
         // See the custom process handling tests for an example of this.
         //
         error_code ignore;
         std::ignore = out.close(ignore);
         std::ignore = err.close(ignore);
      }

      co_await this_coro::reset_cancellation_state();
      std::println("execute: waiting for remaining output...");
      co_await std::move(promise);
      std::println("execute: waiting for remaining output... done");
      co_return exit_status;
   }
};

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_ping_is_started_THEN_completes_gracefully)
{
   co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}), token());
   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("rtt")));
   EXPECT_CALL(*this, on_exit(0));
}

TEST_F(Process, WHEN_multiple_pings_are_started_THEN_all_of_them_complete_gracefully)
{
   constexpr size_t N = 10;
   for (size_t i = 0; i < N; ++i)
      co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "2", "-i", "0.1"}), token());

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(N);
   EXPECT_CALL(*this, on_exit(0)).Times(N);
}

//
// Starting a partially interactive program like 'top' also works, but that looks a little weird.
// Also, it won't finish on it's own, so we have to cancel it after a timeout. For those reasons,
// this test is disabled.
//
// TODO: For real control over interactive programs, I guess we need a PTY.
//
TEST_F(Process, DISABLED_Top)
{
   co_spawn(executor, execute("/usr/bin/top", {}), cancel_after(10s, token()));
   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_exit(SIGKILL)); // 9
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_ping_is_cancelled_terminal_THEN_is_terminated)
{
   co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}),
            cancel_after(250ms, cancellation_type::terminal, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("PING")));
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(0);
   EXPECT_CALL(*this, on_exit(SIGKILL)); // 9
}

TEST_F(Process, WHEN_ping_is_cancelled_partial_THEN_exists_with_code_15)
{
   co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}),
            cancel_after(250ms, cancellation_type::partial, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_exit(SIGTERM)); // 15
}

TEST_F(Process, WHEN_ping_is_cancelled_total_THEN_exists_gracefully)
{
   co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}),
            cancel_after(250ms, cancellation_type::total, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
}

// -------------------------------------------------------------------------------------------------

//
// Usually it is easiest to rely on existing completion token adapters like cancel_after(), but
// it is also possible to use the underlying signal/slot mechanism directly. To assign a
// cancellation slot to a handler, use bind_cancellation_slot().
//
TEST_F(Process, WHEN_custom_cancellation_slot_is_emitted_THEN_process_is_cancelled)
{
   cancellation_signal signal;
   co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}),
            bind_cancellation_slot(signal.slot(), token()));

   steady_timer timer(executor);
   timer.expires_after(250ms);
   timer.async_wait([&](error_code) { signal.emit(cancellation_type::total); });

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));

   run(); // to make sure 'signal' stays in scope, we run the reactor here
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_sleep_is_cancelled_terminal_THEN_exits_with_code_9)
{
   co_spawn(executor, execute("/usr/bin/sleep", {"10"}), cancel_after(50ms, token()));
   EXPECT_CALL(*this, on_exit(SIGKILL));
}

TEST_F(Process, WHEN_sleep_is_cancelled_partial_THEN_exits_with_code_15)
{
   co_spawn(executor, execute("/usr/bin/sleep", {"10"}),
            cancel_after(50ms, cancellation_type::partial, token()));
   EXPECT_CALL(*this, on_exit(SIGTERM));
}

TEST_F(Process, WHEN_sleep_is_cancelled_total_THEN_exits_with_code_2)
{
   co_spawn(executor, execute("/usr/bin/sleep", {"10"}),
            cancel_after(50ms, cancellation_type::total, token()));
   EXPECT_CALL(*this, on_exit(SIGINT));
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_cancelled_terminal_THEN_exits_with_code_9)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/handle_signal", "-i1", "-t1"}),
            cancel_after(250ms, cancellation_type::terminal, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(0);
   EXPECT_CALL(*this, on_exit(SIGKILL));
}

TEST_F(Process, WHEN_cancelled_without_type_THEN_is_cancelled_terminal)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/handle_signal", "-i1", "-t1"}),
            cancel_after(250ms, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_exit(SIGKILL));
}

TEST_F(Process, WHEN_cancelled_partial_THEN_receives_sigterm_and_exits_gracefully)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/handle_signal", "-t0"}),
            cancel_after(250ms, cancellation_type::partial, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGTERM"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
}

TEST_F(Process, WHEN_cancelled_total_THEN_receives_sigint_and_exits_gracefully)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/handle_signal", "-i0"}),
            cancel_after(250ms, cancellation_type::total, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGINT"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
}

// -------------------------------------------------------------------------------------------------

//
// WARNING: This testcase leaves an orphaned "sleep 60"!
//
// This is because async_execute() doesn't set the PGID and kills by PID only.
// See 'test_process_custom.cpp' for an example of custom cancellation that does this correctly.
//
TEST_F(Process, DISABLED_WHEN_bash_is_killed_THEN_exits_with_code_9)
{
   co_spawn(executor,
            execute("/usr/bin/bash",
                    {"-c", "trap 'echo SIGNAL' SIGINT SIGTERM; echo WAITING; sleep 60; echo DONE"}),
            cancel_after(250ms, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("WAITING"))).Times(1);
   EXPECT_CALL(*this, on_exit(9));
}

TEST_F(Process, WHEN_bash_ignores_sigterm_THEN_sleep_finishes)
{
   co_spawn(executor,
            execute("/usr/bin/bash", {"-c", "trap 'echo SIGTERM' SIGTERM; sleep 1; echo DONE"}),
            cancel_after(250ms, cancellation_type::partial, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGTERM"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("DONE"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
}

TEST_F(Process, WHEN_bash_ignores_sigint_THEN_sleep_finishes)
{
   co_spawn(executor,
            execute("/usr/bin/bash", {"-c", "trap 'echo SIGINT' SIGINT; sleep 1; echo DONE"}),
            cancel_after(250ms, cancellation_type::total, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGINT"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("DONE"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_no_newline_at_end_of_output_THEN_prints_line)
{
   co_spawn(executor, execute("/usr/bin/echo", {"-n", "No newline at the end of this"}), token());
   EXPECT_CALL(*this, on_log("No newline at the end of this"));
   EXPECT_CALL(*this, on_exit(0));
}

TEST_F(Process, WHEN_multiple_lines_are_in_last_buffer_THEN_all_are_print)
{
   co_spawn(executor, execute("/usr/bin/echo", {"-ne", "First line\nLast line"}), token());
   EXPECT_CALL(*this, on_log("First line"));
   EXPECT_CALL(*this, on_log("Last line"));
   EXPECT_CALL(*this, on_exit(0));
}

TEST_F(Process, WHEN_process_fails_THEN_returns_non_zero_exit_code)
{
   co_spawn(executor, execute("/usr/bin/false", {}), token());
   EXPECT_CALL(*this, on_exit(1));
}

TEST_F(Process, WHEN_path_does_not_exist_THEN_raises_no_such_file_or_directory)
{
   co_spawn(executor, execute("/path/does/not/exist", {}), token());
   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::no_such_file_or_directory)));
}

// =================================================================================================
