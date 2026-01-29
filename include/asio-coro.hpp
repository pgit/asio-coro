#pragma once
#include "config.hpp"
#include "formatters.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/scope/scope_exit.hpp>

#include <filesystem>
#include <print>
#include <type_traits>

namespace asio = boost::asio; // NOLINT(misc-unused-alias-decls)
using namespace asio;
using ip::tcp;

using boost::scope::make_scope_exit;
using boost::system::error_code;
using boost::system::system_error;

using namespace std::chrono_literals;
using namespace std::literals::string_view_literals;
using namespace experimental::awaitable_operators;

// =================================================================================================

inline error_code code(const std::exception_ptr& ptr)
{
   if (!ptr)
      return {};
   else
   {
      try
      {
         std::rethrow_exception(ptr);
      }
      catch (multiple_exceptions& mex)
      {
         return code(mex.first_exception());
      }
      catch (system_error& ex)
      {
         return ex.code();
      }
   }
}

inline std::string what(const error_code ec) { return ec.message(); }

inline std::string what(const std::exception_ptr& ptr)
{
   if (!ptr)
      return "Success(ep)";
   else
   {
      try
      {
         std::rethrow_exception(ptr);
      }
      catch (multiple_exceptions& ex)
      {
         return what(ex.first_exception());
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

constexpr auto log_exception()
{
   return [](const std::exception_ptr& ptr)
   {
      if (ptr)
         std::println("{}", what(ptr));
   };
}

template <typename... Args>
constexpr auto log_exception(std::string prefix)
{
   return [prefix = std::move(prefix)](const std::exception_ptr& ptr, Args&&... args)
   {
      std::println("{}: {}", prefix, what(ptr));
      (std::println("{}:   result={}", prefix, std::forward<Args>(args)), ...);
   };
}

inline auto make_system_error(boost::system::errc::errc_t error)
{
   return boost::system::error_code(error, boost::system::system_category());
}

// =================================================================================================

inline awaitable<void> yield() { co_await post(co_await this_coro::executor); }

inline awaitable<void> sleep(steady_timer::duration timeout)
{
   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(timeout);
   co_await timer.async_wait();
}

// =================================================================================================

inline static bool isCancelled(cancellation_state& state,
                               cancellation_type mask = cancellation_type::all)
{
   return (state.cancelled() & mask) != cancellation_type::none;
}

// =================================================================================================

template <typename Buffer>
constexpr std::string_view make_string_view(const Buffer& buffer)
{
   return std::string_view(static_cast<const char*>(buffer.data()), buffer.size());
}

// =================================================================================================

namespace std
{
inline void PrintTo(const std::exception_ptr& ep, std::ostream* os) { *os << what(ep); } // NOLINT
} // namespace std

template <typename F>
concept TestConcept = requires(F f, tcp::socket sock) {
   { f(std::move(sock)) } -> std::same_as<awaitable<void>>;
};

// =================================================================================================

namespace asio_coro_detail
{
template <typename T>
struct is_awaitable_impl : std::false_type
{
};

template <typename R>
struct is_awaitable_impl<asio::awaitable<R>> : std::true_type
{
};

template <typename T>
inline constexpr bool is_awaitable_v = is_awaitable_impl<std::remove_cvref_t<T>>::value;
} // namespace asio_coro_detail

/**
 * Concept: a type that is an `asio::awaitable<...>` (after removing cv/ref).
 */
template <typename T>
concept AwaitableOf = asio_coro_detail::is_awaitable_v<T>;

/**
 * Concept: a callable that, when invoked with no arguments, returns an `asio::awaitable<...>`.
 */
template <typename F>
concept CallableAwaitable = std::invocable<F> && AwaitableOf<std::invoke_result_t<F>>;

/**
 * Concept: either an awaitable type or a callable that returns an awaitable.
 */
template <typename T>
concept AwaitableOrCallableAwaitable = AwaitableOf<T> || CallableAwaitable<T>;

// =================================================================================================

/**
 * There is no builtin support for process groups in Process V2, because it is impossible to
 * implement in a portable way. But for POSIX, we can rely on "process groups" and kill them,
 * taking down any descendant process as well.
 */
struct setpgid_initializer
{
   template <typename Launcher>
   error_code on_exec_setup(Launcher&, const std::filesystem::path&, const char* const*(&))
   {
      setpgid(0, 0);
      return {};
   }
};

// =================================================================================================

enum AddressFamily : std::uint8_t
{
   IPv4,
   IPv6
};

inline std::string_view to_string(AddressFamily family)
{
   switch (family)
   {
   case IPv4:
      return "IPv4";
   case IPv6:
      return "IPv6";
   default:
      return "Unknown";
   }
}

// =================================================================================================
