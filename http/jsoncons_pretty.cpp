/**
 * HTTP/1.1 server accepting JSON documents and returning them pretty-printed.
 * On invalid input, a detailed error message is put into the response instead.
 *
 * Shows how to use Boost.Beast for asynchronously reading/writing a HTTP request/response pair.
 */
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
      http::request<http::string_body> request;
      co_await http::async_read(socket, buffer, request);

      http::response<http::string_body> response;
      try
      {
         response.result(http::status::ok);
         response.set(http::field::content_type, "application/json");
         auto j = json::parse(request.body());
         encode_json_pretty(j, response.body());
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
      co_spawn(a.get_executor(), session(co_await a.async_accept()), detached);
}

int main(int argc, char* argv[])
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   return run(context, argc, argv);
}
