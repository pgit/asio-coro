#include "asio-coro.hpp"

#include <boost/asio/error.hpp>

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
   Client(ClientConfig config) : config_(std::move(config)) { assert(config_.buffer_size > 0); }

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
            ; // throw;
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
            co_return co_await co_spawn(executor, std::move(task));
      }
      catch (system_error& ex)
      {
         std::println("write: {}", ex.code().message());
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

      ip::tcp::socket socket(executor);
      auto endpoint = co_await asio::async_connect(socket, endpoints);

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
   size_t connections = 5;
   size_t extraThreads = 0;
   std::chrono::milliseconds duration = 3s;
};

int main()
{
   Config config;
   io_context context;

   std::vector<Client> clients;
   std::vector<std::future<size_t>> futures;
   clients.reserve(config.connections);
   futures.reserve(config.connections);
   for (size_t i = 0; i < futures.capacity(); ++i)
   {
      auto executor = config.extraThreads > 1 ? any_io_executor(make_strand(context))
                                              : any_io_executor(context.get_executor());
      clients.emplace_back(ClientConfig{});
      futures.emplace_back(
         co_spawn(std::move(executor), clients.back().run("localhost", 55555), use_future));
   }

   //
   // Finally, run the IO context until there is no pending operation left.
   //
   auto t0 = steady_clock::now();
   std::vector<std::thread> threads(config.extraThreads);
   for (auto& thread : threads)
      thread = std::thread([&]() { context.run(); });
   context.run();
   for (auto& thread : threads)
      thread.join();

   //
   // Collect results.
   //
   size_t total = 0;
   for (auto& future : futures)
   {
      try
      {
         total += future.get();
      }
      catch (multiple_exceptions& mex)
      {
         std::println("{}, first is {}", mex.what(), what(mex.first_exception()));
      }
      catch (system_error& ex)
      {
         std::println("{}", ex.code().message());
      }
   }

   auto dt = std::max(1ms, floor<milliseconds>(steady_clock::now() - t0));
   std::println("Total bytes transferred: {} at {} MB/s", Bytes(total),
                total * 1000 / 1024 / 1024 / dt.count());
}
