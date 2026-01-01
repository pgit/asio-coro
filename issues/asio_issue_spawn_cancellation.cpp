/**
 * This is not a bug, but a detail to watch out for:
 *
 * Non-terminal cancellation of an awaitable<> task is ignored by default. This is because
 * awaitable<> is configured to react to 'terminal' cancellation only by default. This also
 * applies to the awaitable<> returned from initiating functions and the 'use_awaitable' token.
 */
#include <boost/asio.hpp>

#include <iostream>

using namespace boost::asio;
using enum cancellation_type;

using namespace std::chrono_literals;

awaitable<void> wrapped()
{
   // Enable 'total' cancellation for this task.
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());
   steady_timer timer(co_await this_coro::executor, 2s);
   co_await timer.async_wait();
}

int main()
{
   io_context context;
   auto ex = context.get_executor();

   //
   // Non-terminal cancellation of an awaitable<> as returned by use_awaitable does not work.
   //
   steady_timer t0(ex, 2s);
   co_spawn(ex, t0.async_wait(use_awaitable),
            cancel_after(1ms, total, [](const std::exception_ptr& ep) { //
                                        std::cout << "total: " << !ep << std::endl;
                                     }));

   //
   // Terminal cancellation works.
   //
   steady_timer t1(ex, 2s);
   co_spawn(ex, t1.async_wait(use_awaitable),
            cancel_after(1ms, terminal, [](const std::exception_ptr& ep) { //
                                           std::cout << "terminal: " << !ep << std::endl;
                                        }));

   //
   // Non-terminal cancellation of an awaitable that is explicitly configured to support 'total'
   // cancellation works.
   //
   co_spawn(ex, wrapped(),
            cancel_after(1ms, terminal, [](const std::exception_ptr& ep) { //
                                           std::cout << "wrapped total: " << !ep << std::endl;
                                        }));
   context.run();
}
