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
                std::views::filter([&](const auto& ep)
   {
      return af == IPv4 && ep.address().is_v4() || //
             af == IPv6 && ep.address().is_v6();
   }) | std::ranges::to<std::vector>();

   return ip::basic_resolver_results<tcp>::create(range.begin(), range.end(),
                                                  endpoints.begin()->host_name(),
                                                  endpoints.begin()->service_name());
}

// -------------------------------------------------------------------------------------------------

/**
 * Use async_connect() to open a TCP connection to a host, stopping at the first successful attempt.
 * Prints a single line with \p prefix on error or success.
 *
 * @returns the connected TCP socket
 * @throws system_error
 */
awaitable<tcp::socket> connect(std::string_view prefix,
                               const ip::basic_resolver_results<tcp>& endpoints)
{
   tcp::socket socket(co_await this_coro::executor);
   try
   {
      co_await async_connect(socket, endpoints);
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

awaitable<tcp::socket> connect(std::string_view prefix,
                               const ip::basic_resolver_results<tcp>& endpoints,
                               std::chrono::milliseconds delay)
{
   co_await sleep(delay);
   co_return co_await connect(prefix, endpoints);
};

// =================================================================================================

/// Open a TCP connection to one of the given endpoints, preferring IPv6 using the 😊👀 algorithm.
awaitable<tcp::socket> happy_eyeballs(const ip::basic_resolver_results<tcp>& endpoints)
{
   auto variant =
      co_await (connect("\x1b[1;32mIPv6\x1b[0m", filter(endpoints, AddressFamily::IPv6)) ||
                connect("\x1b[1;34mIPv4\x1b[0m", filter(endpoints, AddressFamily::IPv4), 200ms));

   //
   // The return type of operator|| is a std::variant<> of the return types of the two
   // operations. Here, the two types are equal (tcp::socket). std::get<tcp::socket>()
   // doesn't work in this situations because it checks for unique types. std::get<0/1>
   // works but is clumsy.
   //
   // But we can simply 'visit' the variant like this:
   //
   co_return std::visit([](auto& socket) { return std::move(socket); }, variant);
}

// -------------------------------------------------------------------------------------------------

/// Resolve a \p host and \p service to a list of endpoints and attempt to connect.
awaitable<tcp::socket> happy_eyeballs(std::string_view host, std::string_view service)
{
   tcp::resolver resolver(co_await this_coro::executor);
   auto endpoints = co_await resolver.async_resolve(host, service);
   for (auto& endpoint : endpoints)
      std::println("endpoint: {}", tcp::endpoint{endpoint});

   co_return co_await happy_eyeballs(endpoints);
}

/// Attempt to connect to the given \p host and \p service using the Happy Eyeballs algorithm.
awaitable<void> test_happy_eyeballs(std::string_view host, std::string_view service)
{
   auto socket = co_await (happy_eyeballs(host, service));
   std::println("😊👀 connected to {}", socket.remote_endpoint());
}

// -------------------------------------------------------------------------------------------------

/// Usage: happy_eyeballs <HOST> <SERVICE>
int main(int argc, char* argv[])
{
   if (argc != 3)
   {
      std::println("Usage: {} <HOST> <SERVICE>", argv[0]);
      return 1;
   }

   io_context context;
   co_spawn(context, test_happy_eyeballs(argv[1], argv[2]), log_exception());
   context.run();
}

// =================================================================================================
