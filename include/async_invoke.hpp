#pragma once
#include <boost/asio.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/post.hpp>

#include <type_traits>
#include <utility>

namespace asio = boost::asio;

// =================================================================================================

/**
 * Binds all arguments to a callable, returning a nullary callable.
 *
 * This utility function takes a callable object \p f and a set of arguments \p args..., and
 * returns a new callable that, when invoked with no arguments, calls \p f with the bound
 * arguments. The bound arguments are stored in a tuple and moved when the returned callable is
 * invoked.
 *
 * This is particularly useful for deferring the execution of a function with specific arguments,
 * such as when posting tasks to an executor.
 */
template <typename F, typename... Args>
   requires std::invocable<F, Args...>
auto bind_all(F&& f, Args&&... args)
{
   return [f = std::forward<F>(f),
           args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
      return std::apply(std::move(f), std::move(args));
   };
}

/**
 * Asynchronously invokes a callable object on the specified executor and completes with the result.
 *
 * This function template schedules the provided callable \p f with arguments \p args... to be
 * executed on the given \p executor. The result of the callable (or completion notification for
 * \c void return types) is delivered to the completion handler associated with the provided
 * completion \p token. The handler is invoked on its associated executor.
 *
 * The function integrates with Boost.Asio's asynchronous model, supporting custom executors,
 * allocators, and cancellation slots associated with the completion handler.
 *
 * https://www.boost.org/doc/libs/master/doc/html/boost_asio/reference/asynchronous_operations.html
 */
template <BOOST_ASIO_EXECUTION_EXECUTOR Executor, typename F, typename... Args,
          typename CompletionToken,
          typename CompletionSignature = void(std::invoke_result_t<F, Args...>)>
   requires std::invocable<F, Args...> &&
            asio::completion_token_for<CompletionToken, CompletionSignature>
auto async_invoke(Executor& executor, CompletionToken&& token, F&& f, Args&&... args)
{
   return asio::async_initiate<CompletionToken, CompletionSignature>(
      [](auto handler, Executor& pool, F f, Args... args) mutable
   {
      auto ex = get_associated_executor(handler, pool);
      auto alloc = get_associated_allocator(handler);

      post(pool, bind_allocator(alloc, [handler = std::move(handler), work = make_work_guard(ex),
                                        f = std::move(f),
                                        args = std::make_tuple(std::move(args)...)]() mutable
      {
         auto ex = get_associated_executor(handler);
         if constexpr (std::is_void_v<std::invoke_result_t<F, Args...>>)
         {
            std::apply(std::move(f), std::move(args));
            dispatch(ex, std::move(handler)(boost::system::error_code{}));
         }
         else
         {
            auto result = std::apply(std::move(f), std::move(args));
            dispatch(ex, std::bind(std::move(handler), std::move(result)));
         }
      }));
   }, token, std::ref(executor), std::forward<F>(f), std::forward<Args>(args)...);
}

// -------------------------------------------------------------------------------------------------

/**
 * Function template overload with a default completion token (usually, 'deferred').
 *
 * This is a bit tricky because of the variadic args but works if enough concepts are in place
 * to make them unambiguous.
 */
template <BOOST_ASIO_EXECUTION_EXECUTOR Executor, typename F, typename... Args,
          typename CompletionToken = asio::default_completion_token_t<Executor>,
          typename CompletionSignature = void(std::invoke_result_t<F, Args...>)>
   requires std::invocable<F, Args...> &&
            asio::completion_token_for<CompletionToken, CompletionSignature>
auto async_invoke(Executor& executor, F&& f, Args&&... args)
{
   return async_invoke(executor, CompletionToken(), std::forward<F>(f),
                       std::forward<Args>(args)...);
}

// =================================================================================================
