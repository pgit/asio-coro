#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;

void session(tcp::socket socket)
{
   std::array<char, 64 * 1024> data;
   for (;;)
   {
      boost::system::error_code ec;
      std::size_t n = socket.read_some(buffer(data), ec);
      if (ec == error::eof)
         return;
      write(socket, buffer(data, n));
   }
}

void server(tcp::acceptor acceptor)
{
   for (;;)
      session(acceptor.accept());
}

int main()
{
   io_context context;
   server(tcp::acceptor{context, {tcp::v6(), 55555}});
}