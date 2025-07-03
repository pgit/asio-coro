#include "asio-coro.hpp"
#include "process_base.hpp"

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <print>

using namespace boost::asio;

namespace bp = boost::process::v2;
using namespace ::testing;

// =================================================================================================

class Process : public ProcessBase
{
protected:
   awaitable<int> execute(boost::filesystem::path path, std::vector<std::string> args)
   {
      std::println("execute: {} {}", path.generic_string(), join(args, " "));

      //
      // Create pipes for STDOUT and STDERR and start the child process.
      //
      auto executor = co_await this_coro::executor;
      readable_pipe out(executor), err(executor);
      bp::process child(executor, bp::filesystem::path(path), args,
                        bp::process_stdio{{}, out, err});

      //
      // We support all three types of cancellation, total, partial and terminal.
      // Note that total cancellation implies partial and terminal cancellation.
      //
      // We have to explicitly enable all three types of cancellation here, as there is no way
      // no way to deduce the set of supported cancellation types from the inner async operation
      // (bp::async_execute). The default is to support terminal cancellation only.
      //
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      auto cs = (co_await this_coro::cancellation_state);

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

      auto [ec, rc] = co_await bp::async_execute(std::move(child), as_tuple);
      std::println("execute: finished, cancelled={}, rc={}", cs.cancelled(), rc);
      if ((cs.cancelled() & cancellation_type::terminal) != cancellation_type::none)
      {
         rc = SIGKILL; // work around https://github.com/boostorg/process/issues/503
         out.close();
         err.close();
      }

      co_await this_coro::reset_cancellation_state();
      std::println("execute: waiting for remaining output...");
      co_await std::move(promise);

      std::println("execute: waiting for remaining output... done");
      co_return rc;
   }
};

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_ping_is_started_THEN_completes_gracefully)
{
   co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}), token());
   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("rtt")));
   EXPECT_CALL(*this, on_exit(0));
   run();
}

TEST_F(Process, WHEN_multiple_pings_are_started_THEN_all_of_them_complete_gracefully)
{
   constexpr size_t N = 10;
   for (size_t i = 0; i < N; ++i)
      co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "2", "-i", "0.1"}), token());

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(N);
   EXPECT_CALL(*this, on_exit(0)).Times(N);
   run();
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
   run();
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
   run();
}

TEST_F(Process, WHEN_ping_is_cancelled_partial_THEN_exists_with_code_15)
{
   co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}),
            cancel_after(250ms, cancellation_type::partial, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_exit(SIGTERM)); // 15
   run();
}

TEST_F(Process, WHEN_ping_is_cancelled_total_THEN_exists_gracefully)
{
   co_spawn(executor, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}),
            cancel_after(250ms, cancellation_type::total, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   run();
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
   run();
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_sleep_is_cancelled_terminal_THEN_exits_with_code_9)
{
   co_spawn(executor, execute("/usr/bin/sleep", {"10"}), cancel_after(50ms, token()));
   EXPECT_CALL(*this, on_exit(SIGKILL));
   run();
}

TEST_F(Process, WHEN_sleep_is_cancelled_partial_THEN_exits_with_code_15)
{
   co_spawn(executor, execute("/usr/bin/sleep", {"10"}),
            cancel_after(50ms, cancellation_type::partial, token()));
   EXPECT_CALL(*this, on_exit(SIGTERM));
   run();
}

TEST_F(Process, WHEN_sleep_is_cancelled_total_THEN_exits_with_code_2)
{
   co_spawn(executor, execute("/usr/bin/sleep", {"10"}),
            cancel_after(50ms, cancellation_type::total, token()));
   EXPECT_CALL(*this, on_exit(SIGINT));
   run();
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_cancelled_terminal_THEN_exits_with_code_9)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/handle_signal", "-i1", "-t1"}),
            cancel_after(250ms, cancellation_type::terminal, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(0);
   EXPECT_CALL(*this, on_exit(SIGKILL));
   run();
}

TEST_F(Process, WHEN_cancelled_without_type_THEN_is_cancelled_terminal)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/handle_signal", "-i1", "-t1"}),
            cancel_after(250ms, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_exit(SIGKILL));
   run();
}

TEST_F(Process, WHEN_cancelled_partial_THEN_receives_sigterm_and_exits_gracefully)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/handle_signal", "-t0"}),
            cancel_after(250ms, cancellation_type::partial, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGTERM"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   run();
}

TEST_F(Process, WHEN_cancelled_total_THEN_receives_sigint_and_exits_gracefully)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/handle_signal", "-i0"}),
            cancel_after(250ms, cancellation_type::total, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGINT"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   run();
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_bash_is_killed_THEN_exits_with_code_9)
{
   auto coro =
      execute("/usr/bin/bash",
              {"-c", "trap 'echo SIGNAL' SIGINT SIGTERM; echo WAITING; sleep 100; echo DONE"});
   co_spawn(executor, std::move(coro), cancel_after(250ms, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("WAITING"))).Times(1);
   EXPECT_CALL(*this, on_exit(9));
   run();
}

TEST_F(Process, WHEN_bash_ignores_sigterm_THEN_sleep_finishes)
{
   auto coro = execute("/usr/bin/bash", {"-c", "trap 'echo SIGTERM' SIGTERM; sleep 1; echo DONE"});
   co_spawn(executor, std::move(coro), cancel_after(250ms, cancellation_type::partial, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGTERM"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("DONE"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   run();
}

TEST_F(Process, WHEN_bash_ignores_sigint_THEN_sleep_finishes)
{
   auto coro = execute("/usr/bin/bash", {"-c", "trap 'echo SIGINT' SIGINT; sleep 1; echo DONE"});
   co_spawn(executor, std::move(coro), cancel_after(250ms, cancellation_type::total, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGINT"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("DONE"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   run();
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_no_newline_at_end_of_output_THEN_prints_line)
{
   co_spawn(executor, execute("/usr/bin/echo", {"-n", "No newline at the end of this"}), token());
   EXPECT_CALL(*this, on_log("No newline at the end of this"));
   EXPECT_CALL(*this, on_exit(0));
   run();
}

TEST_F(Process, WHEN_process_fails_THEN_returns_non_zero_exit_code)
{
   co_spawn(executor, execute("/usr/bin/false", {}), token());
   EXPECT_CALL(*this, on_exit(1));
   run();
}

TEST_F(Process, WHEN_path_does_not_exist_THEN_raises_no_such_file_or_directory)
{
   co_spawn(executor, execute("/path/does/not/exist", {}), token());
   EXPECT_CALL(*this, on_error(_));
   run();
}

// =================================================================================================
