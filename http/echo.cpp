/**
 * HTTP/1.1 server echoing the body of incoming requests.
 *
 * The echoing is done without reading the entire body into memory, in a streaming fashion.
 */
#include "literals.hpp"
#include "program_options.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

using namespace boost::asio;
using namespace boost::beast;
using ip::tcp;

awaitable<void> session(tcp::socket socket)
{
   socket.set_option(ip::tcp::no_delay(true));

   flat_buffer buffer;
   for (;;)
   {
      //
      // prepare request and read headers
      //
      http::request_parser<http::buffer_body> parser;
      auto n = co_await http::async_read_header(socket, buffer, parser);

      auto& req = parser.get();

      //
      // prepare response and write header
      //
      http::response<http::buffer_body> res{http::status::ok, req.version()};

      // Transfer-Encoding has precedence over Content-Length
      if (req.chunked())
         res.chunked(true);
      else if (req.has_content_length())
         res.content_length(parser.content_length());
      else if (parser.is_done())
         res.content_length(0);

      http::response_serializer<http::buffer_body> sr{res};
      co_await http::async_write_header(socket, sr);

      //
      // streaming echo of request body
      //
      assert(parser.is_header_done());
      assert(!parser.need_eof());

      parser.body_limit(boost::none);
      sr.limit(0);

      while (!sr.is_done())
      {
         std::array<char, 64_k> data;
         parser.get().body().data = data.data();
         parser.get().body().size = data.size();

         auto [ec, n] = co_await http::async_read(socket, buffer, parser, as_tuple);
         if (ec && ec != http::error::need_buffer)
            throw system_error(ec);

         assert(parser.get().body().size <= data.size());
         res.body().data = data.data();
         res.body().size = data.size() - parser.get().body().size; // unused bytes after reading
         res.body().more = !parser.is_done();

         // nothing read, but parser wants to continue? happens after reading the chunk header
         if (!res.body().size && res.body().more)
            continue;

         std::tie(ec, n) = co_await http::async_write(socket, sr, as_tuple);
         if (ec && ec != http::error::need_buffer)
            throw system_error(ec);
      }
   }
}

awaitable<void> server(tcp::acceptor a)
{
   for (;;)
      co_spawn(a.get_executor(), session(co_await a.async_accept()), detached);
}

int main(int argc, char* argv[])
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   return run(context, argc, argv);
}
