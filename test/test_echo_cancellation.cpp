#include "asio-coro.hpp"
#include "literals.hpp"
#include "stream_utils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <boost/beast/core/stream_traits.hpp>

#include <deque>
#include <map>
#include <random>
#include <ranges>

using enum cancellation_type;
using namespace experimental::awaitable_operators;

namespace rv = std::ranges::views;

using namespace ::testing;

// =================================================================================================

awaitable<void> echo(tcp::socket& socket)
{
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());

   std::array<char, 64_k> data;
   for (;;)
   {
      size_t n = co_await socket.async_read_some(buffer(data));
      co_await async_write(socket, buffer(data, n));
   }
}

awaitable<void> shutdown(tcp::socket& socket)
{
   // std::println("shutdown...");
   co_await async_write(socket, buffer("goodbye\n"sv));
   static thread_local std::mt19937 rng{std::random_device{}()};
   static thread_local std::uniform_int_distribution<int> dist(0, 100);
   co_await sleep(std::chrono::milliseconds(dist(rng)));
   socket.shutdown(boost::asio::socket_base::shutdown_both);
   // std::println("shutdown... done");
   co_return;
}

/**
 * Run echo session on given socket.
 *
 * On 'total' and 'partial' cancellation, stops echo loop and performs a graceful shutdown.
 * On 'terminal' cancellation, stops everything immediately and closes the socket.
 *
 * During graceful shutdown, only 'terminal' cancellation is accepted and causes the socket to be
 * closed immediately, as well.
 */
awaitable<void> session(tcp::socket socket)
{
   auto cs = co_await this_coro::cancellation_state;
   auto ex = co_await this_coro::executor;

   //
   // For maximum flexibility, allow all cancellation types (total, partial terminal) and
   // don't throw when suspending the after cancellation. With this setup, we run the actual echo
   // task.
   //
   co_await this_coro::throw_if_cancelled(false);
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());

   // Finally, run session. Catch errors and re-throw anything that is not about cancellation.
   auto [ep] = co_await co_spawn(ex, echo(socket), as_tuple);
   std::println("session: {}", what(ep));
   if (ep && code(ep) != error::eof && cs.cancelled() == none)
      throw system_error{code(ep)};

   // React to terminal cancellation immediately.
   if ((cs.cancelled() & cancellation_type::terminal) != cancellation_type::none)
      throw system_error{error::operation_aborted};

   // During shutdown, react to 'terminal' cancellation only.
   co_await this_coro::reset_cancellation_state(enable_terminal_cancellation());
   co_await shutdown(socket);
}

// -------------------------------------------------------------------------------------------------

awaitable<void> server(tcp::acceptor acceptor)
{
   auto cs = co_await this_coro::cancellation_state;
   auto ex = co_await this_coro::executor;

   co_await this_coro::throw_if_cancelled(false);

   //
   // The 'server_alive' flag and the assertion checking it in the completion handle of the sessions
   // doesn't serve any functional purpose except making sure we have structured concurrency.
   //
   bool server_alive = true;
   auto scope_exit = make_scope_exit([&]() { server_alive = false; });

   //
   // The fact that cancellation_signal is not copyable is not surprising, but it is also not
   // movable. This makes it a little difficult to handle, and we have to use a unique ptr.
   //
   std::map<size_t, std::unique_ptr<cancellation_signal>> sessions;

   //
   // We use a channel to wait for sessions to complete. This similar to how you would do with
   // a std::condition_variable.
   //
   experimental::channel<void(error_code)> channel(ex);

   //
   // Main accept loop.
   //
   // For each accepted connection, move the socket into a new coroutine. For cancellation, create
   // a cancellation signal and bind it's slot to the completion handler of the coroutine.
   //
   for (size_t id = 0;;)
   {
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      auto [ec, socket] = co_await acceptor.async_accept(as_tuple);

      if (!ec)
      {
         auto [it, _] = sessions.emplace(id, std::make_unique<cancellation_signal>());
         std::println("session {} created, number of active sessions: {}", id, sessions.size());
         co_spawn(ex, session(std::move(socket)),
                  bind_cancellation_slot(it->second->slot(), [&, id](const std::exception_ptr& ep)
         {
            assert(server_alive);
            sessions.erase(id);
            std::println("session {} finished with {}, {} sessions left", //
                         id, what(ep), sessions.size());
            std::ignore = channel.try_send(error_code{});
         }));
         ++id;
      }

      if (cs.cancelled() == cancellation_type::total)
      {
         // std::println("forwarding '{}' to {} sessions", cs.cancelled(), sessions.size());
         for (auto& session : sessions)
            session.second->emit(cs.cancelled());
         continue;
      }
      else if (ec || cs.cancelled() != cancellation_type::none)
      {
         std::println("accept: {}", ec.message());
         break;
      }
   }

   std::println("-----------------------------------------------------------------------------");

   //
   // Forward cancellation to spawned coroutines.
   //
   std::println("forwarding '{}' to {} sessions", cs.cancelled(), sessions.size());
   for (auto& session : sessions)
      session.second->emit(cs.cancelled());

   std::println("-----------------------------------------------------------------------------");

   //
   // Wait until all sessions have finished.
   //
   // The cannel is notified every time after a session is removed.
   //
   std::println("server: waiting for sessions to complete...");
   while (!sessions.empty())
   {
      co_await this_coro::reset_cancellation_state(enable_terminal_cancellation());
      std::ignore = co_await channel.async_receive(as_tuple);
      if (cs.cancelled() != cancellation_type::none)
      {
         std::println("forwarding '{}' to {} sessions", cs.cancelled(), sessions.size());
         for (auto& session : sessions)
            session.second->emit(cs.cancelled());
      }
   }
   std::println("server: waiting for sessions to complete... done");

   std::println("==============================================================================");
}

