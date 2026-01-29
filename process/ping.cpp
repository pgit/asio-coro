#include "log.hpp"

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
namespace bp = boost::process::v2;

using namespace std::chrono_literals;

/// Execute process \p path with given \p args, logging its STDOUT.
/**
 * https://www.boost.org/doc/libs/latest/doc/html/process.html
 */
awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args = {})
{
   std::println("execute: {} {}", path.generic_string(), join(args, " "));

   auto executor = co_await this_coro::executor;
   readable_pipe out(executor);
   bp::process child(executor, path, args, bp::process_stdio{.out = out});

   std::println("execute: communicating...");
   co_await log("STDOUT", out);
   std::println("execute: communicating... done");

   //
   // Wait for process to finish and retrieve exit code.
   //
   std::println("execute: waiting for process...");
   auto exit_code = co_await child.async_wait();
   std::println("execute: waiting for process... done, exit code {}", exit_code);
   co_return exit_code;
}

int main()
{
   io_context context;
   // for (size_t i = 0; i < 100; ++i)
   co_spawn(context, execute("/usr/bin/ping", {"::1", "-c", "5", "-i", "0.1"}), detached);
   context.run();
}
