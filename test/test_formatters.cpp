#include "formatters.hpp"

#include <gtest/gtest.h>

using namespace boost::asio;
using namespace ::testing;

// =================================================================================================

TEST(Formatters, CancellationType)
{
   using ct = asio::cancellation_type;
   using enum ct;
   EXPECT_EQ(std::format("{}", none), "none");
   EXPECT_EQ(std::format("{}", terminal | partial | total), "terminal|partial|total");
   EXPECT_EQ(std::format("{}", terminal | partial), "terminal|partial");
   EXPECT_EQ(std::format("{}", terminal), "terminal");
   EXPECT_EQ(std::format("{}", partial | static_cast<ct>(0x10000)), "partial|0x10000");
   EXPECT_EQ(std::format("{}", total | all), "all");
   EXPECT_EQ(std::format("{}", total | static_cast<ct>(~255)), "total|0xffffff00");
}

// =================================================================================================
