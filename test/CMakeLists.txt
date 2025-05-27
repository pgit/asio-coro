find_package(GTest REQUIRED)
target_link_libraries(GTest::GTest INTERFACE gtest_main)

file(GLOB SRC_FILES test_*.cpp)
add_executable(test_all ${SRC_FILES} utils.cpp)
target_link_libraries(test_all PRIVATE GTest::GTest)

target_include_directories(test_all PUBLIC "../src")
link_libraries(Boost::atomic Boost::url Boost::filesystem)

set_source_files_properties(test_asio.cpp PROPERTIES COMPILE_DEFINITIONS BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT)

# Register the tests with CTest
# include(GoogleTest)
# gtest_discover_tests(test_all)

# Add a custom target to run all tests
# add_custom_target(tests
#   COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
#   DEPENDS test_all
# )
