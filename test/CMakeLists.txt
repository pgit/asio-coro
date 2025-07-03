find_package(GTest REQUIRED)
target_link_libraries(GTest::GTest INTERFACE gtest_main)

file(GLOB SRC_FILES *.cpp)
add_executable(test_all ${SRC_FILES} utils.cpp)
target_link_libraries(test_all PRIVATE GTest::GTest GTest::gmock)

target_include_directories(test_all PUBLIC "../src")

target_link_libraries(test_all PUBLIC Boost::process)

# set_source_files_properties(test_asio.cpp PROPERTIES COMPILE_DEFINITIONS BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT)
