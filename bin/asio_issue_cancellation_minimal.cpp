#include "asio-coro.hpp"
#include "formatters.hpp" // IWYU pragma: keep

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

using namespace boost::asio;
using namespace boost::system;
using namespace experimental;
using namespace std::chrono_literals;

using enum cancellation_type;

awaitable<void> task(cancellation_type filter)
{
   co_await this_coro::reset_cancellation_state([filter](cancellation_type type)
   {
      auto filtered = type & filter;
      if (filtered == none)
         std::println("FILTER({}): {} -> \x1b[1;31m{}\x1b[0m", filter, type, filtered);
      else
         std::println("FILTER({}): {} -> {}", filter, type, filtered);
      return filtered;
   });

   auto cs = co_await this_coro::cancellation_state;
   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(2s);

   std::println("waiting...");
   auto [ec] = co_await timer.async_wait(as_tuple);
   std::println("waiting... {} ({})", ec.what(), cs.cancelled());
   if (ec)
      throw system_error(ec);
}

int main()
{
   boost::asio::io_context context;
   co_spawn(context, [] -> awaitable<void> {
      co_await this_coro::reset_cancellation_state(enable_total_cancellation());
      auto ex = co_await this_coro::executor;      
      auto group = make_parallel_group(co_spawn(ex, task(terminal | partial | total)),
                                       co_spawn(ex, task(terminal)));
      auto x = co_await std::move(group).async_wait(wait_for_one(), deferred);
      std::println("group completed ({} / {})", what(std::get<1>(x)), what(std::get<2>(x)));
   }, cancel_after(1ms, total, detached));
   context.run();
}
