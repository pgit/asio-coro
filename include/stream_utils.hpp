#pragma once
#include "concepts.hpp"
#include "literals.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/completion_condition.hpp>

#include <range/v3/view/chunk.hpp>

#include <ranges>

namespace asio = boost::asio; // NOLINT(misc-unused-alias-decls)
using namespace asio;

using boost::system::error_code;
using boost::system::system_error;

// =================================================================================================

/// Writes a contiguous block of memory, as provided by a contiguous range.
template <AsyncWriteStream Stream, std::ranges::range Range>
   requires std::ranges::contiguous_range<Range>
awaitable<size_t> write(Stream& stream, Range range)
{
   auto cs = co_await this_coro::cancellation_state;  
   co_await this_coro::reset_cancellation_state(enable_partial_cancellation());

   auto [ec, n] = co_await async_write(stream, buffer(range.data(), range.size()), as_tuple);
   
   if (cs.cancelled() != cancellation_type::none)
      co_return n;

   if (ec)
      throw system_error{ec};

   co_return n;
}

// Writes a non-contiguous range in chunks, copying to a temporary buffer.
template <AsyncWriteStream Stream, std::ranges::range Range>
   requires(!std::ranges::contiguous_range<Range>)
awaitable<size_t> write(Stream& request, Range range)
{
   auto ex = co_await this_coro::executor;
   auto cs = co_await this_coro::cancellation_state;
   co_await this_coro::reset_cancellation_state(enable_partial_cancellation());

   size_t total = 0;
   std::array<uint8_t, 64_k> data;
   for (auto chunk : range | ranges::views::chunk(data.size()))
   {
      auto end = std::ranges::copy(chunk, data.data()).out;
      auto copied = end - data.data();
      auto [ec, n] = co_await async_write(request, buffer(data, copied), as_tuple);
      total += n; // even on error, 'n' indicates the number of bytes written so far

      // don't raise an error on cancellation, just report what has been written
      if (cs.cancelled() != cancellation_type::none)
         break;

      if (ec)
         throw system_error{ec};
   }

   co_return total;
}

// =================================================================================================

template <AsyncWriteStream Stream, std::ranges::range Range>
inline awaitable<size_t> write_and_close(Stream stream, Range&& range)
{
   co_await this_coro::reset_cancellation_state(enable_partial_cancellation());
   auto n = co_await write(stream, std::forward<Range>(range));
   stream.close();
   co_return n;
}

template <AsyncWriteStream Stream, std::ranges::range Range, typename Period>
awaitable<size_t> write_and_close(Stream stream, Range&& range, Period timeout)
{
   auto ex = co_await this_coro::executor;
   co_await this_coro::reset_cancellation_state(enable_partial_cancellation());
   auto n = co_await co_spawn(ex, write(stream, std::forward<Range>(range)), cancel_after(timeout));
   stream.close();
   co_return n;
}

// -------------------------------------------------------------------------------------------------

template <AsyncReadStream Stream>
awaitable<std::string> read_all(Stream stream)
{
   std::string buffer;
   auto [ec, n] = co_await async_read(stream, dynamic_buffer(buffer), transfer_all(), as_tuple);
   if (ec && ec != error::eof)
      throw system_error(ec);
   stream.close();
   co_return buffer;
}

// -------------------------------------------------------------------------------------------------

template <AsyncReadStream Stream>
awaitable<size_t> count(Stream stream)
{
   size_t total = 0;
   try
   {
      std::array<char, 64_k> data;
      for (;;)
         total += co_await stream.async_read_some(buffer(data));
   }
   catch (boost::system::system_error& error)
   {
      if (error.code() != error::eof)
         throw;
   }
   co_return total;
}

// =================================================================================================

template <AsyncReadStream ReadStream, AsyncWriteStream WriteStream>
awaitable<size_t> cat(ReadStream in, WriteStream out)
{
   size_t total = 0;
   try
   {
      std::array<char, 64 * 1024> data;
      for (;;)
      {
         size_t n = co_await in.async_read_some(buffer(data));
         co_await async_write(out, buffer(data, n));
         total += n;
      }
   }
   catch (boost::system::system_error& error)
   {
      if (error.code() != error::eof)
         throw;
      out.close();
   }
   co_return total;
}

// =================================================================================================
