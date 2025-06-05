#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;

void session(tcp::socket s)
{
   std::array<char, 1024> data;
   for (;;)
   {
      boost::system::error_code ec;
      std::size_t n = s.read_some(buffer(data), ec);
      if (ec == error::eof)
         return;
      write(s, buffer(data, n));
   }
}

void listen(tcp::acceptor a)
{
   for (;;)
      session(a.accept());
}

int main()
{
   io_context ctx;
   listen({ctx, {tcp::v6(), 55555}});
}