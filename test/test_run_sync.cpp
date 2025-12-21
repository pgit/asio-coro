#include "asio-coro.hpp"
#include "run_sync.hpp"

#include <gtest/gtest.h>

using namespace asio;

// =================================================================================================

TEST(RunSync, Void)
{
   bool flag = false;
   run_sync(std::invoke([](bool& flag) -> awaitable<void>
   {
      flag = true;
      co_return;
   }, flag));
   EXPECT_TRUE(flag);
}

TEST(RunSync, CallableVoid)
{
   bool flag = false;
   run_sync([&] -> awaitable<void>
   {
      flag = true;
      co_return;
   });
   EXPECT_TRUE(flag);
}

// -------------------------------------------------------------------------------------------------

TEST(RunSync, Integer)
{
   EXPECT_EQ(run_sync(std::invoke([]() -> awaitable<int> { co_return 42; })), 42);
}

TEST(RunSync, CallableInteger)
{
   EXPECT_EQ(run_sync([n = 42]() -> awaitable<int> { co_return n; }), 42);
}

// =================================================================================================
