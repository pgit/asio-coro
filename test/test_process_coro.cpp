#include "process_base.hpp"

#include <boost/asio/experimental/coro.hpp>
#include <boost/asio/experimental/use_coro.hpp>

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

class ProcessCoro : public ProcessBase, public testing::Test
{
protected:
   void SetUp() override { EXPECT_CALL(*this, on_stdout(_)).Times(AtLeast(0)); }
   void TearDown() override { run(); }

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
      bp::process child(executor, path, args, bp::process_stdio{.out = out});
      co_return co_await test(std::move(out), std::move(child));
   }

   /// Returns an awaitable<ExitCode> of a coroutine executing a test ping.
   awaitable<ExitCode> ping() { return execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}); }

   /// The test payload, awaited by execute() after the process has been started.
   std::function<awaitable<ExitCode>(readable_pipe out, bp::process child)> test;
};

// =================================================================================================

coro<std::string_view> reader(readable_pipe& pipe)
{
   std::string buf;
   while (pipe.is_open())
   {
      auto n = co_await async_read_until(pipe, dynamic_buffer(buf), '\n');
      assert(n > 0);
      co_yield buf.substr(0, n - 1);
      buf.erase(0, n);
   }
}

awaitable<void> logger(readable_pipe& pipe)
{
   std::println("execute: communicating...");
   auto lineReader = reader(pipe);
   for (;;)
   {
      auto line = co_await lineReader.async_resume(use_awaitable);
      if (line.has_value())
         std::println("STDOUT: \x1b[32m{}\x1b[0m", *line);
   }
   std::println("execute: communicating... done");
}

TEST_F(ProcessCoro, Ping)
{
   test = [](readable_pipe out, bp::process child) -> awaitable<ExitCode>
   {
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      co_await this_coro::throw_if_cancelled(false);

      auto ex = co_await this_coro::executor;
      auto promise = co_spawn(ex, logger(out), use_promise);

      std::println("execute: waiting for process...");
      // auto exit_code = co_await child.async_wait(bind_cancellation_slot(cancellation_slot()));
      auto exit_code =
         co_await async_execute(std::move(child));
      std::println("execute: waiting for process... done, exit code {}", exit_code);

      co_await co_spawn(ex, [&] -> awaitable<void> { co_await std::move(promise); }, as_tuple);
      co_return exit_code;
   };

   EXPECT_CALL(*this, on_exit(0));
   co_spawn(executor, ping(), cancel_after(150ms, cancellation_type::total, token()));
}

// =================================================================================================
