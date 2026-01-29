#include "log.hpp"
#include "asio-coro.hpp"

#include <boost/asio/read_until.hpp>

#include <print>
#include <ranges>

using namespace boost::asio;

using boost::system::system_error;

// =================================================================================================

/// Reads lines from \p pipe and prints them with \p prefix, colored.
/**
 * The \p pipe is passed as a reference and must be kept alive while running this coroutine!
 * On error while reading from the pipe, any lines in the remaining buffer are printed,
 * including the trailing incomplete line, if any.
 *
 * This coroutine supports 'terminal' cancellation only. Any remaining buffered data is printed
 * before returning. If logging was interrupted within a line, that partial line is printed as well.
 *
 * To do this, the coroutine is resumed one last time after it has been cancelled. At that time, it
 * does not access the \p pipe object any more (which is a reference to a parent object that may
 * already be out of scope.
 */
awaitable<void> log(std::string_view prefix, readable_pipe& pipe)
{
   co_await log(prefix, pipe, [prefix](std::string_view line) { //
      std::println("{}: \x1b[32m{}\x1b[0m", prefix, line);
   });
}

// -------------------------------------------------------------------------------------------------

awaitable<void> log(std::string_view prefix, readable_pipe& pipe,
                    std::function<void(std::string_view)> handleLine)
{
   boost::asio::streambuf buffer;

   //
   // Cancellation is restricted to 'terminal|partial' here, but not 'total'.
   // This is a somewhat deliberate decision, because we want to log the remaining output the
   // SIGINT that is is caused by 'total' cancellation. On the other hand, this function is only
   // about logging from a pipe and shouldn't concern itself with the cancellation types of some
   // other component.
   //
   // There are a few testcases that rely on this:
   // * ProcessCancellation.WHEN_parallel_group_is_cancelled_total_THEN_logging_continues
   // * ProcessCancellation.WHEN_parallel_group_operator_THEN_cancellation_fails
   //
   using enum cancellation_type;
#if 1
   auto filter = terminal | partial; // | total;
   co_await this_coro::reset_cancellation_state([filter](cancellation_type type)
   {
      auto filtered = type & filter;
      if (filtered == none)
         std::println("FILTER({}): {} -> \x1b[1;31m{}\x1b[0m", filter, type, filtered);
      else
         std::println("FILTER({}): {} -> {}", filter, type, filtered);
      return filtered;
   });
#else // equivalent
   co_await this_coro::reset_cancellation_state(enable_partial_cancellation());
#endif

   //
   // Installing a custom signal handler allows us to react to cancellation in the moment it
   // happens. This handler is directly invoked during the actual cancellation, and not later
   // in the continuation. This is very important, because at that time, any of the references
   // passed to this coroutine ('prefix', 'pipe' and also 'handleLine') may already be invalid.
   //
   auto cs = co_await this_coro::cancellation_state;
   cancellation_signal signal;
   if (cs.slot().is_connected())
      cs.slot().assign([&](cancellation_type type)
      {
         std::println("{}: CANCELLED ({}) buffer={}", prefix, type, buffer.size());
         for (auto line : make_string_view(buffer.data()) | std::views::split('\n'))
            handleLine(make_string_view(line));
         signal.emit(type);
      });

   //
   // Main log loop, reads from pipe, line-by-line.
   //
   error_code ec;
   for (;;)
   {
      size_t n;
      std::tie(ec, n) = co_await async_read_until(pipe, buffer, '\n',
                                                  bind_cancellation_slot(signal.slot(), as_tuple));

      //
      // On cancellation, bail out early. Any remaining lines in the buffer have already been
      // emitted by the custom cancellation handler.
      //
      if (isCancelled(cs))
      {
         std::println("{}: CANCELLED ({}, ec={})", prefix, cs.cancelled(), what(ec));
         throw system_error(make_system_error(boost::system::errc::operation_canceled));
      }

      if (ec)
         break;

      handleLine(make_string_view(buffer.data()).substr(0, n));
      buffer.consume(n);
   }

   //
   // Emit remaining data from buffer
   //
   assert(!isCancelled(cs));
   for (auto line : make_string_view(buffer.data()) | std::views::split('\n'))
      handleLine(make_string_view(line));

   //
   // Only on graceful EOF, close our end of the pipe as well.
   //
   if (ec == boost::asio::error::eof)
   {
      std::println("{}: reached EOF", prefix);
      pipe.close();
   }
   else if (ec)
   {
      std::println("{}: read error: {}", prefix, what(ec));
      throw system_error(ec);
   }
}

// =================================================================================================
