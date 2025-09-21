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
