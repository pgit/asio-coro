#pragma once
#include "formatters.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/scope/scope_exit.hpp>

#include <filesystem>

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
      catch (boost::system::system_error& ex)
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
   return [](const std::exception_ptr& ptr) { std::println("{}", what(ptr)); };
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

inline awaitable<void> yield() { co_await post(co_await this_coro::executor); }

inline awaitable<void> sleep(steady_timer::duration timeout)
{
   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(timeout);
   co_await timer.async_wait();
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
