#include "asio-coro.hpp"
#include "log.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
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
namespace bp = boost::process::v2;
using enum cancellation_type;

using namespace std::chrono_literals;

std::vector<std::string> make_args(int argc, char* argv[])
{
   std::vector<std::string> args;
   for (int i = 1; i < argc; ++i)
      args.emplace_back(argv[i]);
   return args;
}

/// Execute process \p path with given \p args, logging its STDOUT.
awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args = {})
{
   std::println("execute: {} {}", path.generic_string(), join(args, " "));

   auto ex = co_await this_coro::executor;
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());

   readable_pipe out(ex), err(ex);
   bp::process child(ex, path, args, bp::process_stdio{.out = out, .err = err},
                     setpgid_initializer{});

   // auto p1 = co_spawn(ex, log("STDOUT", out), use_promise);
   // auto p2 = co_spawn(ex, log("\x1b[31mSTDERR\x1b[0m", err), use_promise);
   auto promise = make_parallel_group(co_spawn(ex, log("STDOUT", out), deferred),
                                      co_spawn(ex, log("\x1b[31mSTDERR\x1b[0m", err), deferred))
                     .async_wait(wait_for_all(), use_promise);

   //
   // Wait for process to finish and retrieve exit code.
   //
   std::println("execute: waiting for process...");
   auto exit_code = co_await boost::process::async_execute(std::move(child));
   std::println("execute: waiting for process... done, exit code {}", exit_code);

   steady_timer timer(ex);
   timer.expires_after(1s);
   // co_await this_coro::throw_if_cancelled(false);
   co_await this_coro::reset_cancellation_state([](cancellation_type) { return terminal; });
   co_await (std::move(promise)(use_awaitable) || timer.async_wait(use_awaitable));

   std::println("done waiting for pipes, exit_code={}", exit_code);
   co_return exit_code;
}

/**
 * The signal handling here sort of "shifts" the signals one level of escalation:
 * The async_execute() async operation, when cancelled 'terminal', sends a SIGKILL to
 * the spawned process. But a SIGKILL cannot be intercepted. So what we do here instead
 * is to install a handler for SIGTERM instead, that will result terminal cancellation.
 * Accordingly, a SIGINT is translated to 'partial' cancellation, which sends a SIGTERM
 * to the spawned process.
 */
awaitable<void> signal_handling(cancellation_signal& signal)
{
   signal_set signals(co_await this_coro::executor, SIGINT, SIGTERM, SIGTSTP);
   for (;;)
   {
      auto signum = co_await signals.async_wait();
      std::println(" {}", strsignal(signum));

      switch (signum)
      {
      case SIGTSTP:
         signal.emit(cancellation_type::total); // INT
         break;
      case SIGINT:
         signal.emit(cancellation_type::partial); // TERM
         break;
      case SIGTERM:
         signal.emit(cancellation_type::terminal); // KILL
         break;
      }
   }
}

awaitable<int> with_signal_handling(awaitable<int> task)
{
   auto ex = co_await this_coro::executor;

   cancellation_signal signal;
   auto [order, ex0, ex1, r1] =
      co_await make_parallel_group(
         co_spawn(ex, signal_handling(signal), deferred),
         co_spawn(ex, std::move(task), bind_cancellation_slot(signal.slot(), deferred)))
         .async_wait(wait_for_one(), deferred);

   if (ex1)
      std::rethrow_exception(ex1);

   co_return r1;
}

int main(int argc, char* argv[])
{
   if (argc < 2)
   {
      std::println("Usage: {} <program> [args...]", argv[0]);
      return 1;
   }

   io_context context;
   auto future = co_spawn(
      context, with_signal_handling(execute(argv[1], make_args(argc - 1, argv + 1))), use_future);

   context.run();

   return future.get();
}
