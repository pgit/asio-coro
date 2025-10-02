#include "formatters.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

using namespace boost::asio;
using namespace experimental::awaitable_operators;
using ip::tcp;

awaitable<size_t> forward(tcp::socket& from, tcp::socket& to)
{
   size_t total = 0;

   try
   {
      std::array<char, 64 * 1024> data;
      for (;;)
      {
         auto n = co_await from.async_read_some(buffer(data));
         total += n;
         co_await async_write(to, buffer(data, n));
      }
   }
   catch (boost::system::system_error& ex)
   {
      if (ex.code() != error::eof)
         throw;
   }

   to.shutdown(tcp::socket::shutdown_send);
   co_return total;
}

awaitable<void> session(tcp::socket downstream)
{
   auto executor = co_await this_coro::executor;

   auto flags = tcp::resolver::numeric_service;
   tcp::resolver resolver(executor);
   auto endpoints = co_await resolver.async_resolve("localhost", "55555", flags);

   tcp::socket upstream(executor);
   auto endpoint = co_await async_connect(upstream, endpoints);

   auto [up, down] = co_await (forward(downstream, upstream) && forward(upstream, downstream));
   std::println("forwarded {} upstream and {} downstream", Bytes(up), Bytes(down));
}

awaitable<void> server(tcp::acceptor a)
{
   for (;;)
      co_spawn(a.get_executor(), session(co_await a.async_accept()), detached);
}

int main()
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55554}}), detached);
   context.run();
}
