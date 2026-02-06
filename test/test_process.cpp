#include "process_base.hpp"
#include "stream_utils.hpp"

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
   co_spawn(executor, [] -> awaitable<ExitCode>
   {
      auto executor = co_await this_coro::executor;
      bp::process child(executor, "/usr/bin/true", {});
      co_return co_await child.async_wait();
   }, token());

   EXPECT_CALL(*this, on_exit(0));
}

TEST_F(Process, WHEN_process_fails_THEN_returns_non_zero_exit_code)
{
   co_spawn(executor, [] -> awaitable<ExitCode>
   {
      auto executor = co_await this_coro::executor;
      bp::process child(executor, "/usr/bin/false", {});
      co_return co_await child.async_wait();
   }, token());

   EXPECT_CALL(*this, on_exit(Not(0)));
}

TEST_F(Process, WHEN_path_does_not_exist_THEN_raises_no_such_file_or_directory)
{
   co_spawn(executor, [] -> awaitable<ExitCode>
   {
      auto executor = co_await this_coro::executor;
      bp::process child(executor, "/path/does/not/exist", {});
      co_return co_await child.async_wait(); // not reached
   }, token());

   EXPECT_CALL(*this, on_error(make_system_error(boost::system::errc::no_such_file_or_directory)));
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_stdout_is_logged_THEN_can_detect_ping_output)
{
   co_spawn(executor, [this] -> awaitable<ExitCode>
   {
      auto ex = co_await this_coro::executor;
      readable_pipe out(ex);
      bp::process child(ex, "/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"},
                        bp::process_stdio{.out = out});
      co_await log_stdout(out);
      co_return co_await child.async_wait();
   }, token());

   EXPECT_CALL(*this, on_stdout(_)).Times(AtLeast(1));
   EXPECT_CALL(*this, on_stdout(HasSubstr("rtt")));
   EXPECT_CALL(*this, on_exit(0));
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_range_is_piped_to_gzip_THEN_stdout_has_compressed_data)
{
   co_spawn(executor, [] -> awaitable<ExitCode>
   {
      auto ex = co_await this_coro::executor;
      writable_pipe in(ex);
      readable_pipe out(ex);

      bp::process child(ex, "/usr/bin/gzip", {}, bp::process_stdio{.in = in, .out = out});

      auto [n, compressed] =
         co_await (write_and_close(std::move(in), std::ranges::views::iota(0u, 1024u * 1024)) &&
                   read_all(std::move(out)));

      std::println("compressed {} bytes to {}", n, compressed.size());

      auto exit_code = co_await child.async_wait();

      EXPECT_GE(compressed.size(), 2U);
      EXPECT_EQ(static_cast<unsigned char>(compressed[0]), 0x1f);
      EXPECT_EQ(static_cast<unsigned char>(compressed[1]), 0x8b);

      co_return exit_code;
   }, token());

   EXPECT_CALL(*this, on_exit(0));
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_compress_and_decompress_THEN_size_is_equal)
{
   constexpr size_t N = 10;
   for (size_t i = 0; i < N; ++i)
      co_spawn(executor, [] -> awaitable<ExitCode>
      {
         auto ex = co_await this_coro::executor;
         writable_pipe in(ex); // compressor input
         readable_pipe mout(ex); // compressor output
         writable_pipe min(ex); // decompressor input
         readable_pipe out(ex); // decompressor output

         bp::process zip(ex, "/usr/bin/gzip", {}, bp::process_stdio{.in = in, .out = mout});
         bp::process unzip(ex, "/usr/bin/gunzip", {}, bp::process_stdio{.in = min, .out = out});

         auto [n0, n1, n2] =
            co_await (write_and_close(std::move(in), std::ranges::views::iota(0u), 1s) &&
                      cat(std::move(mout), std::move(min)) && //
                      count(std::move(out)));

         std::println("original {} bytes -> compressed {} -> decompressed {} bytes", n0, n1, n2);
         EXPECT_GT(n0, n1);
         EXPECT_EQ(n0, n2);

         auto [e0, e1] =
            co_await (zip.async_wait(use_awaitable) && unzip.async_wait(use_awaitable));
         EXPECT_EQ(e0, 0);
         EXPECT_EQ(e1, 0);

         co_return e0 ? e0 : e1;
      }, token());

   EXPECT_CALL(*this, on_exit(0)).Times(N);
}

// =================================================================================================
