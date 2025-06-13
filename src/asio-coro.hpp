#include "formatters.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/scope/scope_exit.hpp>

namespace asio = boost::asio; // NOLINT(misc-unused-alias-decls)
using ip::tcp;

using boost::system::error_code;
using boost::system::system_error;

using namespace std::chrono_literals;
using namespace std::literals::string_view_literals;
using namespace experimental::awaitable_operators;

// =================================================================================================

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

constexpr auto log_exception()
{
   return [](const std::exception_ptr& ptr) { std::println("{}", what(ptr)); };
}

constexpr auto log_exception(std::string prefix)
{
   return [prefix = std::move(prefix)](const std::exception_ptr& ptr)
   {
      std::println("{}: {}", prefix, what(ptr));
   };
}

// =================================================================================================
