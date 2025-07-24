find_package(GTest REQUIRED)
target_link_libraries(GTest::GTest INTERFACE gtest_main)

# no CMake based testing, only GTest
enable_testing(false)

file(GLOB SRC_FILES *.cpp)
add_executable(test_all ${SRC_FILES} utils.cpp)
target_include_directories(test_all PRIVATE "../src")
target_link_libraries(test_all PRIVATE GTest::GTest GTest::gmock Boost::process)
