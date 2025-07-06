#include "asio-coro.hpp"
#include "process_base.hpp"

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <boost/algorithm/string/join.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <print>

using namespace boost::asio;

namespace bp = boost::process::v2;
using namespace ::testing;

using boost::algorithm::join;

// =================================================================================================

struct Escalation
{
   std::vector<std::string> args;
   cancellation_type cancellation_type;
   std::set<std::string> expectations;
   int exit_code;
};

inline void PrintTo(const Escalation& e, std::ostream* os)
{
   *os << std::format(("{{{{{}}}, {}, {{{}}}, {}}}"), std::format("{}", join(e.args, ", ")),
                      e.cancellation_type, std::format("{}", join(e.expectations, ", ")),
                      e.exit_code);
}

//
// There is no builtin support for process groups in Process V2, because it is impossible to
// implement in a portable way. But for POSIX, we can rely on "process groups" and kill them, taking
// down any descendant process as well.
//
struct setpgid_initializer
{
   template <typename Launcher>
   error_code on_exec_setup(Launcher& launcher, const boost::filesystem::path& executable,
                            const char* const*(&cmd_line))
   {
      setpgid(0, 0);
      return error_code{};
   }
};

// -------------------------------------------------------------------------------------------------

/**
 * This fixture has a custom execute() function for spawning a process and logging its STDOUT and
 * STDERR. It supports automatic "escalation" of cancellation signals: When "total" cancellation
 * is requested, SIGINT is sent to the child process, just like bp::async_execute() does. But when
 * that does not lead to the process exiting within 1s, signals are escalated and SIGTERM is sent.
 * After another second, the process is finally killed using SIGKILL.
 */
