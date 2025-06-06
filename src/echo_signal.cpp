#include <boost/asio/error.hpp>
#include "asio-coro.hpp"

awaitable<void> session(tcp::socket socket)
{
   std::println("new connection from {}", socket.remote_endpoint());

   size_t total = 0;
   try
   {
      std::array<char, 1460> data;
      for (;;)
      {
         std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
         total += n;
         co_await async_write(socket, asio::buffer(data, n), use_awaitable);
      }
   }
   catch (const boost::system::system_error& ec)
   {
      if (ec.code() != asio::error::eof)
         throw;
   }

   std::println("echoed {} total", Bytes(total));
}

awaitable<void> server(tcp::endpoint endpoint)
{
   auto executor = co_await this_coro::executor;
   tcp::acceptor acceptor(executor, endpoint);

   signal_set signals(executor, SIGINT, SIGTERM);
   signals.async_wait(
      [&](error_code error, auto signal)
      {
         std::println(" INTERRUPTED (signal {})", signal);
         acceptor.cancel();
      });

   std::println("listening on {}", acceptor.local_endpoint());
   for (;;)
   {
      tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, session(std::move(socket)), detached);
   }
}

int main()
{
   io_context io_context;
   co_spawn(io_context, server({tcp::v6(), 55555}), log_exception("server"));
   std::println("running IO context...");
   io_context.run();
   std::println("running IO context... done");
}
