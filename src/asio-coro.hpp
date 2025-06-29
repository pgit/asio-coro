#include "formatters.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/scope/scope_exit.hpp>

namespace asio = boost::asio; // NOLINT(misc-unused-alias-decls)
using ip::tcp;

using boost::scope::make_scope_exit;
using boost::system::error_code;
using boost::system::system_error;

using namespace std::chrono_literals;
using namespace std::literals::string_view_literals;
using namespace experimental::awaitable_operators;

// =================================================================================================

template <>
struct std::formatter<asio::cancellation_type> : std::formatter<std::string_view>
{
   auto format(asio::cancellation_type type, auto& ctx) const
   {
      using ct = asio::cancellation_type;
      switch (type)
      {
      case ct::none:
         return std::formatter<std::string_view>::format("none", ctx);
      case ct::terminal:
         return std::formatter<std::string_view>::format("terminal", ctx);
      case ct::partial:
         return std::formatter<std::string_view>::format("partial", ctx);
      case ct::total:
         return std::formatter<std::string_view>::format("total", ctx);
      default:
         return std::formatter<std::string_view>::format("unknown", ctx);
      }
   }
};

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
      return "success";
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

// =================================================================================================

namespace std
{
inline void PrintTo(const std::exception_ptr& eptr, std::ostream* os) { *os << what(eptr); }
} // namespace std

template <typename F>
concept TestConcept = requires(F f, tcp::socket sock) {
   { f(std::move(sock)) } -> std::same_as<awaitable<void>>;
};


// =================================================================================================
