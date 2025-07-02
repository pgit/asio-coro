#include "asio-coro.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <boost/program_options.hpp>

#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>

using namespace boost::asio;
using namespace boost::asio::experimental;
using namespace boost::asio::experimental::awaitable_operators;

using namespace std::chrono_literals;

namespace po = boost::program_options;

awaitable<void> sleep(steady_timer::duration timeout)
{
   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(timeout);
   co_await timer.async_wait();
}

int main(int argc, char* argv[])
{
   std::optional<size_t> ignore_sigint;
   std::optional<size_t> ignore_sigterm;

   //
   // Detect terminal width for help formatting.
   //
   unsigned int term_width = 80;
   if (isatty(STDOUT_FILENO))
   {
      struct winsize ws{};
      if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
         term_width = ws.ws_col;
   }

   //
   // Setup program options.
   //
   po::options_description desc("Wait for 10 seconds, SIGINT or SIGTERM, whichever comes first.",
                                term_width);
   auto opts = desc.add_options();
   opts("help,h", "Show help message");
   opts("handle-sigint,i", po::value<size_t>()->value_name("N"),
        "Install a signal handler for SIGINT and ignore N signals before exiting.");
   opts("handle-sigterm,t", po::value<size_t>()->value_name("N"),
        "Install a signal handler for SIGTERM and ignore N signals before exiting.");

   //
   // Parse program options.
   //
   try
   {
      po::variables_map vm;
      po::store(po::parse_command_line(argc, argv, desc), vm);
      po::notify(vm);

      if (vm.count("help"))
      {
         desc.print(std::cout);
         return 0;
      }

      if (vm.count("handle-sigint"))
         ignore_sigint = vm["handle-sigint"].as<size_t>();

      if (vm.count("handle-sigterm"))
         ignore_sigterm = vm["handle-sigterm"].as<size_t>();
   }
   catch (const std::exception& ex)
   {
      std::println("Error parsing command line: {}", ex.what());
      return 1;
   }

   io_context context;
   auto executor = context.get_executor();

   //
   // Setup signal handlers for SIGINT and SIGTERM, if configured. When the signal is received,
   // decrement a counter and finish the coroutine if the number of signals to ignore is depleted.
   //
   auto handle = [&](std::string_view name, int signal, size_t to_ignore) -> awaitable<void>
   {
      signal_set set(context, signal);
      for (int i = 0; i <= to_ignore; ++i)
         std::println(" {} (signal {}, #{}/{})", name, co_await set.async_wait(), i, to_ignore);
   };

   //
   // Create a vector of deferred operations to be used in a range-based parallel group that waits
   // for any one of the operations to complete. Note that the deferred operations are lazy and
   // are not started until awaited.
   //
   using op_type = decltype(co_spawn(executor, handle("", SIGINT, 0), deferred));
   std::vector<op_type> ops;

   if (ignore_sigint)
      ops.emplace_back(co_spawn(executor, handle("SIGINT", SIGINT, *ignore_sigint), deferred));

   if (ignore_sigterm)
      ops.emplace_back(co_spawn(executor, handle("SIGTERM", SIGTERM, *ignore_sigterm), deferred));

   ops.emplace_back(co_spawn(executor, sleep(10s), deferred));

   // https://think-async.com/Asio/asio-1.30.2/doc/asio/reference/experimental__make_parallel_group/overload2.html
   make_parallel_group(std::move(ops)).async_wait(wait_for_one(), detached);

   //
   // Finally, run IO context.
   //
   std::println("running IO context...");
   context.run();
   std::println("running IO context... done");
}
