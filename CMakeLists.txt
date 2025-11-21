cmake_minimum_required(VERSION 3.30)
project(asio-coro VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_EXPORT_COMPILE_COMMANDS On)

if (DEFINED ENV{GITHUB_ACTIONS})
    add_compile_definitions(GITHUB_ACTIONS)
endif()

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

# --------------------------------------------------------------------------------------------------
#
# find boost
# https://github.com/madmongo1/blog-december-2020/blob/master/CMakeLists.txt
#
find_package(Boost 1.88 COMPONENTS thread filesystem process program_options OPTIONAL_COMPONENTS system REQUIRED)
find_package(OpenSSL COMPONENTS)
find_package(Threads)

link_libraries(Threads::Threads)
link_libraries(Boost::thread Boost::atomic Boost::program_options)

#
# range-v3 -- FIXME: needed only for views:chunked(),
#                    as the libc++ version doesn't seem to be available, yet
#
include_directories(/usr/local/include/range-v3)
add_compile_options(-Wno-deprecated-declarations) # needed for ranges/v3 starting with clang 19

#
# io_uring
#
pkg_check_modules(uring REQUIRED IMPORTED_TARGET liburing)

# --------------------------------------------------------------------------------------------------

enable_testing(false)
add_subdirectory(lib)

include_directories(include)
link_libraries(asio_coro)

add_subdirectory(bin)
add_subdirectory(echo)
add_subdirectory(http)
add_subdirectory(net)
add_subdirectory(process)
add_subdirectory(test)

# --------------------------------------------------------------------------------------------------
