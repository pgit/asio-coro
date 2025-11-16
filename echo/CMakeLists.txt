file(GLOB SRC_FILES "*.cpp")
foreach(src_file ${SRC_FILES})
   get_filename_component(exe_name ${src_file} NAME_WE)
   add_executable(${exe_name} ${src_file})
endforeach()

# TEST: Use io_uring for 'echo_coro'.
# target_compile_definitions(echo_coro PRIVATE BOOST_ASIO_HAS_IO_URING BOOST_ASIO_DISABLE_EPOLL)
# target_link_libraries(echo_coro PRIVATE uring)
