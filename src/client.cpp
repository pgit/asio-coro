#include "asio-coro.hpp"

#include <boost/asio/error.hpp>

using namespace std::chrono;

awaitable<size_t> write(tcp::socket& socket, const std::size_t size)
{
   size_t total = 0;
   try
   {
      std::array<char, 1460> data;
      while (total < size)
      {
         size_t n = std::min(size - n, sizeof(data));
         co_await async_write(socket, buffer(data, n));
         total += n;
      }
   }
   catch (system_error& ex)
   {
      if (ex.code() == boost::system::errc::operation_canceled)
         ; // std::println("write: {} after writing {} bytes", ex.code().message(), Bytes(total));
      else
         ; // throw;
   }
   error_code ec;
   ec = socket.shutdown(boost::asio::socket_base::shutdown_send, ec);
   co_return total;
}

awaitable<size_t> write(tcp::socket& socket, nanoseconds duration)
{
   auto executor = co_await this_coro::executor;
   try
   {
      auto task = write(socket, std::numeric_limits<size_t>::max());
      co_return co_await co_spawn(executor, std::move(task), cancel_after(duration));
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
      std::array<char, 1460> data;
      for (;;)
      {
         total += co_await socket.async_read_some(buffer(data));
      }
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

awaitable<size_t> client(std::string host, uint16_t port, nanoseconds duration)
{
   auto executor = co_await this_coro::executor;
   tcp::resolver resolver(executor);

   // std::println("resolving {}:{} ...", host, port);
   auto flags = ip::tcp::resolver::numeric_service;
   auto endpoints = co_await resolver.async_resolve(host, std::to_string(port), flags);

   ip::tcp::socket socket(executor);
   auto endpoint = co_await asio::async_connect(socket, endpoints);

   std::println("connected to {}", socket.remote_endpoint());

   auto t0 = steady_clock::now();
   auto task = co_spawn(executor, write(socket, 5000), cancel_after(1s));
   auto [nwrite, nread] = co_await (write(socket, duration) && read(socket));
   auto dt = floor<milliseconds>(steady_clock::now() - t0);
   std::println("wrote {} and read {} in {}", Bytes(nwrite), Bytes(nread), dt);

   co_return nread;
}

struct Config
{
   size_t connections = 5;
   size_t threads = 1;
   std::chrono::milliseconds duration = 3s;
};

int main()
{
   Config config;
   io_context context;

   std::vector<std::future<size_t>> futures;
   futures.reserve(config.connections);
   for (size_t i = 0; i < futures.capacity(); ++i)
   {
      auto executor = config.threads > 1 ? any_io_executor(make_strand(context))
                                         : any_io_executor(context.get_executor());
      futures.emplace_back(
         co_spawn(std::move(executor), client("localhost", 55555, config.duration), use_future));
   }

   //
   // Finally, run the IO context until there is no pending operation left.
   //
   auto t0 = steady_clock::now();
   if (config.threads == 1)
      context.run();
   else
   {
      std::vector<std::thread> threads(config.threads);
      for (auto& thread : threads)
         thread = std::thread([&]() { context.run(); });
      for (auto& thread : threads)
         thread.join();
   }
   auto dt = floor<milliseconds>(steady_clock::now() - t0);

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

   std::println("Total bytes transferred: {} at {} MB/s", Bytes(total),
                total * 1000 / 1024 / 1024 / dt.count());
}
