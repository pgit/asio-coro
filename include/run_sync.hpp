#pragma once
#include "run.hpp"

#include <boost/asio/async_result.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <type_traits>
#include <utility>

namespace asio = boost::asio;

// =================================================================================================

/**
 * Helper function to run an \c asio::awaitable task synchronously.
 *
 * The function creates a temporary IO context and \c co_spawns the task on it. Then, it runs the
 * context until the coroutine has finished (and no other work has been created in the meantime).
 *
 * This function can be seen as the counterpart to \c async_invoke.
 */
template <typename Awaitable>
auto run_sync(Awaitable&& awaitable) -> std::decay_t<typename Awaitable::value_type>
{
   using result_type = std::decay_t<typename Awaitable::value_type>;
   using value_type = std::conditional_t<std::is_void_v<result_type>, std::monostate, result_type>;
   std::optional<value_type> result;
   std::exception_ptr ep;

   asio::io_context context;
   asio::co_spawn(context, [&]() -> asio::awaitable<void>
   {
      try
      {
         if constexpr (!std::is_void_v<result_type>)
            result = co_await std::forward<Awaitable>(awaitable);
         else
         {
            co_await std::forward<Awaitable>(awaitable);
            result.emplace();
         }
      }
      catch (...)
      {
         ep = std::current_exception();
      }
   }, asio::detached);

   ::run(context);
   assert(!!ep ^ result.has_value()); // either exception or value

   if (ep)
      std::rethrow_exception(ep);

   if constexpr (!std::is_void_v<result_type>)
      return std::move(result).value();
}

// -------------------------------------------------------------------------------------------------

/**
 * Wrapper accepting a callable, just like \c co_spawn() does.
 *
 * The lifetime issues you can get when capturing stuff in coroutine lambdas don't even apply so
 * much to \c run_async(), because the coroutine has completed at the end of the full-expression.
 *
 * Still, this specialization is provided for shorter syntax and symmetry.
 */
template <typename Callable>
   requires std::invocable<Callable> &&
            requires { typename std::invoke_result_t<Callable>::value_type; }
auto run_sync(Callable&& callable)
{
   return run_sync(std::invoke(std::forward<Callable>(callable)));
}

// =================================================================================================
