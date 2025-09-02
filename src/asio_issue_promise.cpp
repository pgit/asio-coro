/**
 * This is not a bug, but a detail to watch out for:
 *
 * After a coroutine is cancelled, you cannot assume that any arguments passed as reference from
 * an outer coroutine are still valid. More generally, catching the cancellation signal somehow
 * and continuing to run the coroutine breaks the rules of structured concurrency.
 *
 * It doesn't matter how you 'catch' the cancellation, by using the `as_tuple` completion token
 * adapter or by catching the `system_error`. The code that is as a result of this is part of the
 * completion handler of the asynchronous operation. According to the rules for asynchronous
 * operations, a completion handler must always executed as if `post()`ed.
 *
 * You can, however, do stuff within the cancellation handler.
 */
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

using namespace boost::asio;
using namespace experimental;
using namespace awaitable_operators;
using namespace std::chrono_literals;

class Resource
{
public:
   ~Resource() { alive = false; }
   bool alive{true};
};

awaitable<void> subtask(Resource& resource)
{
   cancellation_signal signal;
   auto cs = co_await this_coro::cancellation_state;
   cs.slot().assign(
      [&](cancellation_type ct)
      {
         std::println("cancelled");
         //
         // It's only here, when handling the cancellation signal, that the parent coroutine
         // is still running and thus the reference to the resource is alive.
         //
         assert(resource.alive);
         signal.emit(ct);
      });

   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(10s);

#if 1
   //
   // We need to be very careful when catching exceptions or, equivalently, using a non-throwing
   // completion token adaptor like as_tuple<>. In this situation, the coroutine is resumed after
   // the cancellation. At that time, the parent coroutine and anything passed as a reference is
   // invalid.
   //
   auto ec = co_await timer.async_wait(bind_cancellation_slot(signal.slot(), as_tuple));
   
   //
   // Entering danger zone. We have been cancelled, but decided to ignore this and continue
   // running. We cannot rely on anything passed to us by reference any more.
   //
   // This compiles and *might* run. With clang and libc++, ASAN doesn't detect this either,
   // UNLESS two tasks() are started in a parallel group.
   //
   assert(!resource.alive); // might compile and run, but is undefined behaviour
#else
   try
   {
      co_await timer.async_wait(bind_cancellation_slot(signal.slot()));
   }
   catch (const boost::system::system_error& ex)
   {
      assert(!resource.alive); // might compile and run, but is undefined behaviour
   }
#endif
}

awaitable<void> task()
{
   Resource resource;
   auto promise = co_spawn(co_await this_coro::executor, subtask(resource), use_promise);
   // when leaving this coroutine, the promise is deleted and subtask() is cancelled
}

int main()
{
   boost::asio::io_context context;
   co_spawn(context.get_executor(), task() && task(), detached); // start two tasks() in a group
   context.run();
}
