/**
 * HTTP/1.1 server accepting JSON documents and returning them pretty-printed.
 *
 * Shows how to use Boost.Beast for asynchronously reading/writing a HTTP request/response pair.
 */
#include "asio-coro.hpp"
#include "program_options.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <jsoncons/json.hpp>

using namespace boost::asio;
using namespace boost::beast;
using namespace jsoncons;
using ip::tcp;

awaitable<void> session(tcp::socket socket)
{
   flat_buffer buffer;
   for (;;)
   {
      http::request_parser<http::string_body> parser;
      parser.body_limit(boost::none);
      co_await http::async_read(socket, buffer, parser);
      auto request = parser.release();

      http::response<http::string_body> response;
      try
      {
         response.result(http::status::ok);
         response.set(http::field::content_type, "application/json");
         encode_json_pretty(json::parse(request.body()), response.body());
      }
      catch (json_exception& ex)
      {
         response.result(http::status::bad_request);
         response.set(http::field::content_type, "text/plain");
         response.body() = ex.what();
      }
      response.body() += "\r\n";
      response.prepare_payload();

      co_await http::async_write(socket, response);
   }
}

awaitable<void> server(tcp::acceptor a)
{
   for (;;)
      co_spawn(a.get_executor(), session(co_await a.async_accept()), log_exception());
}

int main(int argc, char* argv[])
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   return run(context, argc, argv);
}
