#pragma once

#include <boost/asio/io_context.hpp>

// =================================================================================================

size_t run(boost::asio::io_context& context);

// =================================================================================================

using Duration = std::chrono::nanoseconds;

// =================================================================================================

   template <std::ranges::input_range R>
      requires std::formattable<std::ranges::range_value_t<R>, char>
   std::string join(R&& range, std::string_view delimiter)
   {
      std::string result;
      auto it = std::ranges::begin(range);
      const auto end = std::ranges::end(range);

      if (it != end)
      {
         result += std::format("{}", *it++);
         while (it != end)
         {
            result += delimiter;
            result += std::format("{}", *it++);
         }
      }

      return result;
   }

