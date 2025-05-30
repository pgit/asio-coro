cmake_minimum_required(VERSION 3.30)
project(asio-coro VERSION 0.0.1)

set(CMAKE_EXPORT_COMPILE_COMMANDS On)
set(CMAKE_CXX_STANDARD 23)

# FIXME: try io_uring
# add_compile_definitions(BOOST_ASIO_HAS_FILE)
# add_compile_definitions(BOOST_ASIO_HAS_IO_URING_AS_DEFAULT)
# add_compile_definitions(BOOST_ASIO_DISABLE_EPOLL)
# add_compile_definitions(BOOST_ASIO_HAS_IO_URING_AS_DEFAULT)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    # add_compile_definitions(BOOST_ASIO_ENABLE_BUFFER_DEBUGGING)

    # ASIO handler tracking output (very noisy)
    # add_compile_definitions(BOOST_ASIO_ENABLE_HANDLER_TRACKING)
    
    # thread sanitizer
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    
    # address sanitizer
    # add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    # add_link_options(-fsanitize=address)

    # include(CodeCoverage)
    # append_coverage_compiler_flags()
    # SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-generate -fcoverage-mapping")
endif()
add_compile_options(-stdlib=libc++)
add_link_options(-stdlib=libc++ -lstdc++fs)

# needed for ranges/v3 starting with clang 19
add_compile_options(-Wno-deprecated-declarations)

# --------------------------------------------------------------------------------------------------
#
# find boost
# https://github.com/madmongo1/blog-december-2020/blob/master/CMakeLists.txt
#
find_package(Boost 1.78 COMPONENTS system thread filesystem process program_options REQUIRED)
find_package(OpenSSL COMPONENTS)
find_package(Threads)

find_package(fmt)

link_libraries(Threads::Threads)
link_libraries(OpenSSL::SSL)
link_libraries(Boost::system Boost::thread Boost::atomic)

# --------------------------------------------------------------------------------------------------

add_subdirectory(src)
add_subdirectory(test)
