#pragma once

#include <boost/beast/core/stream_traits.hpp>

template <typename T>
concept AsyncReadStream = boost::beast::is_async_read_stream<T>::value;

template <typename T>
concept AsyncWriteStream = boost::beast::is_async_write_stream<T>::value;
