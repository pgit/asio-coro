#include "asio-coro.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <boost/algorithm/string/join.hpp>

#include <filesystem>
#include <print>

using namespace boost::asio;
using namespace experimental::awaitable_operators;

using boost::algorithm::join;
using boost::system::error_code;
using boost::system::system_error;
namespace bp = boost::process::v2;

using namespace std::chrono_literals;

// =================================================================================================

/// Reads lines from \p pipe and prints them, colored, with a \p prefix, colored.
/**
 * The \p pipe is passed as a reference and must be kept alive while running this coroutine!
 * On error while reading from the pipe, any lines in the remaining buffer are printed,
 * including the trailing incomplete line, if any.
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
      for (auto line : split_lines(buffer))
         print(line);
      if (ec.code() == error::eof)
         co_return;
      throw;
   }
}

/// Execute process \p path with given \p args, logging its STDOUT.
awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args = {})
{
   std::println("execute: {} {}", path.generic_string(), join(args, " "));

   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{{}, out, {}}, setpgid_initializer{});

   signal_set sigint(executor, SIGINT);
   sigint.async_wait(
      [&](error_code ec, int signal)
      {
         if (ec)
            return;

         std::println(" INTERRUPTED ({})", signal);
         child.interrupt();
      });

   std::println("execute: communicating...");
   co_await log("STDOUT", out);
   std::println("execute: communicating... done");

   //
   // Wait for process to finish and retrieve exit code.
   //
   std::println("execute: waiting for process...");
   co_await child.async_wait();
   std::println("execute: waiting for process... done, exit code {}", child.exit_code());
   co_return child.exit_code();
}

int main()
{
   io_context context;
   co_spawn(context, execute("/usr/bin/ping", {"::1", "-c", "5"}), detached);
   context.run();
}
