/**
 * HTTP/1.1 server accepting JSON documents and returning them pretty-printed.
 * On invalid input, a detailed error message is put into the response instead.
 *
 * Shows how to use Boost.Beast for asynchronously reading/writing a HTTP request/response pair.
 *
 * Uses 'rapidjson' for JSON parsing and pretty printing. The parser is used in "in situ",
 * which means that the strings in the parsed DOM are referencing the source buffer directly.
 * The buffer is modified by that.
 */
#include "program_options.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

using namespace boost::asio;
using namespace boost::beast;
using namespace rapidjson;
using ip::tcp;

awaitable<void> session(tcp::socket socket)
{
   flat_buffer buffer;
   for (;;)
   {
      http::request<http::string_body> request;
      co_await http::async_read(socket, buffer, request);

      http::response<http::string_body> response;
      response.result(http::status::ok);
      response.set(http::field::content_type, "application/json");

      Document doc;
      if (!doc.ParseInsitu(const_cast<char*>(request.body().c_str())).HasParseError())
      {
         rapidjson::StringBuffer buffer;
         rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
         doc.Accept(writer);
         response.body() = std::move(buffer).GetString();
      }
      else
      {
         response.result(http::status::bad_request);
         response.set(http::field::content_type, "text/plain");
         response.body() = std::format("offset {}: {}", doc.GetErrorOffset(),
                                       GetParseError_En(doc.GetParseError()));
      }
      response.body() += "\n";
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
