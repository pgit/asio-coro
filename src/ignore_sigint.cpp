#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/program_options.hpp>
#include <iostream>

using namespace boost::asio;
using namespace experimental::awaitable_operators;
using namespace std::chrono_literals;
namespace po = boost::program_options;

int main(int argc, char* argv[])
{
   size_t ignore_sigint = 0;
   size_t ignore_sigterm = 0;

   //
   // Setup program options.
   //
   po::options_description desc("Wait for 10 seconds, SIGINT or SIGTERM, whichever comes first.");
   auto opts = desc.add_options();
   opts("help,h", "Show help message");
   opts("ignore-sigint,i", po::value(&ignore_sigint)->default_value(ignore_sigint),
        "Number of SIGINT signals to ignore before exitting");
   opts("ignore-sigterm,t", po::value(&ignore_sigterm)->default_value(ignore_sigterm),
        "Number of SIGTERM signals to ignore before exitting");

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
   }
   catch (const std::exception& ex)
   {
      std::println("Error parsing command line: {}", ex.what());
      return 1;
   }

   //
   // Setup signal handlers for SIGINT and SIGTERM.
   //
   io_context context;
   auto handle = [&](std::string_view name, int signal, size_t to_ignore) -> awaitable<void>
   {
      signal_set set(context, signal);
      for (int i = 0; i <= to_ignore; ++i)
         std::println(" {} (signal {}, #{}/{})", name, co_await set.async_wait(), i, to_ignore);
   };

   co_spawn(context,
            handle("SIGINT", SIGINT, ignore_sigint) || handle("SIGTERM", SIGTERM, ignore_sigterm),
            cancel_after(10s, detached));

   // setvbuf(stdout, nullptr, _IONBF, 0); // disable buffering

   std::println("running IO context...");
   context.run();
   std::println("running IO context... done");
}
