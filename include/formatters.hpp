#pragma once
#include <format>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/algorithm/string/join.hpp>

namespace asio = boost::asio; // NOLINT(misc-unused-alias-decls)
using namespace asio;
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
   auto format(const ip::tcp::endpoint& endpoint, std::format_context& ctx) const
   {
      std::ostringstream stream;
      stream << ip::tcp::endpoint{normalize(endpoint.address()), endpoint.port()};
      return std::formatter<std::string>::format(std::move(stream).str(), ctx);
   }
};

// -------------------------------------------------------------------------------------------------

template <>
struct std::formatter<asio::cancellation_type> : std::formatter<std::string_view>
{
   auto format(asio::cancellation_type type, auto& ctx) const
   {
      using enum asio::cancellation_type;

      if (type == none)
         return std::formatter<std::string_view>::format("none", ctx);

      if (type == all)
         return std::formatter<std::string_view>::format("all", ctx);

      bool first = true;
      auto append_if = [&](asio::cancellation_type flag, std::string_view name)
      {
         if ((type & flag) == flag)
         {
            std::format_to(ctx.out(), "{}{}", first ? "" : "|", name);
            first = false;
            type = type & ~flag;
         }
      };

      append_if(terminal, "terminal");
      append_if(partial, "partial");
      append_if(total, "total");

      if (type != none)
         std::format_to(ctx.out(), "{}0x{:x}", first ? "" : "|", to_underlying(type));

      return ctx.out();
   }
};

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
      if (bs.bytes < 1024)
         return std::format_to(ctx.out(), "{} B", bs.bytes);

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
