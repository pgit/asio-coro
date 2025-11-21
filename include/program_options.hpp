#pragma once

#include <boost/asio/io_context.hpp>

#include <cstddef>

int run(boost::asio::io_context& context, int argc, char* argv[]);
