#include "process_base.hpp"
#include "asio-coro.hpp"

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>
#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <print>

// =================================================================================================

awaitable<void> ProcessBase::log(std::string prefix, readable_pipe& pipe)
{
   std::string buffer;
   auto print = [&](auto line)
   {
      // print trailing '…' if there is more data in the buffer after the current line
      const auto continuation = (line.size() + 1 == buffer.size()) ? "" : "…";
      std::println("{}: \x1b[32m{}\x1b[0m{}", prefix, line, continuation);
      on_log(line);
   };

   auto cs = co_await this_coro::cancellation_state;
   try
   {
      for (;;)
      {
         auto n = co_await async_read_until(pipe, dynamic_buffer(buffer), '\n');
         print(std::string_view(buffer).substr(0, n - 1));
         buffer.erase(0, n);

         //
         // Having a little artificial delay here makes things more interesting for the testcase.
         // This can easily lead to problems if the log() coroutine is detached, breaking
         // structured concurrency. We try to provoke that here.
         //
         co_await sleep(1ms);
      }
   }
   catch (const system_error& ec)
   {
      if (cs.cancelled() != cancellation_type::none)
         std::println("CANCELLED");
      for (auto line : split_lines(buffer))
         print(line);
      std::println("{}: {}", prefix, ec.code().message());
      if (ec.code() == error::eof)
         co_return;
      throw;
   }
}

// =================================================================================================
