/**
 * https://github.com/boostorg/process/issues/516, fixed in Boost 1.90
 *
 * This wouldn't compile before Boost 1.90 if that wasn't also compiled with
 * BOOST_PROCESS_USE_STD_FS. Note that the 1.89 in `cpp-devcontainer` does that.
 */
#include <boost/process/v2/environment.hpp>
#include <boost/process/v2/process.hpp>

using namespace boost::asio;
namespace bp = boost::process::v2;

int main()
{
   io_context context;
   auto executor = context.get_executor();
   bp::process child(executor, "/usr/bin/true", {}, 
      bp::process_environment{{"X=Y"}});
}
