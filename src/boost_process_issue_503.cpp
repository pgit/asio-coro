// https://github.com/boostorg/process/issues/503, fixed in Boost 1.89
#include <boost/asio.hpp>
#include <boost/process/v2/process.hpp>
#include <iostream>

int main()
{
   boost::asio::io_context context;
   boost::process::v2::process child(context, "/usr/bin/sleep", {"10"});
   child.async_wait([](boost::system::error_code, int status){
      std::cout << status << std::endl;
   });
   child.terminate();  // prints 0 -- should be 9 (SIGKILL)
   // child.interrupt();  // prints 2 (SIGINT)
   // child.request_exit();  // prints 15 (SIGTERM)
   context.run();
}
