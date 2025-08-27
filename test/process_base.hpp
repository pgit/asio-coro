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
   /// Reads lines from \p pipe and prints them with \p prefix, colored.
   /**
    * The \p pipe is passed as a reference and must be kept alive while as long as the coroutine is
    * running. After cancellation, the referenced pipe will not be accessed any more, but the
    * coroutine may continue to run until everything has been printed. This is also the reason why
    * the prefix is passed as a copy instead of a reference or view.
    *
    * On error while reading from the pipe, any lines in the remaining buffer are printed,
    * including the trailing incomplete line, if any.
    */
   awaitable<void> log(std::string prefix, readable_pipe& pipe);
   awaitable<void> log(readable_pipe& pipe) { return log("STDOUT", pipe); }

   /// Returns completion token suitable for testing the result of executing a process.
   auto token()
   {
      return [this](const std::exception_ptr& ep, int exit_code)
      {
         if (ep)
         {
            std::println("execute: {}", what(ep));
            on_error(code(ep));
         }
         else
         {
            std::println("execute: Success, exit_code={}", exit_code);
            on_exit(exit_code);
         }
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
