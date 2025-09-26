include_directories("../include")

file(GLOB SRC_FILES "*.cpp")
add_library(asio_coro STATIC ${SRC_FILES})
