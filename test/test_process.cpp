#include "asio-coro.hpp"
#include "utils.hpp"

#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <print>

using namespace boost::asio;

namespace bp = boost::process::v2;

// =================================================================================================

/// Reads lines from \p pipe and prints them, colored, with a \p prefix, colored.
/**
 * The \p pipe is passed as a reference and must be kept alive while running this coroutine!
 */
awaitable<void> log(std::string_view prefix, readable_pipe& pipe)
{
   auto print = [&](auto line) { std::println("{}: \x1b[32m{}\x1b[0m", prefix, line); };

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
      for (auto line : split(buffer))
         print(line);

      if (ec.code() != error::eof)
      {
         std::println("{}: {}", prefix, ec.code().message());
         throw;
      }
   }

   co_return;
}

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

constexpr auto forever = steady_timer::duration::max();

// -------------------------------------------------------------------------------------------------

awaitable<void> execute(std::filesystem::path path, std::vector<std::string> args,
                        std::optional<steady_timer::duration> timeout = std::nullopt)
{
   std::println("execute: {} {}", path.generic_string(), join(args, " "));

   auto executor = co_await this_coro::executor;
   readable_pipe out(executor), err(executor);
   bp::process child(executor, bp::filesystem::path(path), args, bp::process_stdio{{}, out, err});

   std::println("execute: communicating...");
   auto result = co_await co_spawn(executor, log(out, err), as_tuple);
   if (std::get<0>(result))
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
}

// =================================================================================================

class Process : public testing::Test
{
protected:
   void spawn_execute(std::filesystem::path path, std::vector<std::string> args = {},
                      std::optional<steady_timer::duration> timeout = 5s)
   {
      // co_spawn(context, execute(std::move(path), std::move(args), timeout),
      // log_exception("spawn"));
      if (timeout)
         co_spawn(context, execute(std::move(path), std::move(args)),
                  cancel_after(*timeout, log_exception("spawn")));
      else
         co_spawn(context, execute(std::move(path), std::move(args)), log_exception("spawn"));
   }

   io_context context;
};

// -------------------------------------------------------------------------------------------------

TEST_F(Process, Ping)
{
   spawn_execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"});
   context.run();
}

TEST_F(Process, PingMulti)
{
   for (size_t i = 0; i < 10; ++i)
      spawn_execute("/usr/bin/ping", {"::1", "-c", "2", "-i", "0.1"});
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

// -------------------------------------------------------------------------------------------------

TEST_F(Process, WHEN_timeout_THEN_is_interrupted)
{
   spawn_execute("/usr/bin/bash", {"-c", "echo Hello!"}, 250ms);
   context.run();
}

TEST_F(Process, WHEN_ignores_sigint_THEN_runs_into_timeout)
{
   spawn_execute("build/src/ignore_sigint", {}, 250ms);
   context.run();
}

TEST_F(Process, WHEN_ignores_sigint_THEN_runs_into_timeout_buffered)
{
   spawn_execute("/usr/bin/stdbuf", {"-o0", "build/src/ignore_sigint"}, 250ms);
   context.run();
}

TEST_F(Process, WHEN_ignores_sigterm_THEN_runs_into_timeout)
{
   spawn_execute("/usr/bin/bash",
                 {"-c", "trap 'echo SIGNAL' SIGINT SIGTERM; echo WAITING; sleep 10"}, 250ms);
   context.run();
}

// -------------------------------------------------------------------------------------------------

TEST_F(Process, EchoNoNewline)
{
   spawn_execute("/usr/bin/echo", {"-n", "There is no newline at the end of this"});
   ::run(context);
}

TEST_F(Process, Fail)
{
   spawn_execute("/usr/bin/false");
   ::run(context);
}

TEST_F(Process, NotFound)
{
   spawn_execute("/path/does/not/exist");
   ::run(context);
}

// =================================================================================================
