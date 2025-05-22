cmake_minimum_required(VERSION 3.30)
project(asio_coro VERSION 0.0.1)

set(CMAKE_EXPORT_COMPILE_COMMANDS On)
set(CMAKE_CXX_STANDARD 23)

# --------------------------------------------------------------------------------------------------
#
# find boost
# https://github.com/madmongo1/blog-december-2020/blob/master/CMakeLists.txt
#
find_package(Boost 1.78 COMPONENTS system thread filesystem program_options REQUIRED)
find_package(OpenSSL COMPONENTS)
find_package(Threads)

find_package(fmt)

link_libraries(Threads::Threads)
link_libraries(OpenSSL::SSL)
link_libraries(Boost::system Boost::thread Boost::atomic)

# --------------------------------------------------------------------------------------------------
#
# Project definition
#
file(GLOB SRC_FILES "*.cpp")
foreach(src_file ${SRC_FILES})
   get_filename_component(exe_name ${src_file} NAME_WE)
   add_executable(${exe_name} ${src_file})
endforeach()
