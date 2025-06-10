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
         std::println("{}: log: {}", prefix, ec.code().message());
         throw;
      }
   }
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
   auto context = co_await this_coro::executor;
   readable_pipe out(context), err(context);
   bp::process child(context, bp::filesystem::path(path), args, bp::process_stdio{{}, out, err});

   auto result = co_await (log("STDOUT", out) && log("STDERR", err) || sleep(timeout));
   if (result.index() == 1)
      child.interrupt();
   co_await (log("STDOUT", out) && log("STDERR", err));
   co_await child.async_wait();
   co_return child.exit_code();
}

// =================================================================================================

int main(int argc, char** argv)
{
   io_context context;
   auto task = execute("/usr/bin/ping", {argc > 1 ? argv[1] : "::1", "-v", "-c", "5", "-i", "0.1"}, 250ms);
   auto future = co_spawn(context, std::move(task), use_future);
   context.run();
   return future.get();
}

// =================================================================================================
