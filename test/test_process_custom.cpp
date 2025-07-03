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
   std::vector<std::string> expectations;
   int exit_code;
};

inline void PrintTo(const Escalation& e, std::ostream* os)
{
   *os << std::format(("{{{{{}}}, {}, {{{}}}, {}}}"), std::format("{}", join(e.args, ", ")),
                      e.cancellation_type, std::format("{}", join(e.expectations, ", ")),
                      e.exit_code);
}

class ProcessCustom : public ProcessBase, public ::testing::WithParamInterface<Escalation>
{
protected:
   awaitable<int> execute(boost::filesystem::path path, std::vector<std::string> args)
   {
      std::println("execute: {} {}", path.generic_string(), join(args, " "));

      auto executor = co_await this_coro::executor;
      readable_pipe out(executor), err(executor);
      bp::process child(executor, bp::filesystem::path(path), args,
                        bp::process_stdio{{}, out, err});

      //
      // We support all three types of cancellation, total, partial and terminal.
      // Note that total cancellation implies partial and terminal cancellation.
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
      using namespace experimental;
      auto promise = make_parallel_group(co_spawn(executor, log("STDOUT", out)),
                                         co_spawn(executor, log("STDERR", err)))
                        .async_wait(wait_for_all(), use_promise);

      //
      // This is a little awkward, but a very ASIO-ish thing to do: Use a timer for notification
      // purposes. It seems to be the easiest way to wait for cancellation signals in a coroutine.
      //
      steady_timer timer(executor);
      timer.expires_at(steady_timer::time_point::max());
      auto [ec] = co_await timer.async_wait(as_tuple);
      int status = 0;

      //
      // Reset cancellation state if needed -- otherwise, the next co_await throws.
      //
      std::println("execute: {} / {}", what(ec), cs.cancelled());
      auto cancelled = cs.cancelled();
      if (cancelled != cancellation_type::none)
         co_await this_coro::reset_cancellation_state();

      //
      // SIGINT --> SIGTERM --> SIGKILL
      //
      if ((cancelled & cancellation_type::total) != cancellation_type::none)
      {
         std::println("execute: interrupting...");
         child.interrupt();
         std::tie(ec, status) = co_await child.async_wait(cancel_after(1s, as_tuple));
      }

      if (ec == boost::system::errc::operation_canceled ||
          (cancelled & cancellation_type::partial) != cancellation_type::none)
      {
         std::println("execute: requesting exit...");
         child.request_exit();
         std::tie(ec, status) = co_await child.async_wait(cancel_after(1s, as_tuple));
      }

      if (ec == boost::system::errc::operation_canceled ||
          (cancelled & cancellation_type::terminal) != cancellation_type::none)
      {
         std::println("execute: terminating...");
         child.terminate();
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

// -------------------------------------------------------------------------------------------------

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
      Escalation{{"-i1", "-t1"}, cancellation_type::terminal, {},                     9}
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

   for (const auto& expectation : param.expectations)
      EXPECT_CALL(*this, on_log(HasSubstr(expectation))).Times(1);

   EXPECT_CALL(*this, on_log(HasSubstr("done"))).Times(param.exit_code ? 0 : 1);
   EXPECT_CALL(*this, on_exit(param.exit_code));

   run();
}

// =================================================================================================
