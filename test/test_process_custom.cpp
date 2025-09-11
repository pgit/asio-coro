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
using namespace boost::system;

namespace bp = boost::process::v2;
using namespace ::testing;

using boost::algorithm::join;

// =================================================================================================

/// Configuration for parametrized fixture, see INSTANTIATE_TEST_SUITE_P below.
struct Escalation
{
   std::vector<std::string> args;
   cancellation_type cancellation_type;
   std::set<std::string> expectations;
   int exit_code;
};

inline void PrintTo(const Escalation& e, std::ostream* os)
{
   *os << std::format(("{{{{{}}}, {}, {{{}}}, {}}}"), join(e.args, ", "), e.cancellation_type,
                      join(e.expectations, ", "), e.exit_code);
}

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
   void TearDown() override { run(); }

   /**
    * This variant of executing a process handles cancellation in a special, escalating way:
    * Similar to \c async_execute(), it supports all three cancellation types. They map to
    * signalling SIGINT, SIGTERM and SIGKILL as well. If the cancellation type bitmask has more
    * than one of the 'total', 'partial' or 'terminal' bits set, and the process does not exit
    * on receiving the weakest signals within one second, the next signal is attempted.
    *
    * If the cancellation type does not include 'terminal' (resulting in SIGKILL), the process may
    * not exit at all.
    *
    * To do a full escalation of SIGINT -> SIGTERM -> SIGKILL, use \c cancellation_type::all.
    *
    * This operation newer completes with a cancellation error. If it is cancelled, it will try
    * to stop the process gracefully first. In the end, even on 'terminal' cancellation and SIGKILL,
    * an exit code is returned.
    */
   awaitable<ExitCode> execute(std::filesystem::path path, std::vector<std::string> args)
   {
      std::println("execute: {} {}", path.generic_string(), join(args, " "));

      auto executor = co_await this_coro::executor;
      readable_pipe out(executor), err(executor);
      bp::process child(executor, path, args, bp::process_stdio{.out = out, .err = err},
                        setpgid_initializer{});

      //
      // We support all three types of cancellation: total, partial and terminal.
      //
      // We have to explicitly enable all three types of cancellation here, as there is no way
      // no way to deduce the set of supported cancellation types from the inner async operation
      // (bp::async_wait). The default is to support terminal cancellation only.
      //
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      auto cs = co_await this_coro::cancellation_state;

      //
      // Start logging in background. We cannot use && here, as STDOUT and STDERR might be closed
      // independently of each other and we do want to continue reading from the other. However,
      // we could use a parallel group for this like operator&& does, but using the wait_for_all()
      // completion condition instead of wait_for_one_error().
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
      using enum cancellation_type;
      cancellation_signal signal;
      if (cs.slot().is_connected())
         cs.slot().assign([&](auto type)
         {
            std::println("execute: CANCELLED ({})", type);
            signal.emit(terminal);
         });
      auto [ec, rc] = co_await child.async_wait(bind_cancellation_slot(signal.slot(), as_tuple));

      //
      // Determine if we have been cancelled and reset cancellation state afterwards. Otherwise,
      // the next co_await throws.
      //
      auto cancelled = cs.cancelled();
      std::println("execute: {} / {}", what(ec), cancelled);
      co_await this_coro::reset_cancellation_state(); // doesn't hurt if nut cancelled

      //
      // escalate: [[SIGINT -->] SIGTERM -->] SIGKILL, waiting one second at each '-->'
      //
      if (ec && (cancelled & total) == total)
      {
         std::println("execute: interrupting...");
         child.interrupt(ec); // sends SIGINT, ignore error
         std::tie(ec, rc) = co_await child.async_wait(cancel_after(1s, as_tuple));
         std::println("execute: interrupting... {}", what(ec));
      }

      if (ec && (cancelled & partial) == partial)
      {
         std::println("execute: requesting exit...");
         child.request_exit(ec); // sends SIGTERM, ignore error
         std::tie(ec, rc) = co_await child.async_wait(cancel_after(1s, as_tuple));
         std::println("execute: requesting exit... {}", what(ec));
      }

      if (ec && (cancelled & terminal) == terminal)
      {
#if 0
         std::println("execute: terminating");
         child.terminate(ec); // sends SIGKILL, ignore error
#else
         std::println("execute: terminating (PGID={})", child.native_handle());
         ::kill(-child.native_handle(), SIGKILL); // kill process group
#endif
#if BOOST_VERSION < 108900
         std::ignore = co_await child.async_wait(as_tuple);
         co_return 9;
#else
         co_return co_await child.async_wait();
#endif
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
};

// =================================================================================================

//
// This test is disabled because it leaves "sleep" running in background, detached.
//
// UPDATE: Not any more -- we are now using setpgid() and killing the whole process group.
//
TEST_F(ProcessCustom, WHEN_bash_is_killed_THEN_exits_with_code_9)
{
   auto coro =
      execute("/usr/bin/bash",
              {"-c", "trap 'echo SIGNAL' SIGINT SIGTERM; echo WAITING; sleep 10; echo DONE"});
   co_spawn(executor, std::move(coro), cancel_after(250ms, token()));

   EXPECT_CALL(*this, on_log(HasSubstr("WAITING"))).Times(1);
   EXPECT_CALL(*this, on_exit(9));
}

// -------------------------------------------------------------------------------------------------

/**
 * The parallel group operator "||" returns a variant of the return types of both coroutines.
 * The \c sleep() completes first and returns \c void, so we'll have \c std::monostate here.
 *
 * If cancelled like this, there is no way to retrieve the exit code.
 */
TEST_F(ProcessCustom, WHEN_operator_or_times_out_THEN_is_interrupted_and)
{
   auto coro = execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"});
   co_spawn(executor, std::move(coro) || sleep(250ms),
            [token = token()](const std::exception_ptr& ep,
                              std::variant<ExitCode, std::monostate> variant)
   {
      EXPECT_NO_THROW(std::get<std::monostate>(variant));
      token(ep, 0);
   });

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_exit(0));
}

