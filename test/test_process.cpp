#include "asio-coro.hpp"
#include "utils.hpp"

#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <print>

namespace bp = boost::process::v2;
using duration = std::chrono::steady_clock::duration;

// =================================================================================================

class Process : public testing::Test
{
protected:
   /// Reads lines from \p pipe and prints them, colored, with a \p prefix, colored.
   awaitable<void> log(std::string_view prefix, readable_pipe& pipe)
   {
      auto log = [&](auto line) { std::println("{}: \x1b[32m{}\x1b[0m", prefix, line); };

      std::string buffer;
      try
      {
         // clang-format off
         auto finally = boost::scope::scope_exit([&]() { if (!buffer.empty()) log(buffer); });
         // clang-format on
         for (;;)
         {
            auto n = co_await async_read_until(pipe, dynamic_buffer(buffer), '\n');
            log(std::string_view(buffer).substr(0, n - 1));
            buffer.erase(0, n);
         }
      }
      catch (const system_error& ec)
      {
         if (ec.code() != error::eof)
         {
            std::println("log: {}", ec.code().message());
            throw;
         }
      }
   }

   awaitable<void> log(readable_pipe& out, readable_pipe& err)
   {
      co_await (log("STDOUT", out) && log("STDERR", err));
   }

   awaitable<void> sleep(Duration timeout)
   {
      steady_timer timer(co_await this_coro::executor);
      timer.expires_after(timeout);
      co_await timer.async_wait();
   }

   awaitable<void> execute(std::filesystem::path path, std::vector<std::string> args,
                           std::optional<duration> timeout = std::nullopt)
   {
      std::println("execute: {} {}", path.generic_string(), join(args, " "));

      readable_pipe out(context), err(context);
      bp::process child(context, bp::filesystem::path(path), args, bp::process_stdio{{}, out, err});

      std::println("execute: communicating...");
      if (timeout)
         co_await (log("STDOUT", out) && log("STDERR", err) || sleep(*timeout));
      else
         co_await (log("STDOUT", out) && log("STDERR", err));
      std::println("execute: communicating... done");
      child.interrupt();

      co_await (log("STDOUT", out) && log("STDERR", err) || sleep(1s));
      child.terminate();

      std::println("execute: waiting for process...");
      co_await child.async_wait();
      std::println("execute: waiting for process... done, exit code {}", child.exit_code());
   }

   void spawn_execute(std::filesystem::path path, std::vector<std::string> args = {},
                      std::optional<duration> timeout = 5s)
   {
      co_spawn(context, execute(std::move(path), std::move(args), timeout), log_exception("spawn"));
   }

   io_context context;
};

// -------------------------------------------------------------------------------------------------

TEST_F(Process, Ping)
{
   spawn_execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"});
   context.run();
}

TEST_F(Process, DISABLED_Top)
{
   spawn_execute("/usr/bin/top", {}, 10s);
   context.run();
}

TEST_F(Process, Interrupt)
{
   spawn_execute("/usr/bin/ping", {"127.0.0.1", "-i", "0.1"}, 250ms);
   context.run();
}

TEST_F(Process, EchoNoNewline)
{
   spawn_execute("/usr/bin/echo", {"-n", "There is no newline at the end of this"});
   ::run(context);
}

// =================================================================================================
