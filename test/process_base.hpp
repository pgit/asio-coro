#include "asio-coro.hpp"
#include "utils.hpp"

#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <print>

using namespace boost::asio;
using namespace ::testing;

// =================================================================================================

class ProcessBase
{
protected:
   /// Reads lines from \p pipe and prints them, colored, with a \p prefix, colored.
   /**
    * The \p pipe is passed as a reference and must be kept alive while running this coroutine!
    * On error while reading from the pipe, any lines in the remaining buffer are printed,
    * including the trailing incomplete line, if any.
    */
   awaitable<void> log(std::string_view prefix, readable_pipe& pipe);

   /// Returns completion token suitable for testing the result of executing a process.
   auto token()
   {
      return [this](const std::exception_ptr& ep, int exit_code)
      {
         std::println("execute: {}, exit_code={}", what(ep), exit_code);
         if (ep)
            on_error(code(ep));
         else
            on_exit(exit_code);
      };
   }

   MOCK_METHOD(void, on_log, (std::string_view line), ());
   MOCK_METHOD(void, on_error, (error_code ec), ());
   MOCK_METHOD(void, on_exit, (int exit_code), ());

   auto make_system_error(boost::system::errc::errc_t error)
   {
      return boost::system::error_code(error, boost::system::system_category());
   }

private:
   io_context context;

protected:
   any_io_executor executor{context.get_executor()};
   void run() { context.run(); }
   void runDebug() { ::run(context); }
};

// =================================================================================================
