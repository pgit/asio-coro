#include <boost/capy.hpp>
#include <boost/corosio.hpp>

using namespace boost::corosio;
using namespace boost::capy;

task<void> handle_connection(tcp_socket socket)
{
   std::array<char, 64 * 1024> data;
   for (;;)
   {
      auto [ec, n] = co_await socket.read_some(mutable_buffer(data.data(), data.size()));
      if (ec == error::eof)
         break;

      auto [wec, wn] = (co_await write(socket, const_buffer(data.data(), n)));
      if (wec)
         break;
   }
   
   socket.close();
}

task<void> server(io_context& ioc, tcp_acceptor acc)
{
   for (;;)
   {
      tcp_socket peer(ioc);
      std::ignore = co_await acc.accept(peer);
      run_async(ioc.get_executor())(handle_connection(std::move(peer)));
   }
}

int main()
{
   io_context context;
   any_executor ex = context.get_executor();
   tcp_acceptor acceptor(ex, endpoint{55555});
   run_async(ex)(server(context, std::move(acceptor)));

   std::vector<std::jthread> threads;
   threads.reserve(std::thread::hardware_concurrency());
   for (size_t i = 0; i < threads.capacity(); ++i)
      threads.emplace_back([&]() { context.run(); });

   context.run();
}