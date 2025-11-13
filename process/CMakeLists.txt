link_libraries(Boost::process)

file(GLOB SRC_FILES "*.cpp")
foreach(src_file ${SRC_FILES})
   get_filename_component(exe_name ${src_file} NAME_WE)
   add_executable(${exe_name} ${src_file})
endforeach()
