#include "asio-coro.hpp"

#include <boost/asio/error.hpp>
#include <boost/program_options.hpp>

#include <iostream>

using std::chrono::milliseconds;
namespace po = boost::program_options;

int main(int argc, char* argv[])
{
   std::string host = "127.0.0.1";
   uint16_t port = 55555;
   double timeout_seconds = 1.0;

   //
   // Define and parse command line options.
   //
   po::options_description desc("Allowed options");
   desc.add_options()("help,h", "produce help message");
   desc.add_options()("host", po::value<std::string>(&host)->default_value(host),
                      "host to connect to");
   desc.add_options()("port,p", po::value(&port)->default_value(port)->value_name("PORT"),
                      "port number");
   desc.add_options()(
      "duration,d",
      po::value(&timeout_seconds)->default_value(timeout_seconds)->value_name("SECONDS"),
      "maximum duration (in seconds) to wait for the port");

   po::variables_map vm;
   try
   {
      po::store(po::parse_command_line(argc, argv, desc), vm);
      po::notify(vm);
   }
   catch (const po::error& ex)
   {
      std::println(std::cerr, "Command line error: {}", ex.what());
      return 1;
   }

   if (vm.count("help"))
   {
      desc.print(std::cout);
      return 1;
   }

   if (timeout_seconds < 0.0)
   {
      std::println("ERROR: duration must be non-negative");
      return 1;
   }

   using namespace std::chrono;
   auto timeout = duration_cast<steady_timer::duration>(duration<double>(timeout_seconds));

   //
   // Create IO context with the configured number of concurrent connections.
   //
   io_context io_context;

   co_spawn(io_context,
            [&] -> awaitable<void>
   {
      auto ex = co_await this_coro::executor;
      tcp::resolver resolver(ex);

      ip::tcp::resolver::results_type endpoints;
      boost::system::error_code ec;
      if (auto addr = ip::make_address(host, ec); !ec)
      {
         auto ep = ip::tcp::endpoint{addr, port};
         endpoints = ip::tcp::resolver::results_type::create(ep, host, std::to_string(port));
      }
      else
      {
         auto flags = ip::tcp::resolver::numeric_service;
         endpoints = co_await resolver.async_resolve(host, std::to_string(port), flags);
      }

      ip::tcp::socket socket(ex);
      for (;;)
      {
         auto [ec, endpoint] = co_await asio::async_connect(socket, endpoints, as_tuple);
         if (!ec)
            break;
         co_await sleep(10ms);
      }
   },
            cancel_after(timeout, [](const std::exception_ptr& ep)
   {
      if (ep)
         std::println("{}", what(ep));
      exit(ep ? 1 : 0);
   }));

   io_context.run();
}
