#include "asio-coro.hpp"
#include "utils.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/process.hpp>
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/async.hpp>
#include <boost/process/v1/async_pipe.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/environment.hpp>
#include <boost/process/v1/io.hpp>

#include <ranges>

#include <gtest/gtest.h>

#include <print>

namespace bp = boost::process::v1;
using namespace experimental::awaitable_operators;

// =================================================================================================

class Process : public testing::Test
{
protected:
   awaitable<void> log(std::string_view prefix, bp::async_pipe pipe)
   {
      std::string buffer;
      for (;;)
      {
         auto [ex, n] =
            co_await async_read_until(pipe, dynamic_buffer(buffer), '\n', as_tuple(deferred));

         if (n)
         {
            auto line = std::string_view(buffer).substr(0, n - 1);
            std::println("{}: \x1b[32m{}\x1b[0m", prefix, line);
            buffer.erase(0, n);
         }

         if (ex)
            break;
      }

      if (!buffer.empty())
         std::println("{}: \x1b[32m{}\x1b[0m", prefix, buffer);
   }

   awaitable<std::string> consume(bp::async_pipe pipe)
   {
      std::string result;
      std::vector<char> data(1460);
      for (;;)
      {
         auto [ex, nread] = co_await async_read(pipe, buffer(data), as_tuple(deferred));
         result += std::string_view(data.data(), nread);
         if (nread)
            std::println("STDOUT: {} bytes", nread);
         if (ex)
            break;
      }
      co_return result;
   }

   template <std::ranges::input_range R>
      requires std::formattable<std::ranges::range_value_t<R>, char>
   std::string join(R&& range, std::string_view delimiter)
   {
      std::string result;
      auto it = std::ranges::begin(range);
      const auto end = std::ranges::end(range);

      if (it != end)
      {
         result += std::format("{}", *it++);
         while (it != end)
         {
            result += delimiter;
            result += std::format("{}", *it++);
         }
      }

      return result;
   }

   awaitable<void> spawn_process(bp::filesystem::path path, std::vector<std::string> args)
   {
      std::println("spawn: {} {}", path.generic_string(), join(args, " "));

      auto env = bp::environment();
      bp::async_pipe out(context), err(context);
      bp::child child(
         path, env, std::move(args), bp::std_out > out, bp::std_err > err,
         bp::on_exit = [](int exit, const std::error_code& ec) { //
            std::println("on_exit: exit={}, ec={}", exit, ec.message());
         });

      std::println("spawn: communicating...");
      co_await (log("STDERR", std::move(err)) && log("STDOUT", std::move(out)));
      std::println("spawn: communicating... done");

      std::println("spawn: waiting for process...");
      child.wait(); // FIXME: this is sync -- but will it ever block here, after pipes are closed?
      std::println("spawn: waiting for process... done, exit_code={}", child.exit_code());
   }

   void spawn(bp::filesystem::path path, std::vector<std::string> args)
   {
      co_spawn(context, spawn_process(std::move(path), std::move(args)), detached);
   }

   io_context context;
   int numSpawned = 0;
};

// -------------------------------------------------------------------------------------------------

using Args = std::vector<std::string>;

TEST_F(Process, Ping)
{
   Args args = {"127.0.0.1", "-c", "5", "-i", "0.1"};
   spawn("/usr/bin/ping", std::move(args));

   ::run(context);
}

// =================================================================================================
