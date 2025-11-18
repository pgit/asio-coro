#include "process_base.hpp"

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>

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
};

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_process_succeeds_THEN_returns_zero_exit_code)
{
   co_spawn(executor, [this] -> awaitable<ExitCode>
   {
      auto executor = co_await this_coro::executor;
      bp::process child(executor, "/usr/bin/true", {});
      co_return co_await child.async_wait();
   }, token());

   EXPECT_CALL(*this, on_exit(0));
}

TEST_F(Process, WHEN_process_fails_THEN_returns_non_zero_exit_code)
{
   co_spawn(executor, [this] -> awaitable<ExitCode>
   {
      auto executor = co_await this_coro::executor;
      bp::process child(executor, "/usr/bin/false", {});
      co_return co_await child.async_wait();
   }, token());

   EXPECT_CALL(*this, on_exit(Not(0)));
}

TEST_F(Process, WHEN_path_does_not_exist_THEN_raises_no_such_file_or_directory)
{
   co_spawn(executor, [this] -> awaitable<ExitCode>
   {
      auto executor = co_await this_coro::executor;
      bp::process child(executor, "/path/does/not/exist", {});
      co_return co_await child.async_wait();  // not reached
   }, token());

   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::no_such_file_or_directory)));
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_stdout_is_logged_THEN_can_detect_ping_output)
{
   co_spawn(executor, [this] -> awaitable<ExitCode>
   {
      auto executor = co_await this_coro::executor;
      readable_pipe out(executor);
      bp::process child(executor, "/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"},
                        bp::process_stdio{.out = out});
      co_await log_stdout(out);
      co_return co_await child.async_wait();
   }, token());

   EXPECT_CALL(*this, on_stdout(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt")));
   EXPECT_CALL(*this, on_exit(0));
}

// =================================================================================================
