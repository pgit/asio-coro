#include "asio-coro.hpp"
#include "utils.hpp"

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

class Process : public testing::Test
{
protected:
   /// Reads lines from \p pipe and prints them, colored, with a \p prefix, colored.
   /**
    * The \p pipe is passed as a reference and must be kept alive while running this coroutine!
    */
   awaitable<void> log(std::string_view prefix, readable_pipe& pipe)
   {
      auto print = [&](auto line)
      {
         std::println("{}: \x1b[32m{}\x1b[0m", prefix, line);
         on_log(line);
      };

      std::string buffer;
      try
      {
         for (;;)
         {
            auto n = co_await async_read_until(pipe, dynamic_buffer(buffer), '\n');
            print(std::string_view(buffer).substr(0, n - 1));
            buffer.erase(0, n);
         }
      }
      catch (const system_error& ec)
      {
         for (auto line : split_lines(buffer))
            print(line);

         if (ec.code() != error::eof)
         {
            std::println("{}: {}", prefix, ec.code().message());
            throw;
         }
      }
   }

   MOCK_METHOD(void, on_log, (std::string_view line), ());

   // ----------------------------------------------------------------------------------------------

   awaitable<void> log(readable_pipe& out, readable_pipe& err)
   {
      co_await (log("STDOUT", out) && log("STDERR", err));
   }

   awaitable<void> sleep(steady_timer::duration timeout)
   {
      steady_timer timer(co_await this_coro::executor);
      timer.expires_after(timeout);
      co_await timer.async_wait();
   }

   // ----------------------------------------------------------------------------------------------

   awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args)
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
      // cancellation signal immediatelly. This makes sense as bp::async_execute() will wait
      // for the process anyway.
      //
      // However, this introduces a problem with structured concurrency: The spawned thread of
      // execution may outlive this coroutine if we don't join it properly.
      //
      // FIXME: find a better way to do this
      //
      bool finished = false;
      co_spawn(executor, log(out, err), [&](std::exception_ptr) { finished = true; });
      auto [ec, rc] = co_await bp::async_execute(std::move(child), as_tuple);

      std::println("execute: finished, cancelled={}", cs.cancelled());
      if ((cs.cancelled() & cancellation_type::terminal) != cancellation_type::none)
      {
         rc = 9;
         out.close();
         err.close();
      }

      co_await this_coro::reset_cancellation_state();
      while (!finished)
      {
         std::println("waiting for log() to finish....");
         co_await post(executor);
      }
      co_return rc;
   }

   awaitable<int> execute2(std::filesystem::path path, std::vector<std::string> args)
   {
      std::println("execute: {} {}", path.generic_string(), join(args, " "));

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

      std::println("execute: communicating...");
      auto result = co_await co_spawn(executor, log(out, err), as_tuple);
      std::println("execute: cancellation state: {}", cs.cancelled());
      if (cs.cancelled() != cancellation_type::none)
      // if (std::get<0>(result))
      {
         (co_await this_coro::cancellation_state).clear();
         std::println("execute: communicating... timeout, interrupting (SIGINT)...");
         child.interrupt();
         auto result = co_await (log(out, err) || sleep(1s));
         if (result.index() == 1)
         {
            std::println("execute: communicating... timeout, requesting exit (SIGTERM)...");
            child.request_exit();
            result = co_await (log(out, err) || sleep(1s));
            if (result.index() == 1)
            {
               std::println("execute: communicating... timeout, terminating (SIGKILL)...");
               child.terminate();
            }
         }
      }
      std::println("execute: communicating... done");

      std::println("execute: waiting for process...");
      co_await child.async_wait();
      std::println("execute: waiting for process... done, exit code {}", child.exit_code());
      co_return child.exit_code();
   }

protected:
   auto token()
   {
      return [this](std::exception_ptr ep, int exit_code)
      {
         std::println("spawn: {}, exit_code={}", what(ep), exit_code);
         if (ep)
            on_error(code(ep));
         else
            on_exit(exit_code);
      };
   }

   MOCK_METHOD(void, on_error, (error_code ec), ());
   MOCK_METHOD(void, on_exit, (int exit_code), ());

private:
   io_context context;

protected:
   any_io_executor executor{context.get_executor()};
   void run() { ::run(context); }
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
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/ignore_sigint", "-i1", "-t1"}),
            cancel_after(250ms, cancellation_type::terminal, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(0);
   EXPECT_CALL(*this, on_exit(SIGKILL));
   run();
}

TEST_F(Process, WHEN_cancelled_without_type_THEN_is_cancelled_terminal)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/ignore_sigint", "-i1", "-t1"}),
            cancel_after(250ms, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_exit(SIGKILL));
   run();
}

TEST_F(Process, WHEN_cancelled_partial_THEN_receives_sigterm_and_exits_gracefully)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/ignore_sigint"}),
            cancel_after(250ms, cancellation_type::partial, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGTERM"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   run();
}

TEST_F(Process, WHEN_cancelled_total_THEN_receives_sigint_and_exits_gracefully)
{
   co_spawn(executor, execute("/usr/bin/stdbuf", {"-o0", "build/src/ignore_sigint"}),
            cancel_after(250ms, cancellation_type::total, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGINT"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   run();
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_cancelled_total_and_signals_are_ignored_THEN_escalates)
{
   co_spawn(executor, execute2("/usr/bin/stdbuf", {"-o0", "build/src/ignore_sigint", "-i1", "-t1"}),
            cancel_after(250ms, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGINT"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("SIGTERM"))).Times(1);
   EXPECT_CALL(*this, on_exit(9));
   run();
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_bash_is_killed_THEN_exits_with_code_9)
{
   auto coro =
      execute("/usr/bin/bash",
              {"-c", "trap 'echo SIGNAL' SIGINT SIGTERM; echo WAITING; sleep 10; echo DONE"});
   co_spawn(executor, std::move(coro), cancel_after(250ms, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("WAITING"))).Times(1);
   EXPECT_CALL(*this, on_exit(9));
   run();
}

TEST_F(Process, WHEN_bash_ingores_sigterm_THEN_sleep_finishes)
{
   auto coro = execute("/usr/bin/bash", {"-c", "trap 'echo SIGTERM' SIGTERM; sleep 1; echo DONE"});
   co_spawn(executor, std::move(coro), cancel_after(250ms, cancellation_type::partial, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("SIGTERM"))).Times(1);
   EXPECT_CALL(*this, on_log(HasSubstr("DONE"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
   run();
}

TEST_F(Process, WHEN_bash_ingores_sigint_THEN_sleep_finishes)
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

TEST_F(Process, WHEN_path_does_not_exist_THEN_raises_noch_such_file_or_directory)
{
   co_spawn(executor, execute("/path/does/not/exist", {}), token());
   EXPECT_CALL(*this, on_error(_));
   run();
}

// =================================================================================================
