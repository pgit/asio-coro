#include "log.hpp"

#include <boost/asio/read_until.hpp>

#include <print>

using namespace boost::asio;

/// Reads lines from \p pipe and prints them with \p prefix, colored.
/**
 * The \p pipe is passed as a reference and must be kept alive while running this coroutine!
 * On error while reading from the pipe, any lines in the remaining buffer are printed,
 * including the trailing incomplete line, if any.
 *
 * This coroutine supports 'terminal' cancellation only. Any remaining buffered data is printed
 * before returning. If logging was interrupted within a line, that partial line is printed as well.
 */
awaitable<void> log(std::string_view prefix, readable_pipe& pipe)
{
   auto print = [&](auto line) { std::println("{}: \x1b[32m{}\x1b[0m", prefix, line); };

   std::string buffer;
   try
   {
      for (;;)
      {
         auto n = co_await async_read_until(pipe, dynamic_buffer(buffer), '\n');
         print(std::string_view(buffer).substr(0, n - 1));
         buffer.erase(0, n);
      }
   }
   catch (const boost::system::system_error& ec)
   {
      for (auto line : split_lines(buffer))
         print(line);

      if (ec.code() == error::eof)
         co_return;

      throw;
   }
}