awaitable<void> signal_handling(cancellation_signal& signal)
{
   signal_set signals(co_await this_coro::executor, SIGINT, SIGTERM, SIGTSTP);
   for (;;)
   {
      auto signum = co_await signals.async_wait();
      std::println(" {}", strsignal(signum));

      switch (signum)
      {
      case SIGTSTP:
         signal.emit(cancellation_type::total);
         break;
      case SIGINT:
         signal.emit(cancellation_type::partial);
         break;
      case SIGTERM:
         signal.emit(cancellation_type::terminal);
         break;
      }
   }
}

awaitable<void> with_signal_handling(awaitable<void> task)
{
   cancellation_signal signal;
   co_await (signal_handling(signal) ||
             co_spawn(co_await this_coro::executor, std::move(task),
                      bind_cancellation_slot(signal.slot(), use_awaitable)));
}

// =================================================================================================

class EchoCancellation : public testing::Test
{
public:
   void SetUp() override
   {
      tcp::acceptor acceptor(executor, tcp::endpoint{tcp::v6(), 0});
      endpoint = acceptor.local_endpoint();
      ASSERT_GT(endpoint.port(), 0);
      std::println("listening on {:c}", endpoint);

      co_spawn(executor, with_signal_handling(::server(std::move(acceptor))) && client(endpoint),
               detached);
   }

   MOCK_METHOD(void, on_server_session_error, (error_code ec), ());

   awaitable<void> client(tcp::endpoint endpoint)
   {
      auto ex = co_await this_coro::executor;

      ip::tcp::socket socket(ex);
      co_await socket.async_connect(endpoint);
      std::println("connected to {1:c}, local endpoint {0:c}", socket.local_endpoint(),
                   socket.remote_endpoint());

      auto task = test(std::move(socket));
      auto [ep] = co_await co_spawn(ex, std::move(task), as_tuple);

      ::kill(getpid(), SIGTERM);
   }

   void TearDown() override
   {
      context.run();
      // run(context);
   }

private:
   io_context context;

protected:
   any_io_executor executor{context.get_executor()};
   tcp::endpoint endpoint;
   std::function<awaitable<void>(tcp::socket socket)> test = noop;

   static awaitable<void> noop(tcp::socket) { co_return; }
};

// -------------------------------------------------------------------------------------------------

TEST_F(EchoCancellation, WHEN_client_shuts_down_gracefully_THEN_server_shuts_down_gracefully)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      socket.shutdown(socket_base::shutdown_send);

      std::string str;
      auto [ec, n] = co_await async_read(socket, dynamic_buffer(str), as_tuple);
      EXPECT_EQ(ec, error::eof);
      EXPECT_EQ(str, "goodbye\n");
   };
}

TEST_F(EchoCancellation, WHEN_client_shuts_down_THEN_server_shuts_down_without_goodbye)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      socket.shutdown(socket_base::shutdown_both);

      std::string str;
      auto [ec, n] = co_await async_read(socket, dynamic_buffer(str), as_tuple);
      EXPECT_EQ(ec, error::eof);
      EXPECT_EQ(str, "");
   };
}

