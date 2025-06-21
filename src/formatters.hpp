#include <format>
#include <ranges>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/algorithm/string/join.hpp>

namespace asio = boost::asio; // NOLINT(misc-unused-alias-decls)
using namespace asio; // in case we want to qualify explicitly
using ip::tcp;

using boost::algorithm::join;

// =================================================================================================

/// Normalize mapped IPv4 address ([::ffff:127.0.0.1]) to an actual V4 address (127.0.0.1).
inline ip::address normalize(const ip::address& addr)
{
   if (addr.is_v6() && addr.to_v6().is_v4_mapped())
      return ip::make_address_v4(ip::v4_mapped_t{}, addr.to_v6());
   return addr;
}

// -------------------------------------------------------------------------------------------------

template <>
struct std::formatter<ip::address> : std::formatter<std::string>
{
   auto format(const ip::address& address, std::format_context& ctx) const
   {
      std::ostringstream stream;
      stream << normalize(address);
      return std::formatter<std::string>::format(std::move(stream).str(), ctx);
   }
};

template <>
struct std::formatter<ip::tcp::endpoint> : std::formatter<std::string>
{
   auto format(const tcp::endpoint& endpoint, std::format_context& ctx) const
   {
      std::ostringstream stream;
      stream << tcp::endpoint{normalize(endpoint.address()), endpoint.port()};
      return std::formatter<std::string>::format(std::move(stream).str(), ctx);
   }
};

// =================================================================================================

/// Transform \p lines into a range of \c string_view, splitting at LF. Skip last line if empty.
inline auto split(std::string_view lines)
{
   if (lines.ends_with('\n'))
      lines.remove_suffix(1);

   return lines | std::views::split('\n') | std::views::transform([](auto range){
      return std::string_view(range);
   });
}

// =================================================================================================

/// Format size_t truncated and formatted to a suitable binary unit (GiB, MiB, KiB etc..)
struct Bytes
{
   size_t bytes;
};

template <>
struct std::formatter<Bytes>
{
   constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

   auto format(const Bytes& bs, std::format_context& ctx) const
   {
      constexpr array units{"B", "KiB", "MiB", "GiB", "TiB", "PiB"};

      size_t index = 0;
      double size = static_cast<double>(bs.bytes);
      while (size >= 1024.0 && index < units.size() - 1)
      {
         size /= 1024.0;
         ++index;
      }

      return std::format_to(ctx.out(), "{:.2f} {}", size, units[index]);
   }
};

// =================================================================================================
