#include "asio-coro.hpp"

class session : public std::enable_shared_from_this<session>
{
public:
   session(tcp::socket socket) : socket_(std::move(socket))
   {
      std::println("new connection from {}", socket_.remote_endpoint());
   }

   void start() { do_read(); }

private:
   void do_read()
   {
      auto self(shared_from_this());
      socket_.async_read_some(asio::buffer(data_),
                              [this, self](error_code ec, std::size_t length)
                              {
                                 if (!ec)
                                    do_write(length);
                              });
   }

   void do_write(std::size_t length)
   {
      auto self(shared_from_this());
      boost::asio::async_write(socket_, asio::buffer(data_, length),
                               [this, self](error_code ec, std::size_t /*length*/)
                               {
                                  if (!ec)
                                     do_read();
                               });
   }

   tcp::socket socket_;
   std::array<uint8_t, 1460> data_;
};

class server
{
public:
   server(boost::asio::io_context& io_context, tcp::endpoint endpoint)
      : acceptor_(io_context, endpoint), socket_(io_context)
   {
      std::println("listening on {}", acceptor_.local_endpoint());
      do_accept();
   }

private:
   void do_accept()
   {
      acceptor_.async_accept(socket_,
                             [this](error_code ec)
                             {
                                if (!ec)
                                   std::make_shared<session>(std::move(socket_))->start();

                                do_accept();
                             });
   }

   tcp::acceptor acceptor_;
   tcp::socket socket_;
};

int main(int argc, char* argv[])
{
   boost::asio::io_context io_context;
   server server(io_context, {tcp::v6(), 55555});
   io_context.run();
}