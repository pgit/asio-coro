file(GLOB SRC_FILES "*.cpp")
foreach(src_file ${SRC_FILES})
   get_filename_component(exe_name ${src_file} NAME_WE)
   add_executable(${exe_name} ${src_file})
endforeach()

target_link_libraries(process PUBLIC Boost::process)
target_link_libraries(process_signal PUBLIC Boost::process)
target_link_libraries(process_timeout PUBLIC Boost::process)
