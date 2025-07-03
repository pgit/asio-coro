#include "asio-coro.hpp"
#include "process_base.hpp"

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>
#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <print>

// =================================================================================================

awaitable<void> ProcessBase::log(std::string_view prefix, readable_pipe& pipe)
{
   auto print = [&](auto line)
   {
      std::println("{}: \x1b[32m{}\x1b[0m", prefix, line);
      on_log(line);
   };

   std::string buffer;
   try
   {
      for (;;)
      {
         auto n = co_await async_read_until(pipe, dynamic_buffer(buffer), '\n');
         print(std::string_view(buffer).substr(0, n - 1));
         buffer.erase(0, n);
         co_await sleep(50ms);
      }
   }
   catch (const system_error& ec)
   {
      for (auto line : split_lines(buffer))
         print(line);

      std::println("{}: {}", prefix, ec.code().message());
      throw;
   }
}

// -------------------------------------------------------------------------------------------------

awaitable<void> ProcessBase::sleep(steady_timer::duration timeout)
{
   co_await this_coro::reset_cancellation_state(enable_total_cancellation());
   steady_timer timer(co_await this_coro::executor);
   timer.expires_after(timeout);
   co_await timer.async_wait();
}

// =================================================================================================
