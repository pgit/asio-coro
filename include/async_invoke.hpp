
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
 * Asynchronously invokes a callable object on the specified executor and completes with the result.
 *
 * This function template schedules the provided callable \p f with arguments \p args... to be
 * executed on the given \p executor. The result of the callable (or completion notification for
 * \c void return types) is delivered to the completion handler associated with the provided
 * completion \p token. The handler is invoked on its associated executor.
 *
 * The function integrates with Boost.Asio's asynchronous model, supporting custom executors,
 * allocators, and cancellation slots associated with the completion handler.
 */
template <typename Executor, typename CompletionToken, typename F, typename... Args>
auto async_invoke(Executor& executor, CompletionToken&& token, F&& f, Args&&... args)
{
   using result_type = std::invoke_result_t<F, Args...>;

   return asio::async_initiate<CompletionToken, void(result_type)>(
      [](auto handler, Executor& pool, F f, Args... args) mutable
   {
      auto ex = asio::get_associated_executor(handler);
      auto alloc = asio::get_associated_allocator(handler);

      asio::post(pool,
                 asio::bind_allocator(alloc, [handler = std::move(handler),
                                              work = make_work_guard(ex), f = std::move(f),
                                              args = std::make_tuple(std::move(args)...)]() mutable
      {
         auto ex = asio::get_associated_executor(handler);
         if constexpr (std::is_void_v<result_type>)
         {
            std::apply(std::move(f), std::move(args));
            dispatch(ex, [handler = std::move(handler)] mutable { //
               std::move(handler)();
            });
         }
         else
         {
            auto result = std::apply(std::move(f), std::move(args));
            dispatch(ex, [handler = std::move(handler), result = std::move(result)] mutable { //
               std::move(handler)(std::move(result));
            });
         }
      }));
   }, token, std::ref(executor), std::forward<F>(f), std::forward<Args>(args)...);
}

// =================================================================================================
