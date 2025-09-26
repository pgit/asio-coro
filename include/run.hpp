#pragma once

#include <boost/asio/io_context.hpp>

// =================================================================================================

/**
 * Unless \c NDEBUG is defined, runs the given IO context just like \c io_context::run() would, but
 * with logging a separator line between each \c run_one() step and some timing information.
 */
size_t run(boost::asio::io_context& context);

// =================================================================================================
