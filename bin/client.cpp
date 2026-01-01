#include "asio-coro.hpp"
#include "literals.hpp"
#include "run.hpp"

#include <boost/asio/error.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <ranges>
#include <thread>

using std::chrono::milliseconds;
using std::chrono::steady_clock;

namespace po = boost::program_options;

struct ClientConfig
{
   size_t buffer_size = 64_k;
   std::optional<size_t> size;
   std::optional<steady_clock::duration> duration = 1s;
};

class Client
{
public:
   explicit Client(ClientConfig config) : config_(config) { assert(config_.buffer_size > 0); }

private:
   /*
    * Write as much data to the socket as possible, until either the configured limit has been
    * reached or this coroutine is cancelled.
    */
   awaitable<size_t> write_loop(tcp::socket& socket)
   {
      size_t total = 0;
      size_t size = config_.size ? *config_.size : std::numeric_limits<size_t>::max();
      try
      {
         const auto data = std::views::iota(uint8_t{0}) | // 0..255, 0..255, ...
                           std::views::take(config_.buffer_size) | //
                           std::ranges::to<std::vector>();
         while (total < size)
         {
            const size_t n = std::min(total - size, data.size());
            total += co_await socket.async_write_some(buffer(data, n));
         }
      }
      catch (system_error& ex)
      {
         // std::println("write: {} after writing {} bytes", ex.code().message(), Bytes(total));
         if (ex.code() == boost::system::errc::operation_canceled)
            ;
         else
            throw;
      }

      error_code ec;
      ec = socket.shutdown(boost::asio::socket_base::shutdown_send, ec);
      co_return total;
   }

   awaitable<size_t> write(tcp::socket& socket)
   {
      auto executor = co_await this_coro::executor;
      if (config_.duration)
         co_return co_await co_spawn(executor, write_loop(socket), cancel_after(*config_.duration));
      else
         co_return co_await write_loop(socket);
   }

   awaitable<size_t> read(tcp::socket& socket)
   {
      size_t total = 0;
      try
      {
         std::vector<char> data(config_.buffer_size);
         for (;;)
            total += co_await socket.async_read_some(buffer(data));
      }
      catch (system_error& ex)
      {
         if (ex.code() == asio::error::eof || ex.code() == boost::system::errc::operation_canceled)
            ; // std::println("read: {} after reading {} bytes", ex.code().message(), Bytes(total));
         else
         {
            std::println("read: {} after reading {} bytes", ex.code().message(), Bytes(total));
            throw;
         }
      }
      co_return total;
   }

   ClientConfig config_;

public:
   awaitable<size_t> run(std::string host, uint16_t port)
   {
      auto executor = co_await this_coro::executor;
      tcp::resolver resolver(executor);

      ip::tcp::resolver::results_type endpoints;
      boost::system::error_code ec;
      if (auto addr = ip::make_address(host, ec); !ec)
      {
         auto ep = ip::tcp::endpoint{addr, port};
         endpoints = ip::tcp::resolver::results_type::create(ep, host, std::to_string(port));
      }
      else
      {
         std::println("resolving {}:{} ...", host, port);
         auto flags = ip::tcp::resolver::numeric_service;
         endpoints = co_await resolver.async_resolve(host, std::to_string(port), flags);
      }
#if 0 
      auto range = endpoints | std::views::transform([](const auto& x)
      { return std::format("{}", x.endpoint()); });
      std::println("endpoints: {}", range);
#endif

      ip::tcp::socket socket(executor);
      auto endpoint = co_await asio::async_connect(socket, endpoints);
      std::println("connected to: {}", endpoint);

      // std::println("connected to {}", socket.remote_endpoint());

      auto t0 = steady_clock::now();
      auto [nwrite, nread] = co_await (write(socket) && read(socket));
      auto dt = floor<milliseconds>(steady_clock::now() - t0);
      std::println("wrote {} and read {} (\u0394 {}) in {}", //
                   Bytes(nwrite), Bytes(nread), ssize_t(nread) - ssize_t(nwrite), dt);

      co_return nread;
   }
};

struct Config
{
   std::string host = "127.0.0.1";
   uint16_t port = 55555;
   size_t connections = 1;
   size_t threads = std::thread::hardware_concurrency();
   double duration = 1;
};

int main(int argc, char* argv[])
{
   Config config;
   bool debug = false;

   //
   // Define and parse command line options.
   //
   po::options_description desc("Allowed options");
   desc.add_options()("help,h", "produce help message");
   desc.add_options()("host", po::value<std::string>(&config.host)->default_value(config.host),
                      "host to connect to");
   desc.add_options()("port,p",
                      po::value(&config.port)->default_value(config.port)->value_name("PORT"),
                      "port number");
   desc.add_options()(
      "connections,c",
      po::value(&config.connections)->default_value(config.connections)->value_name("N"),
      "number of connections");
   desc.add_options()("threads,t",
                      po::value(&config.threads)->default_value(config.threads)->value_name("N"),
                      "number of IO contexts to run in parallel");
   desc.add_options()(
      "duration,d",
      po::value(&config.duration)->default_value(config.duration)->value_name("SECONDS"),
      "number of seconds to run the test before closing the connection");
   desc.add_options()("debug", po::bool_switch(&debug),
                      "enable debug mode (single threaded with additional logging)");

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

   if (config.threads == 0)
   {
      std::println("ERROR: number of threads must be at least 1");
      return 1;
   }

   if (config.duration < 0.0)
   {
      std::println("ERROR: duration must be non-negative");
      return 1;
   }

   if (debug)
   {
      config.threads = 1;
      std::println("DEBUG mode enabled");
   }

   //
   // Create IO context with the configured number of concurrent connections.
   //
   config.threads = std::min(config.threads, config.connections);
   std::vector<io_context> io_contexts(config.threads);

   std::vector<Client> clients;
   clients.reserve(config.connections);

   auto futures = std::views::iota(size_t{0}, clients.capacity()) |
                  std::views::transform([&](size_t i) mutable
   {
      auto executor = io_contexts[i % io_contexts.size()].get_executor();
      auto durationDouble = std::chrono::duration<double>(config.duration);
      auto duration = duration_cast<steady_clock::duration>(durationDouble);
      clients.emplace_back(ClientConfig{.duration = duration});
      return co_spawn(executor, clients.back().run(config.host, config.port), as_tuple(use_future));
   }) | std::ranges::to<std::vector>();

   //
   // Finally, run the configured number of IO contexts until there is no pending operation left.
   //
   if (debug)
   {
      assert(io_contexts.size() == 1);
      runDebug(io_contexts[0]);
      exit(0);
   }
   else
   {
      auto t0 = steady_clock::now();
      for (auto& context : io_contexts)
         std::jthread([&context]() { context.run(); }).detach();

      size_t total = 0;
      for (auto& future : futures)
      {
         auto [ec, bytes] = future.get();
         total += bytes;
         if (ec)
            std::println("ERROR: {}", what(ec));
      }

      auto dt = std::max(1ms, floor<milliseconds>(steady_clock::now() - t0));
      std::println("Total bytes echoed: {} at {} MiB/s", Bytes(total),
                   total * 1000 / 1024 / 1024 / dt.count());
   }
}