// -------------------------------------------------------------------------------------------------

inline awaitable<void> sleepAndThrow(steady_timer::duration timeout)
{
   co_await sleep(timeout);
   throw system_error(error_code(errc::operation_canceled, system_category()));
}

/**
 * We can also use \c operator&& to do cancellation, if the \c sleep() completes with an error
 * instead of just returning \c void. This would short-circuit and cancel the &&.
 *
 * However, similar to \c operator|| above, there is no way to get at the exit code this way, as
 * only the cancellation error is reported.
 */
TEST_F(ProcessCustom, WHEN_operator_and_times_out_THEN_is_interrupted)
{
   auto coro = execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"});
   co_spawn(executor, std::move(coro) && sleepAndThrow(250ms), token());

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_log(HasSubstr("5 packets"))).Times(0);
   EXPECT_CALL(*this, on_log(HasSubstr("rtt"))).Times(1);
   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::operation_canceled)));
}

// =================================================================================================

//
// Test signal escalation with various instances of the 'handle_signal' program.
//
// -i0: Exit gracefully when SIGINT is received for the first time
// -t0: Exit gracefully when SIGTERM is received for the first time
// -i1: Ignore the first time SIGINT is received and exit gracefully the second time
// -t1: Ignore the first time SIGTERM is received and exit gracefully the second time
//
// SIGKILL cannot be ignored.
//
// If no option for SIGINT and/or SIGTERM isspecified, no handlers are installed and the
// program exits immediately with an appropriate exit status of 2 (SIGINT) or 15 (SIGTERM).
//
using enum cancellation_type;
INSTANTIATE_TEST_SUITE_P(
   // clang-format off
   EscalationCases, ProcessCustom,
   ::testing::Values(
      Escalation{{},                   all,                    {},                     SIGINT},
      Escalation{{"-i0"},              all,                    {"SIGINT"},             0},
      Escalation{{"-i1"},              all,                    {"SIGINT"},             SIGTERM},
      Escalation{{"-i1", "-t0"},       all,                    {"SIGINT", "SIGTERM"},  0},
      Escalation{{"-i1", "-t1"},       all,                    {"SIGINT", "SIGTERM"},  SIGKILL},
      Escalation{{"-i1", "-t1"},       terminal|        total, {"SIGINT"},             SIGKILL},
      Escalation{{""},                 terminal|partial,       {},                     SIGTERM},
      Escalation{{"-t0"},              terminal|partial,       {"SIGTERM"},            0},
      Escalation{{"-t1"},              terminal|partial,       {"SIGTERM"},            SIGKILL},
      Escalation{{"-i1", "-t1"},       terminal,               {},                     SIGKILL},
      Escalation{{"--timeout", "2s", "-t1"},    partial,       {"SIGTERM", "TIMEOUT"}, 0},
      Escalation{{"--timeout", "0ms"}, terminal,               {"TIMEOUT"},            0}
   )
   // clang-format on
);

// -------------------------------------------------------------------------------------------------

TEST_P(ProcessCustom, Escalation)
{
   const auto& param = GetParam();

#if 0   
   //
   // Use 'stdbuf' to Enable line buffering on STDOUT. Without this, we can't EXPECT any output
   // before the child process exits, as it may be buffered.
   //
   // UPDATE: 'handle_signal' now calls 'setlinebuf(stdout)'.
   //         Keeping this as a reference on how to use 'stdbuf'.
   //
   std::vector<std::string> args{"-eL", "build/src/handle_signal"};
   args.append_range(param.args);

   co_spawn(executor, execute("/usr/bin/stdbuf", args),
            cancel_after(250ms, param.cancellation_type, token()));
#else
   co_spawn(executor, execute("build/src/handle_signal", param.args),
            cancel_after(250ms, param.cancellation_type, token()));
#endif

   EXPECT_CALL(*this, on_log(_)).Times(AtLeast(1));

   for (auto signal : {"SIGINT", "SIGTERM", "TIMEOUT"})
   {
      if (param.expectations.contains(signal))
         EXPECT_CALL(*this, on_log(HasSubstr(signal))).Times(1); // must appear in output
      else
         EXPECT_CALL(*this, on_log(HasSubstr(signal))).Times(0); // may not appear in output
   }

   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(param.exit_code ? 0 : 1);
   EXPECT_CALL(*this, on_exit(param.exit_code));
}

// =================================================================================================
