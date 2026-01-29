/**
 * https://github.com/chriskohlhoff/asio/issues/1705
 *
 * Demonstrates that cancelling a promise via cancel_after doesn't work properly, as opposed to
 * cancelling it via a parallel group. The reason seems to be that the completion handler is
 * invoked immediately and not posted().
 *
 * This variant doesn't use coroutines.
 */
#include <boost/asio.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

#include <print>

using namespace boost::asio;
using namespace std::chrono_literals;

int main()
{
   io_context context;

   steady_timer forever(context);
   forever.expires_after(steady_timer::duration::max());
   auto promise = forever.async_wait(experimental::use_promise);

   steady_timer timer(context);
   std::move(promise)(cancel_after(timer, 1ms, cancellation_type::total,
                                   [](boost::system::error_code ec) { 
      std::println("completed with {}", ec.message());
   }));

   std::println("running IO context...");
   context.run();
   std::println("running IO context... done");
}
