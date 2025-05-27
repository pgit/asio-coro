#include "utils.hpp"

/**
 * Unless \c NDEBUG is defined, runs the given IO context just like \c io_context::run() would, but
 * with logging a separator line between each \c run_one() step and some timing information.
 */
size_t run(boost::asio::io_context& context)
{
#if defined(NDEBUG)
   return context.run();
#else
   size_t i = 0;
   using namespace std::chrono;   
   for (auto t0 = steady_clock::now(); context.run_one(); ++i)
   {
      auto t1 = steady_clock::now();
      auto dt = duration_cast<milliseconds>(t1 - t0);
      t0 = t1;
      // clang-format off
      if (dt < 100ms)
         std::println("--- {} ------------------------------------------------------------------------", i);
      else
         std::println("\x1b[1;31m--- {} ({}) ----------------------------------------------------------------\x1b[0m", i, dt);
      // clang-format off
   }
   return i;
#endif
}
