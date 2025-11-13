#pragma once
#include "literals.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/beast/core/stream_traits.hpp>

#include <range/v3/view/chunk.hpp>

#include <ranges>

namespace asio = boost::asio; // NOLINT(misc-unused-alias-decls)
using namespace asio;

using boost::system::error_code;
using boost::system::system_error;

// =================================================================================================

//
// FIXME: Do we really need to restrict to "borrowed range" here? The range is kept alive in
//        the coroutine frame, so we do not need to worry about it's lifetime.
//
template <typename Stream, typename Range>
   requires boost::beast::is_async_write_stream<Stream>::value &&
            std::ranges::borrowed_range<Range> && std::ranges::contiguous_range<Range>
awaitable<size_t> send(Stream& stream, Range range)
{
   co_await this_coro::reset_cancellation_state(enable_partial_cancellation());
   co_return co_await async_write(stream, buffer(range.data(), range.size()));
}

//
// For a non-contiguous range, we need to copy into a buffer first.
//
template <typename Stream, typename Range>
   requires boost::beast::is_async_write_stream<Stream>::value &&
            std::ranges::borrowed_range<Range> && (!std::ranges::contiguous_range<Range>)
awaitable<size_t> send(Stream& request, Range range)
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
      total += n;
      if (cs.cancelled() != cancellation_type::none)
         break;

      if (ec)
         throw system_error{ec};
   }

   co_return total;
}

// =================================================================================================