// -------------------------------------------------------------------------------------------------

awaitable<std::string> read(tcp::socket& socket, size_t bytes)
{
   std::string str(bytes, '\0');
   auto [ec, n] = co_await async_read(socket, buffer(str), as_tuple);
   if (ec == error::eof)
      str.resize(n);
   else if (ec)
      throw system_error{ec};
   co_return str;
}

awaitable<std::string> read_until_eof(tcp::socket& socket)
{
   std::string str;
   auto [ec, n] = co_await async_read(socket, dynamic_buffer(str), as_tuple);
   if (ec && ec != error::eof)
      throw system_error{ec};
   co_return str;
}

// -------------------------------------------------------------------------------------------------

TEST_F(EchoCancellation, WHEN_send_message_THEN_receive_echo)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      constexpr auto hello = "Hello, World!"sv;
      co_await socket.async_send(buffer(hello));
      EXPECT_EQ(co_await read(socket, hello.size()), hello);
      socket.shutdown(socket_base::shutdown_send);
      EXPECT_EQ(co_await read_until_eof(socket), "goodbye\n");
   };
}

TEST_F(EchoCancellation, WHEN_shutdown_immediately_THEN_receive_echo_and_goodbye)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      constexpr auto hello = "Hello, World!"sv;
      co_await socket.async_send(buffer(hello));
      socket.shutdown(socket_base::shutdown_send);
      EXPECT_EQ(co_await read_until_eof(socket), "Hello, World!goodbye\n");
   };
}

TEST_F(EchoCancellation, WHEN_send_sigtstp_THEN_connection_is_closed_gracefully)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      ::kill(getpid(), SIGTSTP);
      EXPECT_EQ(co_await read_until_eof(socket), "goodbye\n");
   };
}

TEST_F(EchoCancellation, WHEN_send_sigint_THEN_connection_is_closed_gracefully)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      ::kill(getpid(), SIGINT);
      EXPECT_EQ(co_await read_until_eof(socket), "goodbye\n");
   };
}

TEST_F(EchoCancellation, WHEN_send_sigterm_THEN_connection_is_closed_immediately)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      ::kill(getpid(), SIGTERM);
      EXPECT_EQ(co_await read_until_eof(socket), "");
   };
}

// =================================================================================================

TEST_F(EchoCancellation, DISABLED_WHEN_backpressure_THEN_connection_is_closed_immediately)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      auto ex = co_await this_coro::executor;
      auto [ep, n] = co_await co_spawn(ex, write(socket, rv::iota(0)), cancel_after(1s, as_tuple));
      socket.shutdown(socket_base::shutdown_send);
      std::println("echoed {} bytes", n);
      EXPECT_EQ((co_await read_until_eof(socket)).size(), n + 8);
   };
}

// =================================================================================================

TEST_F(EchoCancellation, WHEN_many_clients_THEN_handles_all_gracefully)
{
   test = [this](tcp::socket) -> awaitable<void>
   {
      using boost::asio::experimental::promise;
      using boost::asio::experimental::use_promise;

      auto ex = co_await this_coro::executor;

      constexpr std::size_t concurrency = 100;
      constexpr std::size_t total_clients = 1000;

      std::deque<promise<void(std::exception_ptr)>> running;
      auto spawn_client = [this, ex]() -> promise<void(std::exception_ptr)>
      {
         return co_spawn(ex, [this]() -> awaitable<void>
         {
            ip::tcp::socket socket(co_await this_coro::executor);
            co_await socket.async_connect(endpoint);
            co_await socket.async_send(buffer("Hello, World!"sv));
            socket.shutdown(socket_base::shutdown_send);
            EXPECT_THAT(co_await read_until_eof(socket), EndsWith("goodbye\n"));
         }, use_promise);
      };

      for (std::size_t i = 0; i < concurrency; ++i)
         running.push_back(spawn_client());

      std::size_t launched = running.size();
      while (launched < total_clients)
      {
         co_await std::move(running.front());
         running.pop_front();
         running.push_back(spawn_client());
         ++launched;
      }

      while (!running.empty())
      {
         co_await std::move(running.front());
         running.pop_front();
      }
   };
}

// =================================================================================================
