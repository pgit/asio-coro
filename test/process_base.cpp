#include "process_base.hpp"

#include "log.hpp"

#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>
#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <print>

// =================================================================================================

awaitable<void> ProcessBase::log(std::string prefix, readable_pipe& pipe,
                                 std::function<void(std::string_view)> handleLine)
{
   co_await ::log(prefix, pipe, [prefix, handleLine = std::move(handleLine)](std::string_view line)
   {
      if (line.ends_with('\n'))
         line.remove_suffix(1);

      std::println("{}: \x1b[32m{}\x1b[0m", prefix, line);
      handleLine(line);
   });
}

// =================================================================================================
