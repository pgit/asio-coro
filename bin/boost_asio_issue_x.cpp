#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <print>

using namespace boost::asio;
using namespace boost::system;
using namespace experimental;
using namespace awaitable_operators;
using namespace std::chrono_literals;

using enum cancellation_type;

using Sleep = void(boost::system::error_code);

using Duration = std::chrono::nanoseconds;

template <BOOST_ASIO_COMPLETION_TOKEN_FOR(Sleep) CompletionToken>
static auto async_sleep(boost::asio::any_io_executor ex, Duration duration, CompletionToken&& token)
{
   return boost::asio::async_initiate<CompletionToken, Sleep>(
      [](auto handler, boost::asio::any_io_executor ex, Duration duration)
      {
         auto timer = std::make_shared<steady_timer>(ex, duration);
         timer->async_wait(consign(std::move(handler), timer));
      },
      token, std::move(ex), duration);
}

inline std::string what(error_code ec)
{
   return ec.message();
}

inline std::string what(const std::exception_ptr& ptr)
{
   if (!ptr)
      return "Completed";
   try
   {
      std::rethrow_exception(ptr);
   }
   catch (boost::system::system_error& ex)
   {
      return ex.code().message();
   }
}

constexpr auto token(cancellation_type type)
{
   return cancel_after(1ms, type, as_tuple(use_future));
}

awaitable<void> wrapped(Duration duration)
{
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());
   co_await async_sleep(co_await this_coro::executor, duration, deferred);
}

int main()
{
   boost::asio::io_context context;
   auto executor = context.get_executor();
   auto spawn_terminal = co_spawn(executor, async_sleep(executor, 2s, use_awaitable), token(terminal));
   auto spawn_total = co_spawn(executor, async_sleep(executor, 2s, use_awaitable), token(total));
   auto wrapped_total = co_spawn(executor, wrapped(2s), token(total));
   cancellation_signal signal;
   auto direct_total = async_sleep(executor, 2s, bind_cancellation_slot(signal.slot(), as_tuple(use_future)));
   signal.emit(total);
   context.run();
   std::println("spawn_terminal: {}", what(std::get<std::exception_ptr>(spawn_terminal.get())));
   std::println("spawn_total: {}", what(std::get<std::exception_ptr>(spawn_total.get())));
   std::println("wrapped_total: {}", what(std::get<std::exception_ptr>(wrapped_total.get())));
   std::println("direct_total: {}", what(std::get<error_code>(direct_total.get())));
}
