cmake_minimum_required(VERSION 3.30)
project(asio-coro VERSION 0.0.1)

set(CMAKE_EXPORT_COMPILE_COMMANDS On)
set(CMAKE_CXX_STANDARD 23)

# debug and sanitizer options
if (CMAKE_BUILD_TYPE MATCHES Debug)
    # add_compile_definitions(BOOST_ASIO_ENABLE_BUFFER_DEBUGGING)

    # ASIO handler tracking output (very noisy)
    # add_compile_definitions(BOOST_ASIO_ENABLE_HANDLER_TRACKING)
    
    # thread sanitizer (doesn't work on codespaces)
    # add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    # add_link_options(-fsanitize=thread)
    
    # address sanitizer
    # add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    # add_link_options(-fsanitize=address)
endif()
add_compile_options(-stdlib=libc++)
add_link_options(-stdlib=libc++ -lstdc++fs)

# use std::filesystem instead of boost::filesystem
add_compile_definitions(BOOST_PROCESS_USE_STD_FS)

# avoid system executor
add_compile_definitions(BOOST_ASIO_NO_TS_EXECUTORS)

# --------------------------------------------------------------------------------------------------
#
# find boost
# https://github.com/madmongo1/blog-december-2020/blob/master/CMakeLists.txt
#
find_package(Boost 1.88 COMPONENTS thread filesystem process program_options OPTIONAL_COMPONENTS system REQUIRED)
find_package(OpenSSL COMPONENTS)
find_package(Threads)

link_libraries(Threads::Threads)
link_libraries(Boost::thread Boost::atomic)

# --------------------------------------------------------------------------------------------------

enable_testing(false)
add_subdirectory(lib)
add_subdirectory(bin)
add_subdirectory(echo)
add_subdirectory(process)
add_subdirectory(test)

# --------------------------------------------------------------------------------------------------
