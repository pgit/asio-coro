#include "asio-coro.hpp"
#include "utils.hpp"

#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <print>

namespace bp = boost::process::v2;
using namespace experimental::awaitable_operators;

// =================================================================================================

class Process : public testing::Test
{
protected:
   awaitable<void> log1(std::string_view prefix, readable_pipe pipe)
   {
      std::string buffer;
      for (;;)
      {
         auto [ec, n] =
            co_await async_read_until(pipe, dynamic_buffer(buffer), '\n', as_tuple(deferred));

         if (n)
         {
            auto line = std::string_view(buffer).substr(0, n - 1);
            std::println("{}: \x1b[32m{}\x1b[0m", prefix, line);
            buffer.erase(0, n);
         }

         if (ec)
            break;
      }

      if (!buffer.empty())
         std::println("{}: \x1b[32m{}\x1b[0m", prefix, buffer);
   }

   awaitable<void> log(std::string_view prefix, readable_pipe& pipe)
   {
      std::string buffer;
      try
      {
         for (;;)
         {
            auto n = co_await async_read_until(pipe, dynamic_buffer(buffer), '\n', deferred);
            assert(n > 0);

            auto line = std::string_view(buffer).substr(0, n - 1);
            std::println("{}: \x1b[32m{}\x1b[0m", prefix, line);
            buffer.erase(0, n);
         }
      }
      catch (const system_error& ec)
      {
         if (ec.code() != error::eof)
         {
            std::println("exception: {}", ec.code().message());
            throw;
         }
      }

      if (!buffer.empty())
         std::println("{}: \x1b[32m{}\x1b[0m", prefix, buffer);
   }

   awaitable<void> spawn_process(std::filesystem::path path, std::vector<std::string> args,
                                 Duration timeout)
   {
      std::println("spawn: {} {}", path.generic_string(), join(args, " "));

      readable_pipe out(context), err(context);
      bp::process child(context, bp::filesystem::path(path), args, bp::process_stdio{{}, out, err});

      use_awaitable_t<>::as_default_on_t<steady_timer> timer(context);
      timer.expires_after(timeout);

      std::println("spawn: communicating...");
      co_await (log("STDERR", err) && log("STDOUT", out) || timer.async_wait());
      std::println("spawn: communicating... done");

      child.interrupt();

      timer.expires_after(1s);
      co_await (log("STDERR", err) && log("STDOUT", out) || timer.async_wait());

      std::println("spawn: waiting for process...");
      co_await child.async_wait(deferred);
      std::println("spawn: waiting for process... done, exit code {}", child.exit_code());
   }

   void spawn(std::filesystem::path path, std::vector<std::string> args, Duration timeout = 5s)
   {
      co_spawn(context, spawn_process(std::move(path), std::move(args), timeout),
               log_exception("spawn"));
   }

   io_context context;
   int numSpawned = 0;
};

// -------------------------------------------------------------------------------------------------

TEST_F(Process, Ping)
{
   spawn("/usr/bin/ping", {"127.0.0.1", "-c", "5", "-i", "0.1"});
   ::run(context);
}

TEST_F(Process, Cancel)
{
   spawn("/usr/bin/ping", {"127.0.0.1", "-i", "0.1"}, 250ms);
   ::run(context);
}

TEST_F(Process, EchoNoNewline)
{
   spawn("/usr/bin/echo", {"-n", "There is no newline at the end of this"});
   ::run(context);
}

// =================================================================================================
