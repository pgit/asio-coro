#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;

void session(tcp::socket s)
{
   std::array<char, 1460> data;
   for (;;)
   {
      boost::system::error_code ec;
      std::size_t n = s.read_some(buffer(data), ec);
      if (ec == error::eof)
         return;
      write(s, buffer(data, n));
   }
}

void server(tcp::acceptor a)
{
   for (;;)
      std::thread(session, a.accept()).detach();  // was: session(a.accept());
}

int main()
{
   io_context ctx;
   server({ctx, {tcp::v6(), 55555}});
}