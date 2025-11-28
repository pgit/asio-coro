#include "program_options.hpp"

#include "run.hpp"

#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <thread>
#include <vector>

int run(boost::asio::io_context& context, int argc, char* argv[])
{
   namespace po = boost::program_options;

   bool debug = false;
   std::size_t threads = 0;

   po::options_description desc("Usage");
   desc.add_options()("help,h", "produce help message")(
      "debug,d", po::bool_switch(&debug)->default_value(debug), "use debug run() for io_context")(
      "threads,t", po::value<std::size_t>(&threads)->default_value(threads),
      "number of extra threads to run the io_context");

   po::variables_map vm;
   try
   {
      po::store(po::parse_command_line(argc, argv, desc), vm);
      po::notify(vm);
   }
   catch (const po::error& ex)
   {
      std::println(std::cerr, "ERROR: {}", ex.what());
      return 1;
   }

   if (vm.count("help"))
   {
      desc.print(std::cout);
      return 0;
   }

   if (debug && threads > 0)
   {
      std::println(std::cerr, "ERROR: debug output works single-threaded only");
      return 1;
   }

   //
   // finally, run IO context
   //
   if (debug)
   {
      ::runDebug(context);
   }
   else
   {
      std::vector<std::jthread> workers;
      workers.reserve(threads);
      for (std::size_t i = 0; i < threads; ++i)
         workers.emplace_back([&context]() { context.run(); });

      context.run();

      for (auto& thread : workers)
         thread.join();
   }

   return 0;
}
