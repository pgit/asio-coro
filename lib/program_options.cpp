#include "program_options.hpp"

#include "run.hpp"

#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <thread>
#include <vector>

int program_options::run(int argc, char* argv[])
{
   namespace po = boost::program_options;

   bool debug = false;
   std::size_t threads = 0;

   po::options_description desc("Allowed options");
   desc.add_options()
      ("help,h", "produce help message")
      ("debug,d", po::bool_switch(&debug)->default_value(false), "use debug run() for io_context")
      ("threads,t", po::value<std::size_t>(&threads)->default_value(0), "number of extra threads to run the io_context")
      ;

   po::variables_map vm;
   try
   {
      po::store(po::parse_command_line(argc, argv, desc), vm);
      po::notify(vm);
   }
   catch (const po::error& ex)
   {
      std::cerr << "Command line error: " << ex.what() << std::endl;
      return 1;
   }

   if (vm.count("help"))
   {
      desc.print(std::cout);
      return 0;
   }

   boost::asio::io_context context;

   if (debug)
   {
      // Use the debug run variant which logs run_one etc.
      ::run(context);
      return 0;
   }

   // Spawn extra threads to run the context, like in client.cpp
   std::vector<std::jthread> workers;
   workers.reserve(threads);
   for (std::size_t i = 0; i < threads; ++i)
      workers.emplace_back([&context]() { context.run(); });

   // Run on the calling thread
   context.run();

   return 0;
}
