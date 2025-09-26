#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/awaitable.hpp>

#include <ranges>

using namespace boost::asio;

/// Transform \p lines into a range of \c string_view, splitting at LF. Skip last line if empty.
inline auto split_lines(std::string_view lines)
{
   if (lines.ends_with('\n'))
      lines.remove_suffix(1);

   return lines | std::views::split('\n') |
          std::views::transform([](auto range) { return std::string_view(range); });
}

awaitable<void> log(std::string_view prefix, readable_pipe& pipe);
