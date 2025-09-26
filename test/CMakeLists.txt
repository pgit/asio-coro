include_directories("../include")

file(GLOB SRC_FILES "*.cpp")
add_executable(test_all ${SRC_FILES})
target_link_libraries(test_all PRIVATE GTest::GTest GTest::gmock Boost::process)
target_link_libraries(test_all PRIVATE asio_coro)

find_package(GTest REQUIRED)
target_link_libraries(GTest::GTest INTERFACE gtest_main)

enable_testing(false)
