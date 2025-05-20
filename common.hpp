#include <format>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/experimental/co_composed.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace asio = boost::asio;
using namespace asio;
using ip::tcp;

template <>
struct std::formatter<ip::tcp::endpoint> : std::formatter<std::string>
{
   auto format(const ip::tcp::endpoint& endpoint, std::format_context& ctx) const
   {
      std::ostringstream str;
      str << endpoint;
      return std::formatter<std::string>::format(str.str(), ctx);
   }
};
