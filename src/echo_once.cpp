#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;

void echo(tcp::socket s)
{
   char data[1024];
   for (;;)
   {
      std::size_t n = s.read_some(buffer(data));
      write(s, buffer(data, n));
   }
}

void listen(tcp::acceptor a)
{
   for (;;)
      echo(a.accept());
}

int main()
{
   io_context ctx;
   listen({ctx, {tcp::v4(), 55555}});
}