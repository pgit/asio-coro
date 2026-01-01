file(GLOB SRC_FILES "*.cpp")
foreach(src_file ${SRC_FILES})
   get_filename_component(exe_name ${src_file} NAME_WE)
   add_executable(${exe_name} ${src_file})
endforeach()

target_link_libraries(handle_signal Boost::program_options)
target_link_libraries(client Boost::program_options)
target_link_libraries(wait_for_port Boost::program_options)

target_compile_definitions(file_io PRIVATE BOOST_ASIO_HAS_IO_URING)
target_link_libraries(file_io PUBLIC uring)
