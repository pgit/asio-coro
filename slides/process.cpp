#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <boost/algorithm/string/join.hpp>
#include <boost/scope/scope_exit.hpp>

#include <filesystem>
#include <print>

using namespace boost::asio;
using namespace experimental::awaitable_operators;

using boost::algorithm::join;
using boost::system::system_error;
namespace bp = boost::process::v2;

using namespace std::chrono_literals;

// =================================================================================================

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

awaitable<void> sleep(std::chrono::steady_clock::duration timeout)
{
   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(timeout);
   co_await timer.async_wait();
}

// =================================================================================================

/// Execute process \p path with given \p args, interrupting it after \p timeout.
awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args,
                        std::chrono::steady_clock::duration timeout)
{
   std::println("execute: {} {}", path.generic_string(), join(args, " "));

   auto context = co_await this_coro::executor;
   readable_pipe out(context), err(context);
   bp::process child(context, bp::filesystem::path(path), args, bp::process_stdio{{}, out, err});

   std::println("execute: communicating...");
   // co_await co_spawn(context, log(out, err), cancel_after(timeout, as_tuple));
   // auto result = co_await (log(out, err) || sleep(timeout));
   auto result = co_await (log("STDOUT", out) && log("STDERR", err) || sleep(timeout));
   // std::println("execute: communicating... done ({})", result.index());

   child.interrupt();

   //
   // Give the process another second to exit after interrupting it, then terminate.
   //
   co_await (log("STDOUT", out) && log("STDERR", err) || sleep(1s));
   child.terminate();

   //
   // Wait for process to finish and retrieve exit code.
   //
   std::println("execute: waiting for process...");
   co_await child.async_wait();
   std::println("execute: waiting for process... done, exit code {}", child.exit_code());
   co_return child.exit_code();
}

// -------------------------------------------------------------------------------------------------

/// For slides only: STDOUT and STDERR, but no timeout
awaitable<void> execute(std::filesystem::path path, std::vector<std::string> args = {})
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor), err(executor);
   bp::process child(executor, bp::filesystem::path(path), std::move(args),
                     bp::process_stdio{{}, out, err});

   co_await (log("STDOUT", out) && log("STDERR", err));
   co_await child.async_wait();
}

// -------------------------------------------------------------------------------------------------

/// For slides only: STDOUT only, no timeout
awaitable<void> execute_stdout(std::filesystem::path path, std::vector<std::string> args = {})
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, bp::filesystem::path(path), std::move(args),
                     bp::process_stdio{{}, out, {}});

   co_await log("STDOUT", out);
   co_await child.async_wait();
}

// =================================================================================================

int main()
{
   io_context context;
   co_spawn(context, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}, 250ms), detached);
   context.run();
}

// =================================================================================================
