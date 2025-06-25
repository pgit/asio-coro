#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <boost/algorithm/string/join.hpp>
#include <boost/scope/scope_exit.hpp>

#include <filesystem>
#include <print>
#include <ranges>

using namespace boost::asio;
using namespace experimental::awaitable_operators;

using boost::system::system_error;
namespace bp = boost::process::v2;

using namespace std::chrono_literals;

// =================================================================================================

/// Transform \p lines into a range of \c string_view, splitting at LF. Skip last line if empty.
auto split_lines(std::string_view lines)
{
   if (lines.ends_with('\n'))
      lines.remove_suffix(1);

   return lines | std::views::split('\n') |
          std::views::transform([](auto range) { return std::string_view(range); });
}

/// Reads lines from \p pipe and prints them, colored, with a \p prefix, colored.
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

      if (ec.code() != error::eof)
      {
         std::println("{}: log: {}", prefix, ec.code().message());
         throw;
      }
   }
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

// =================================================================================================

/// Execute process \p path with given \p args, interrupting it after \p timeout.
awaitable<int> execute(std::filesystem::path path, std::vector<std::string> args,
                       steady_timer::duration timeout)
{
   auto executor = co_await this_coro::executor;
   readable_pipe out(executor), err(executor);
   bp::process child(executor, bp::filesystem::path(path), args, bp::process_stdio{{}, out, err});

   if ((co_await (log(out, err) || sleep(timeout))).index() == 1)
   {
      child.interrupt();
      if ((co_await (log(out, err) || sleep(1s))).index() == 1)
         child.terminate();
   }
   co_await child.async_wait();
   co_return child.exit_code();
}

// =================================================================================================

int main(int argc, char** argv)
{
   io_context context;
   auto task =
      execute("/usr/bin/ping", {argc > 1 ? argv[1] : "::1", "-v", "-c", "5", "-i", "0.1"}, 250ms);
   auto future = co_spawn(context, std::move(task), use_future);
   context.run();
   return future.get();
}

// =================================================================================================
