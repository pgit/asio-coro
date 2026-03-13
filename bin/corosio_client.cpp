#include "formatters.hpp"

#include <boost/capy.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/corosio.hpp>
#include <boost/program_options.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <vector>

namespace corosio = boost::corosio;
namespace capy = boost::capy;
namespace po = boost::program_options;

using namespace std::chrono_literals;

struct ClientConfig
{
   size_t buffer_size = 64 * 1024;
   std::optional<size_t> size;
   std::optional<std::chrono::steady_clock::duration> duration = 1s;
};

capy::task<size_t> write_loop(corosio::tcp_socket& socket, ClientConfig config)
{
   std::vector<uint8_t> payload(config.buffer_size);
   std::iota(payload.begin(), payload.end(), uint8_t{0});

   size_t total = 0;
   size_t limit = config.size.value_or(std::numeric_limits<size_t>::max());
   std::optional<std::chrono::steady_clock::time_point> deadline;
   if (config.duration)
      deadline = std::chrono::steady_clock::now() + *config.duration;

   while (total < limit)
   {
      if (deadline && std::chrono::steady_clock::now() >= *deadline)
         break;

      size_t chunk = std::min(limit - total, payload.size());
      auto [wec, wn] = co_await capy::write(
         socket, capy::const_buffer(payload.data(), chunk));
      if (wec)
         break;

      total += wn;
   }

   socket.shutdown(corosio::tcp_socket::shutdown_send);

   co_return total;
}

capy::task<size_t> read_loop(corosio::tcp_socket& socket, size_t buffer_size)
{
   std::vector<char> buffer(buffer_size);
   size_t total = 0;
   for (;;)
   {
      auto [rec, rn] = co_await socket.read_some(
         capy::mutable_buffer(buffer.data(), buffer.size()));
      if (rec)
         break;

      total += rn;
   }

   co_return total;
}

struct SessionStats
{
   size_t written = 0;
   size_t read = 0;
   std::chrono::steady_clock::duration elapsed{};
   bool connected = false;
};

capy::task<SessionStats> run_session(corosio::io_context& ioc,
                                     ClientConfig config,
                                     std::string host,
                                     uint16_t port)
{
   SessionStats stats;
   corosio::resolver resolver(ioc);
   auto [resolve_ec, results] = co_await resolver.resolve(host, std::to_string(port));
   if (resolve_ec)
   {
      std::println(std::cerr, "ERROR: {}", resolve_ec.message());
      co_return stats;
   }
   if (results.empty())
   {
      std::println(std::cerr, "ERROR: Resolve returned no addresses");
      co_return stats;
   }

   corosio::tcp_socket socket(ioc);
   socket.open();

   boost::system::error_code last_ec;
   for (auto const& entry : results)
   {
      auto [ec] = co_await socket.connect(entry.get_endpoint());
      if (!ec)
      {
         last_ec.clear();
         break;
      }
      last_ec = ec;
      socket.close();
      socket.open();
   }

   if (last_ec)
   {
      std::println(std::cerr, "ERROR: {}", last_ec.message());
      co_return stats;
   }

   stats.connected = true;
   std::println("connected to: {}:{}", host, port);

   auto start = std::chrono::steady_clock::now();
   auto [total_written, total_read] = co_await capy::when_all(
      write_loop(socket, config),
      read_loop(socket, config.buffer_size));

   socket.close();
   stats.written = total_written;
   stats.read = total_read;
   stats.elapsed = std::chrono::steady_clock::now() - start;
   co_return stats;
}

capy::task<void> client_task(corosio::io_context& ioc,
                             ClientConfig config,
                             std::string host,
                             uint16_t port,
                             std::atomic<size_t>& total_bytes)
{
   auto stats = co_await run_session(ioc, config, std::move(host), port);
   if (stats.connected)
   {
      auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stats.elapsed);
      auto delta = static_cast<int64_t>(stats.read) - static_cast<int64_t>(stats.written);
      std::println("wrote {} and read {} (Δ {}) in {}ms",
                   Bytes(stats.written), Bytes(stats.read), delta, elapsed_ms.count());
   }
   total_bytes.fetch_add(stats.read, std::memory_order_relaxed);
}

struct Config
{
   std::string host = "127.0.0.1";
   uint16_t port = 55555;
   size_t connections = 1;
   size_t threads = std::thread::hardware_concurrency();
   double duration = 1.0;
};

int main(int argc, char* argv[])
{
   Config config;

   po::options_description desc("Allowed options");
   desc.add_options()
      ("help,h", "produce help message")
      ("host", po::value<std::string>(&config.host)->default_value(config.host),
       "host to connect to")
      ("port,p", po::value(&config.port)->default_value(config.port)->value_name("PORT"),
       "port number")
      ("connections,c",
       po::value(&config.connections)->default_value(config.connections)->value_name("N"),
       "number of connections")
      ("threads,t",
       po::value(&config.threads)->default_value(config.threads)->value_name("N"),
       "number of IO contexts to run in parallel")
      ("duration,d",
       po::value(&config.duration)->default_value(config.duration)->value_name("SECONDS"),
       "number of seconds to run before closing the connection");

   po::variables_map vm;
   try
   {
      po::store(po::parse_command_line(argc, argv, desc), vm);
      po::notify(vm);
   }
   catch (const po::error& ex)
   {
      std::cerr << "Command line error: " << ex.what() << "\n";
      return 1;
   }

   if (vm.count("help"))
   {
      desc.print(std::cout);
      return 1;
   }

   if (config.threads == 0)
   {
      std::cerr << "ERROR: number of threads must be at least 1\n";
      return 1;
   }

   if (config.connections == 0)
   {
      std::cerr << "ERROR: number of connections must be at least 1\n";
      return 1;
   }

   if (config.duration < 0.0)
   {
      std::cerr << "ERROR: duration must be non-negative\n";
      return 1;
   }

   config.threads = std::min(config.threads, config.connections);

   std::vector<corosio::io_context> io_contexts(config.threads);
   std::atomic<size_t> total_bytes{0};

   ClientConfig client_config;
   if (config.duration > 0.0)
   {
      auto duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
         std::chrono::duration<double>(config.duration));
      client_config.duration = duration;
   }
   else
   {
      client_config.duration.reset();
   }

   for (size_t i = 0; i < config.connections; ++i)
   {
      auto& ioc = io_contexts[i % io_contexts.size()];
      capy::run_async(ioc.get_executor())(
         client_task(ioc, client_config, config.host, config.port, total_bytes));
   }

   auto start = std::chrono::steady_clock::now();

   std::vector<std::jthread> threads;
   threads.reserve(io_contexts.size());
   for (size_t i = 1; i < io_contexts.size(); ++i)
      threads.emplace_back([&ioc = io_contexts[i]]() { ioc.run(); });

   io_contexts[0].run();

   auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
   auto ms = std::max<int64_t>(elapsed.count(), 1);
   auto mib_per_s = static_cast<double>(total_bytes.load()) * 1000.0 / 1024.0 / 1024.0 / ms;

   std::println("Total bytes echoed: {} at {} MiB/s", Bytes(total_bytes.load()),
                static_cast<int64_t>(mib_per_s));

   return 0;
}