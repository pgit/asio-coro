/**
 * Asynchronous file I/O requires BOOST_ASIO_HAS_IO_URING and liburing.
 *
 * This program tries to read as much as it can from /dev/zero within one second.
 */
#include "formatters.hpp"
#include "literals.hpp"

#include <boost/asio.hpp>
#include <boost/asio/stream_file.hpp>

using namespace boost::asio;
using namespace std::chrono;
using namespace std::chrono_literals;

awaitable<size_t> read_file()
{
   auto cs = co_await this_coro::cancellation_state;
   auto ex = co_await this_coro::executor;

   stream_file file(ex, "/dev/zero", stream_file::read_only);

   size_t total = 0;
   std::vector<char> data(1_m);
   for (;;)
   {
      auto [ec, n] = co_await file.async_read_some(buffer(data), as_tuple);
      total += n;
      if (ec == error::eof || cs.cancelled() != cancellation_type::none)
         break;
      else if (ec)
         throw boost::system::system_error(ec);
   }

   std::println("read_file: read {}", Bytes(total));
   co_return total;
}

int main()
{
   io_context context;
   co_spawn(context, read_file(), cancel_after(1s, detached));
   context.run();
}
