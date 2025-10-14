#include "asio-coro.hpp"
#include "formatters.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <ranges>

using namespace boost::asio;
using namespace experimental::awaitable_operators;
using namespace std::chrono_literals;

// =================================================================================================

/// Filter list of \p endpoints for given address family \p af.
auto filter(const ip::basic_resolver_results<tcp>& endpoints, AddressFamily af)
{
   auto range = endpoints | //
                std::views::transform([](auto& r) { return r.endpoint(); }) |
                std::views::filter([&](const auto& ep) {
      return ep.address().is_v4() && af == ipv4 || ep.address().is_v6() && af == ipv6;
   }) | std::ranges::to<std::vector>();

   return ip::basic_resolver_results<tcp>::create(range.begin(), range.end(),
                                                  endpoints.begin()->host_name(),
                                                  endpoints.begin()->service_name());
}

/**
 * Use async_connect() to open a TCP connection to a host, stopping at the first successful attempt.
 * Prints a single line with \p prefix on error or success.
 *
 * @returns the connected TCP socket
 * @throws system_error
 */
auto connect(std::string_view prefix, const ip::basic_resolver_results<tcp>& endpoints)
   -> awaitable<tcp::socket>
{
   tcp::socket socket(co_await this_coro::executor);
   try
   {
      co_await async_connect(socket, endpoints, cancel_after(2s));
      std::println("{} connected to {}", prefix, socket.remote_endpoint());
   }
   catch (const boost::system::system_error& err)
   {
      std::println("{} {}", prefix, err.code().message());
      throw;
   }
   co_return socket;
};

// -------------------------------------------------------------------------------------------------

auto connect(std::string_view prefix, const ip::basic_resolver_results<tcp>& endpoints,
             std::optional<std::chrono::milliseconds> delay) -> awaitable<tcp::socket>
{
   co_await sleep(*delay);
   co_return co_await connect(prefix, endpoints);
};

// =================================================================================================

/// Open a TCP connect to one of the given endpoints, using the "Happy Eyeballs" algorithm.
awaitable<tcp::socket> happy_eyeballs(ip::basic_resolver_results<tcp>& endpoints)
{
   auto result = co_await (connect("IPv4", filter(endpoints, AddressFamily::ipv4), 100ms) ||
                           connect("IPv6", filter(endpoints, AddressFamily::ipv6)));

   if (result.index() == 0)
      co_return std::get<0>(std::move(result));
   else
      co_return std::get<1>(std::move(result));
}

// -------------------------------------------------------------------------------------------------

/// Resolve a tuple of \p host and \p service to a list of endpoints and attempt to connect.
awaitable<void> happy_eyeballs(std::string_view host, std::string_view service)
{
   auto executor = co_await this_coro::executor;

   tcp::resolver resolver(executor);
   auto endpoints = co_await resolver.async_resolve(host, service);
   for (auto& endpoint : endpoints)
      std::println("👀 endpoint: {}", tcp::endpoint{endpoint});

   auto socket = co_await happy_eyeballs(endpoints);
   std::println("connected to {}", socket.remote_endpoint());
}

// -------------------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
   if (argc != 3)
   {
      std::println("Usage: {} <HOST> <SERVICE>", argv[0]);
      return 1;
   }

   io_context context;
   co_spawn(context, happy_eyeballs(argv[1], argv[2]), log_exception());
   context.run();
}

// =================================================================================================
