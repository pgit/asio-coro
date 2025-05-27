#include <format>

#include <boost/asio.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace asio = boost::asio; // NOLINT(misc-unused-alias-decls)
using namespace asio;  // in case we want to qualify explicitly
using ip::tcp;
using boost::system::error_code;

/// Normalize mapped IPv4 address ([::ffff:127.0.0.1]) to an actual V4 address (127.0.0.1).
inline ip::address normalize(const ip::address& addr)
{
   if (addr.is_v6() && addr.to_v6().is_v4_mapped())
      return ip::make_address_v4(ip::v4_mapped_t{}, addr.to_v6());
   return addr;
}

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
struct std::formatter<tcp::endpoint> : std::formatter<std::string>
{
   auto format(const tcp::endpoint& endpoint, std::format_context& ctx) const
   {
      std::ostringstream stream;
      stream << tcp::endpoint{normalize(endpoint.address()), endpoint.port()};
      return std::formatter<std::string>::format(std::move(stream).str(), ctx);
   }
};

inline std::string what(const error_code ec) { return ec.message(); }

inline std::string what(const std::exception_ptr& ptr)
{
   if (!ptr)
      return "success";
   else
   {
      try
      {
         std::rethrow_exception(ptr);
      }
      catch (boost::system::system_error& ex)
      {
         return ex.code().message();
      }
      catch (std::exception& ex)
      {
         return ex.what();
      }
   }
}

inline auto log_exception()
{
   return [](const std::exception_ptr& ptr) { std::println("{}", what(ptr)); };
}

inline auto log_exception(std::string_view prefix)
{
   return [=](const std::exception_ptr& ptr) { std::println("{}: {}", prefix, what(ptr)); };
}
