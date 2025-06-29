#include "asio-coro.hpp"
#include "utils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <print>
#include <string_view>

using namespace ::testing;

// =================================================================================================

class Echo : public testing::Test
{
public:
   void SetUp() override
   {
      acceptor.emplace(context, tcp::endpoint{tcp::v6(), 0});
      ASSERT_GT(acceptor->local_endpoint().port(), 0);

      //
      // The server is spawned with a 1s timeout -- we expect the client to have connected by then.
      //
      co_spawn(context, server(), cancel_after(1s, log_exception("server")));

      //
      // Spawn the client with the local server endpoint. If an exception is thrown within the
      // client, that will be captured by the future.
      //
      clientFuture = co_spawn(context, client(acceptor->local_endpoint()), use_future);
   }

   awaitable<void> session(tcp::socket socket)
   {
      std::array<char, 64 * 1024> data;
      for (;;)
      {
#if 1
         auto n = co_await socket.async_read_some(buffer(data));
#else
         auto [ec, n] = co_await socket.async_read_some(buffer(data), as_tuple);
         if (ec == boost::system::errc::operation_canceled)
            break;
         else if (ec)
            throw system_error(ec);
#endif
         co_await async_write(socket, buffer(data, n));
      }
   }

   awaitable<void> server()
   {
      for (;;)
      {
         auto socket = co_await acceptor->async_accept();
         std::println("connection from {}", socket.remote_endpoint());
         co_spawn(context, session(std::move(socket)),
                  [this](std::exception_ptr ep)
                  {
                     std::println("server session: {}", what(ep));
                     if (ep)
                        on_server_session_error(code(ep));
                  });
      }
   }

   MOCK_METHOD(void, on_server_session_error, (error_code ec), ());

   awaitable<void> client(tcp::endpoint endpoint)
   {
      auto executor = co_await this_coro::executor;

      ip::tcp::socket socket(executor);
      co_await socket.async_connect(endpoint);
      std::println("connected to {}", socket.remote_endpoint());

      auto task = test(std::move(socket));
      auto [ep] = co_await co_spawn(executor, std::move(task), cancel_after(timeout, as_tuple));
      acceptor->cancel();
      if (ep)
         std::rethrow_exception(ep);
   }

   static awaitable<void> sleep(std::chrono::milliseconds duration)
   {
      steady_timer timer(co_await this_coro::executor);
      timer.expires_after(100ms);
      co_await timer.async_wait();
   }

   void run()
   {
      ASSERT_TRUE(!!test);
      using namespace std::chrono;
      auto t0 = steady_clock::now();
      ::run(context);
      auto t1 = steady_clock::now();
      this->runtime = floor<milliseconds>(t1 - t0);
      clientFuture.get(); // may throw, to be catched by EXPECT_THROW(...)
   }

   template <TestConcept Test>
   void run(Test test)
   {
      this->test = std::move(test);
      run();
   }

   static awaitable<void> noop(tcp::socket) { co_return; }

protected:
   io_context context;
   std::optional<tcp::acceptor> acceptor;
   std::function<awaitable<void>(tcp::socket socket)> test = noop;
   std::chrono::milliseconds runtime, timeout = 1s;

private:
   std::future<void> clientFuture;
};

// -------------------------------------------------------------------------------------------------

TEST_F(Echo, WHEN_no_test_has_been_set_THEN_test_completes)
{
   run();
}

TEST_F(Echo, WHEN_socket_is_shut_down_THEN_test_completes)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      socket.shutdown(socket_base::shutdown_send);
      co_return;
   };
   EXPECT_CALL(*this, on_server_session_error(make_error_code(error::misc_errors::eof)));
   EXPECT_NO_THROW(run());
}

TEST_F(Echo, WHEN_client_takes_too_long_THEN_timeout_hits)
{
   timeout = 100ms;
   test = [](tcp::socket socket) -> awaitable<void>
   {
      co_await sleep(5s);
   };
   EXPECT_CALL(*this, on_server_session_error(make_error_code(error::misc_errors::eof)));
   EXPECT_NO_THROW(run());
   EXPECT_GE(runtime, timeout);
   EXPECT_LT(runtime, 1s);
}

TEST_F(Echo, WHEN_send_hello_THEN_receive_echo)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      const auto hello = "Hello, World!"sv;
      co_await socket.async_send(buffer(hello));
      socket.shutdown(socket_base::shutdown_send);

      std::array<char, 64 * 1024> data;
      // auto n = co_await socket.async_read_some(buffer(data));
      auto [ec, n] = co_await async_read(socket, buffer(data), as_tuple);
      EXPECT_EQ(ec, asio::error::eof);
      EXPECT_EQ(n, hello.length());
      EXPECT_EQ(std::string_view(data.data(), n), hello);
   };
   EXPECT_CALL(*this, on_server_session_error(make_error_code(error::misc_errors::eof)));
   EXPECT_NO_THROW(run());
}

TEST_F(Echo, WHEN_send_hello_in_chunks_THEN_receive_echo)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      auto executor = co_await this_coro::executor;

      const auto hello = "Hello, World!"sv;
      awaitable<void> sender = co_spawn(
         executor,
         [&]() -> awaitable<void>
         {
            co_await socket.async_send(buffer(hello.substr(0, 5)));
            co_await sleep(10ms);
            co_await socket.async_send(buffer(hello.substr(5)));
            socket.shutdown(socket_base::shutdown_send);
         },
         use_awaitable);

      //
      // In contrast to async_read_some(), async_read() tries to to fill the buffer completely,
      // so it will not return early after it has received the first chunk of the message.
      //
      auto receiver = [&]() -> awaitable<void>
      {
         std::array<char, 64 * 1024> data;
         auto [ec, n] = co_await async_read(socket, buffer(data), as_tuple);
         EXPECT_EQ(ec, asio::error::eof);
         EXPECT_EQ(n, hello.length());
         EXPECT_EQ(std::string_view(data.data(), n), hello);
      };

      co_await (std::move(sender) && std::move(receiver)());
   };
   EXPECT_NO_THROW(run());
}

TEST_F(Echo, WHEN_socket_closed_THEN_read_fails)
{
   test = [](tcp::socket socket) -> awaitable<void>
   {
      socket.shutdown(socket_base::shutdown_send);

      std::array<char, 64 * 1024> data;
      EXPECT_THROW(co_await socket.async_read_some(buffer(data)), system_error);
   };
   EXPECT_CALL(*this, on_server_session_error(make_error_code(error::misc_errors::eof)));
   EXPECT_NO_THROW(run());
}

// =================================================================================================
