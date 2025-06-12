#include <map>
#include "asio-coro.hpp"

awaitable<void> session(tcp::socket& socket)
{
   std::array<char, 64 * 1024> data;
   for (;;)
   {
      size_t n = co_await socket.async_read_some(buffer(data));
      co_await async_write(socket, buffer(data, n));
   }
}

awaitable<void> server(tcp::acceptor acceptor)
{
   std::mutex mutex;
   std::map<size_t, tcp::socket> sockets;

   auto executor = co_await this_coro::executor;
   signal_set signals(executor, SIGINT, SIGTERM);
   signals.async_wait(
      [&](error_code error, auto signum)
      {
         std::println(" INTERRUPTED (signal {})", signum);
         acceptor.cancel();
         auto lock = std::lock_guard(mutex);
         for (auto& socket : sockets)
            socket.second.cancel();
      });


   //
   // Main accept loop. For each new connection, record the socket in a map for cancellation.
   // The session() coroutine is passed a reference only.
   //
   for (size_t id = 0;; ++id)
   {
      auto [ec, socket] = co_await acceptor.async_accept(as_tuple);
      if (ec)
      {
         std::println("accept: {}", ec.message());
         break;
      }

      auto lock = std::lock_guard(mutex);
      auto [it, inserted] = sockets.emplace(id, std::move(socket));
      std::println("number of active sessions: {}", sockets.size());
      co_spawn(executor, session(it->second),
               [&, id](std::exception_ptr ep)
               {
                  auto lock = std::lock_guard(mutex);
                  sockets.erase(id);
                  std::println("session {} finished: {}, {} sessions left", id, what(ep),
                               sockets.size());
               });
   }

   std::println("-----------------------------------------------------------------------------");

   //
   // Simple busy waiting until all coroutines have finished.
   //
   // If cancellation takes longer (for example, when doing a gracefull TLS disconnect), this
   // must be replaced by a better mechnism.
   //
   auto lock = std::unique_lock(mutex);
   while (!sockets.empty())
   {
      lock.unlock();
      co_await post(executor, asio::deferred);
      lock.lock();
   }

   std::println("==============================================================================");
}

int main()
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   std::thread thread([&]() { context.run(); });
   context.run();
   thread.join();
}
