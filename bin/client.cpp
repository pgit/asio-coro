#include "asio-coro.hpp"

#include <boost/asio/error.hpp>
#include <boost/program_options.hpp> // Add this include

#include <iostream>
#include <ranges>
#include <thread>

using std::chrono::milliseconds;
using std::chrono::steady_clock;

struct ClientConfig
{
   size_t buffer_size = 64 * 1024;
   std::optional<size_t> size;
   std::optional<steady_clock::duration> time = 1s;
};

class Client
{
public:
   explicit Client(ClientConfig config) : config_(config) { assert(config_.buffer_size > 0); }

private:
   /*
    * Write as much data to the socket as possible, until either the configured limit has been
    * reached or this coroutine is cancelled. We don't care about the data that is written, so
    * that is just an uninitialized vector.
    */
   awaitable<size_t> write_loop(tcp::socket& socket)
   {
      size_t total = 0;
      size_t size = config_.size ? *config_.size : std::numeric_limits<size_t>::max();
      try
      {
         std::vector<char> data(config_.buffer_size);
         while (total < size)
         {
            size_t n = std::min(size - n, data.size());
            co_await async_write(socket, buffer(data, n));
            total += n;
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
      try
      {
         auto task = write_loop(socket);
         if (config_.time)
            co_return co_await co_spawn(executor, std::move(task), cancel_after(*config_.time));
         else
            co_return co_await std::move(task); // co_spawn(executor, std::move(task));
      }
      catch (system_error& ex)
      {
         // std::println("write: {}", ex.code().message());
         throw;
      }
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
            std::println("read: {}", ex.code().message());
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

      // std::println("resolving {}:{} ...", host, port);
      auto flags = ip::tcp::resolver::numeric_service;
      auto endpoints = co_await resolver.async_resolve(host, std::to_string(port), flags);

      auto range = endpoints | std::views::transform([](const auto& x)
      { return std::format("{}", x.endpoint()); });
      // auto range = endpoints | std::views::transform([](const auto& x) { return x.endpoint(); });
      // std::println("endpoints: {}", range);

      ip::tcp::socket socket(executor);
      auto endpoint = co_await asio::async_connect(socket, endpoints);
      std::println("connected to: {}", endpoint);

      // std::println("connected to {}", socket.remote_endpoint());

      auto t0 = steady_clock::now();
      auto [nwrite, nread] = co_await (write(socket) && read(socket));
      auto dt = floor<milliseconds>(steady_clock::now() - t0);
      std::println("wrote {} and read {} in {}", Bytes(nwrite), Bytes(nread), dt);

      co_return nread;
   }
};

struct Config
{
   std::string host = "localhost";
   uint16_t port = 55555;
   size_t connections = 1;
   size_t extraThreads = 0;
   std::chrono::milliseconds duration = 3s;
};

int main(int argc, char* argv[])
{
   namespace po = boost::program_options;

   Config config;

   // Define and parse command line options
   po::options_description desc("Allowed options");
   desc.add_options()("help,h", "produce help message");
   desc.add_options()("host", po::value<std::string>(&config.host)->default_value("localhost"),
                      "host to connect to");
   desc.add_options()("port,p", po::value<uint16_t>(&config.port)->default_value(55555),
                      "port number");
   desc.add_options()("connections,c", po::value<size_t>(&config.connections)->default_value(1),
                      "number of connections");
   desc.add_options()("extraThreads,t", po::value<size_t>(&config.extraThreads)->default_value(0),
                      "number of extra threads");

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
      return 1;
   }

   io_context context;
   any_io_executor executor = context.get_executor();

   std::vector<Client> clients;
   clients.reserve(config.connections);
   auto futures = std::views::iota(size_t{0}, clients.capacity()) |
                  std::views::transform([&, executor](size_t) mutable
   {
      if (config.extraThreads)
         executor = make_strand(executor);
      clients.emplace_back(ClientConfig{});
      return co_spawn(executor, clients.back().run(config.host, config.port), as_tuple(use_future));
   }) | std::ranges::to<std::vector>();

   //
   // Finally, run the IO context until there is no pending operation left.
   //
   auto t0 = steady_clock::now();
   {
      auto threads =
         std::views::iota(size_t{0}, config.extraThreads) |
         std::views::transform([&](size_t) { return std::jthread([&]() { context.run(); }); }) |
         std::ranges::to<std::vector>();
      context.run();
   }

   //
   // Collect results.
   //
   size_t total = 0;
   for (auto& future : futures)
   {
      auto [ec, bytes] = future.get();
      total += bytes;
      if (ec)
         std::println("ERROR: {}", what(ec));
   }

   auto dt = std::max(1ms, floor<milliseconds>(steady_clock::now() - t0));
   std::println("Total bytes echoed: {} at {} MB/s", Bytes(total),
                total * 1000 / 1024 / 1024 / dt.count());
}
