#include "asio-coro.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <boost/algorithm/string/join.hpp>

#include <filesystem>
#include <print>

using namespace boost::asio;
using namespace experimental;
using namespace experimental::awaitable_operators;

using boost::algorithm::join;
using boost::system::error_code;
using boost::system::system_error;
namespace bp = boost::process::v2;

using namespace std::chrono_literals;

// =================================================================================================

/// Reads lines from \p pipe and prints them with \p prefix, colored.
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
      throw;
   }
}

// =================================================================================================

/// Execute process \p path with given \p args, interrupting it after \p timeout.
awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args)
{
   std::println("execute: {} {}", path.generic_string(), join(args, " "));

   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{.out = out});

   std::println("execute: communicating...");
   auto [ep] = co_await co_spawn(executor, log("STDOUT", out), as_tuple);
   std::println("execute: communicating... done, {}", what(ep));
   co_await this_coro::reset_cancellation_state();
   // co_await this_coro::throw_if_cancelled(false);
   // auto [ep] = co_await co_spawn(executor, log("STDOUT", out), as_tuple);
   // std::println("execute: communicating... {}", what(ep));

   //
   // Wait for process to finish and retrieve exit code.
   //
   std::println("execute: waiting for process...");
   co_await child.async_wait();
   std::println("execute: waiting for process... done, exit code {}", child.exit_code());
   co_return child.exit_code();
}

awaitable<int> execute0(std::filesystem::path path, std::vector<std::string> args)
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{.out = out});

   co_await log("STDOUT", out);

   std::println("execute: waiting for process...");
   co_await child.async_wait();
   std::println("execute: waiting for process... done, exit code {}", child.exit_code());
   co_return child.exit_code();
}

awaitable<int> execute1(std::filesystem::path path, std::vector<std::string> args)
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{.out = out});

   try
   {
      co_await log("STDOUT", out);
   }
   catch (const system_error& ec)
   {
      std::println("execute: log: {}", ec.code().message());
   }

   std::println("execute: waiting for process...");
   co_await child.async_wait();
   std::println("execute: waiting for process... done, exit code {}", child.exit_code());
   co_return child.exit_code();
}

awaitable<int> execute2(std::filesystem::path path, std::vector<std::string> args)
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{.out = out});

   try
   {
      co_await log("STDOUT", out);
   }
   catch (const system_error& ec)
   {
      std::println("execute: log: {}", ec.code().message());
   }
   co_await this_coro::reset_cancellation_state();
   std::println("execute: waiting for process...");
   co_await child.async_wait();
   std::println("execute: waiting for process... done, exit code {}", child.exit_code());
   co_return child.exit_code();
}

awaitable<int> execute3(std::filesystem::path path, std::vector<std::string> args)
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{.out = out});

   auto [ep] = co_await co_spawn(executor, log("STDOUT", out), as_tuple);
   std::println("execute: log: {}", what(ep));

   co_await this_coro::reset_cancellation_state();
   std::println("execute: waiting for process...");
   auto exit_code = co_await child.async_wait();
   std::println("execute: waiting for process... done, exit code {}", exit_code);
   co_return exit_code;
}

awaitable<int> execute4(std::filesystem::path path, std::vector<std::string> args)
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{.out = out});

   co_await this_coro::throw_if_cancelled(false);
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());
   auto promise = co_spawn(executor, log("STDOUT", out), as_tuple(use_promise));

   std::println("execute: waiting for process...");
   // auto [ec, exit_code] = co_await child.async_wait(as_tuple);
   auto [ec, exit_code] = co_await async_execute(std::move(child), as_tuple);
   std::println("execute: waiting for process... {}, exit code {}", ec.message(), exit_code);

   auto [ep] = co_await std::move(promise);
   std::println("execute: log: {}", what(ep));

   co_return exit_code;
}

awaitable<int> execute5(std::filesystem::path path, std::vector<std::string> args)
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{.out = out});

   auto promise = co_spawn(executor, log("STDOUT", out), as_tuple(use_promise));

   co_await this_coro::throw_if_cancelled(false);
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());

   cancellation_signal signal;
   auto cs = co_await this_coro::cancellation_state;
   cs.slot().assign(
      [&](cancellation_type ct)
      {
         std::println("cancelled: {}", ct);
         if ((ct & cancellation_type::total) != cancellation_type::none)
            child.interrupt();
         signal.emit(cancellation_type::terminal);
      });

   std::println("execute: waiting for process...");
   auto [ec, exit_code] =
      co_await child.async_wait(bind_cancellation_slot(signal.slot(), as_tuple));
   // auto [ec, exit_code] = co_await async_execute(std::move(child), as_tuple);
   std::println("execute: waiting for process... {}, exit code {}", ec.message(), exit_code);

   auto [ep] = co_await std::move(promise);
   std::println("execute: log: {}", what(ep));

   co_return exit_code;
}

/// Execute process \p path with given \p args, interrupting it after \p timeout.
awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args,
                       std::chrono::steady_clock::duration timeout)
{
   auto executor = co_await this_coro::executor;

   cancellation_signal signal;
   steady_timer timer(executor);
   timer.expires_after(timeout);
   timer.async_wait(
      [&](error_code ec)
      {
         if (!ec)
            signal.emit(cancellation_type::total);
      });

   co_return co_await co_spawn(executor, execute5(std::move(path), std::move(args)),
                               bind_cancellation_slot(signal.slot()));
}

// =================================================================================================

int main()
{
   io_context context;
   co_spawn(context, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}, 250ms),
            log_exception<int>("execute"));
   context.run();
}

// =================================================================================================
