#include <boost/asio.hpp>

using boost::system::error_code;
using namespace boost::asio;
using namespace std::chrono_literals;

int main(int argc, char* argv[])
{
   io_context context;
   signal_set sigint(context, SIGINT);
   signal_set sigterm(context, SIGTERM);
   steady_timer timer(context);

   // Stop after the second SIGINT...
   co_spawn(
      context,
      [&]() -> awaitable<void>
      {
         for (int i = 0; i < 2; ++i)
         {
            int signum = co_await sigint.async_wait();
            std::println(" INTERRUPTED (signal {})", signum);
         }
         sigterm.cancel();
         timer.cancel();
      },
      detached);

   /// ... or the first SIGTERM...
   sigterm.async_wait(
      [&](error_code error, auto signum)
      {
         if (error == boost::system::errc::operation_canceled)
            return;
         std::println(" TERMINATED (signal {})", signum);
         sigint.cancel();
         timer.cancel();
      });

   /// ... or 10s, whichever comes first.
   timer.expires_after(10s);
   timer.async_wait(
      [&](error_code err)
      {
         sigint.cancel();
         sigterm.cancel();
      });

   // setvbuf(stdout, nullptr, _IONBF, 0); // disable buffering
   
   std::println("running IO context...");
   context.run();
   std::println("running IO context... done");
}
