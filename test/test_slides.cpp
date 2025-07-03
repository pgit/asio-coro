#include <boost/asio.hpp>

#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace boost::asio;
namespace bp = boost::process::v2;

using boost::system::system_error;

using namespace ::testing;
using namespace std::chrono_literals;

// =================================================================================================

class Fixture : public testing::Test
{
   io_context context;

protected:
   any_io_executor executor{context.get_executor()};
   void run() { context.run(); }
};

// -------------------------------------------------------------------------------------------------

TEST_F(Fixture, WHEN_nothing_is_tested_THEN_nothing_happens) { run(); }

TEST_F(Fixture, WHEN_spawn_process_THEN_finishes_eventually)
{
   bp::process child(executor, "/usr/bin/sleep", {"0.1"});
   auto future = child.async_wait(use_future);
   run();
   EXPECT_EQ(future.get(), 0);
}

TEST_F(Fixture, WHEN_process_succeeds_THEN_returns_zero_exit_code)
{
   bp::process child(executor, "/usr/bin/true", {});
   auto future = child.async_wait(use_future);
   run();
   EXPECT_EQ(future.get(), 0);
}

TEST_F(Fixture, WHEN_process_fails_THEN_returns_nonzero_exit_code)
{
   bp::process child(executor, "/usr/bin/false", {});
   auto future = child.async_wait(use_future);
   run();
   EXPECT_NE(future.get(), 0);
}

TEST_F(Fixture, WHEN_process_is_cancelled_THEN_exception_is_thrown)
{
   bp::process child(executor, "/usr/bin/sleep", {"10"});
   auto future = child.async_wait(cancel_after(50ms, use_future));
   run();
   EXPECT_THROW(future.get(), system_error);
}

TEST_F(Fixture, WHEN_process_is_cancelled_THEN_error_is_reported_as_tuple)
{
   bp::process child(executor, "/usr/bin/sleep", {"10"});
   auto future = child.async_wait(cancel_after(50ms, as_tuple(use_future)));
   run();
   auto [ec, exit_code] = future.get();
   EXPECT_EQ(ec, boost::system::errc::operation_canceled);
}

// =================================================================================================