class ProcessCustom : public ProcessBase,
                      public ::testing::Test,
                      public ::testing::WithParamInterface<Escalation>
{
protected:
   awaitable<int> execute(boost::filesystem::path path, std::vector<std::string> args)
   {
      std::println("execute: {} {}", path.generic_string(), join(args, " "));

      auto executor = co_await this_coro::executor;
      readable_pipe out(executor), err(executor);
      bp::process child(executor, bp::filesystem::path(path), args, bp::process_stdio{{}, out, err},
                        setpgid_initializer{});

      //
      // We support all three types of cancellation: total, partial and terminal.
      //
      // We have to explicitly enable all three types of cancellation here, as there is no way
      // no way to deduce the set of supported cancellation types from the inner async operation
      // (bp::async_execute). The default is to support terminal cancellation only.
      //
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      auto cs = co_await this_coro::cancellation_state;

      //
      // Start logging in background. We cannot use && here, as STDOUT and STDERR might be closed
      // independently of each other and we do want to continue reading from the other. However,
      // we can use a parallel group for this like operator&& does, but using the wait_for_all()
      // condition instead of wait_for_one_error().
      //
      // The use_promise completion token allows us to start the asynchronous operation eagerly.
      // This is similar to how use_future works, but this one can be awaited asynchronously.
      //
      using namespace experimental;
      auto promise = make_parallel_group(co_spawn(executor, log("STDOUT", out)),
                                         co_spawn(executor, log("STDERR", err)))
                        .async_wait(wait_for_all(), use_promise);

      //
      // async_wait() only reacts to terminal cancellation. So, if we receive a 'total' or 'partial'
      // cancellation request, we "upgrade" it to 'terminal' and pass that to async_wait().
      //
      cancellation_signal signal;
      cs.slot().assign([&](auto ct) { signal.emit(cancellation_type::terminal); });
      auto [ec, rc] = co_await child.async_wait(bind_cancellation_slot(signal.slot(), as_tuple));

      //
      // Determine if we have been cancelled and reset cancellation state afterwards. Otherwise,
      // the next co_await throws.
      //
      auto cancelled = cs.cancelled();
      std::println("execute: {} / {}", what(ec), cancelled);
      co_await this_coro::reset_cancellation_state();

      //
      // escalate: [[SIGINT -->] SIGTERM -->] SIGKILL
      //
      if ((cancelled & cancellation_type::total) != cancellation_type::none)
      {
         std::println("execute: interrupting...");
         child.interrupt(); // sends SIGINT
         std::tie(ec, rc) = co_await child.async_wait(cancel_after(1s, as_tuple));
         if (ec == boost::system::errc::operation_canceled)
            cancelled = cancellation_type::partial;
      }

      if ((cancelled & cancellation_type::partial) != cancellation_type::none)
      {
         std::println("execute: requesting exit...");
         child.request_exit(); // sends SIGTERM
         std::tie(ec, rc) = co_await child.async_wait(cancel_after(1s, as_tuple));
         if (ec == boost::system::errc::operation_canceled)
            cancelled = cancellation_type::terminal;
      }

      if ((cancelled & cancellation_type::terminal) != cancellation_type::none)
      {
         // std::println("execute: terminating...");
         // child.terminate();
         std::println("execute: terminating... (PGID={})", child.native_handle());
         ::kill(-child.native_handle(), SIGKILL); // kill process group
         co_await child.async_wait(as_tuple);
         co_return 9;
      }

      //
      // Wait for process to finish.
      //
      std::println("execute: waiting for process...");
      co_await child.async_wait(as_tuple);
      std::println("execute: waiting for process... done, exit code {}", child.exit_code());

      std::println("execute: waiting for remaining output...");
      co_await std::move(promise);
      std::println("execute: waiting for remaining output... done");

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
   void run() { context.run(); }
};

//
// This test is disabled because it leaves "sleep" running in background, detached.
//
TEST_F(ProcessCustom, DISABLED_WHEN_bash_is_killed_THEN_exits_with_code_9)
{
   auto coro =
      execute("/usr/bin/bash",
              {"-c", "trap 'echo SIGNAL' SIGINT SIGTERM; echo WAITING; sleep 10; echo DONE"});
   co_spawn(executor, std::move(coro), cancel_after(250ms, token()));
   EXPECT_CALL(*this, on_log(HasSubstr("WAITING"))).Times(1);
   EXPECT_CALL(*this, on_exit(9));
   run();
}

// =================================================================================================

//
// Test signal escalation with various instances of the 'handle_signal' program.
//
// SIGINT:
//   -i0: Exit gracefully when the signal is received for the first time
//   -i1: Ignore the first time the signal is received and exit gracefully the second time
//
// SIGTERM:
//   -t0: Exit gracefully when the signal is received for the first time
//   -t1: Ignore the first time the signal is received and exit gracefully the second time
//
// SIGKILL: Cannot be ignored.
//
// If no option for SIGINT and/or SIGTERM isspecified, no handleris installed for the signal  the
// program exits immediatelly with an appropriate exit status (2 or 15).
//
INSTANTIATE_TEST_SUITE_P(
   // clang-format off
   EscalationCases, ProcessCustom,
   ::testing::Values(
      Escalation{{},             cancellation_type::total,    {},                     2},
      Escalation{{"-i0"},        cancellation_type::total,    {"SIGINT"},             0},
      Escalation{{"-i1"},        cancellation_type::total,    {"SIGINT"},            15},
      Escalation{{"-i1", "-t0"}, cancellation_type::total,    {"SIGINT", "SIGTERM"},  0},
      Escalation{{"-i1", "-t1"}, cancellation_type::total,    {"SIGINT", "SIGTERM"},  9},
      Escalation{{""},           cancellation_type::partial,  {},                    15},
      Escalation{{"-t0"},        cancellation_type::partial,  {"SIGTERM"},            0},
      Escalation{{"-t1"},        cancellation_type::partial,  {"SIGTERM"},            9},
      Escalation{{"-i1", "-t1"}, cancellation_type::terminal, {},                     9},
      Escalation{{"--timeout", "0ms"}, cancellation_type::terminal, {},               0}
   )
   // clang-format on
);

// -------------------------------------------------------------------------------------------------

TEST_P(ProcessCustom, Escalation)
{
   const auto& param = GetParam();

   std::vector<std::string> args{"-o0", "build/src/handle_signal"};
   args.append_range(param.args);

   co_spawn(executor, execute("/usr/bin/stdbuf", args),
            cancel_after(250ms, param.cancellation_type, token()));

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));

   for (auto signal : {"SIGINT", "SIGTERM"})
   {
      if (param.expectations.contains(signal))
         EXPECT_CALL(*this, on_log(HasSubstr(signal))).Times(1); // must appear in output
      else
         EXPECT_CALL(*this, on_log(HasSubstr(signal))).Times(0); // may not appear in output
   }

   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(param.exit_code ? 0 : 1);
   EXPECT_CALL(*this, on_exit(param.exit_code));

   run();
}

// =================================================================================================
