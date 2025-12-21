/**
 * HTTP/1.1 server accepting a JSON schema and then documents to be verified against it.
 *
 * curl http://localhost:55555/schema -d @test/data/schema.json
 * curl http://localhost:55555 -d @test/data/64KB-min.json
 * curl http://localhost:55555 -d '[{"x": "123456789012345", "y": 5678}]'
 */
#include "program_options.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpath/jsonpath.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>

#include <shared_mutex>

using namespace boost::asio;
using namespace boost::beast;
using namespace jsoncons;
using ip::tcp;

awaitable<void> session(tcp::socket socket)
{
   static std::shared_mutex schema_mutex;
   static std::optional<jsonschema::json_schema<json>> schema;
   flat_buffer buffer;
   for (;;)
   {
      http::request<http::string_body> request;
      co_await http::async_read(socket, buffer, request);

      http::response<http::string_body> response;
      try
      {
         if (request.target() == "/schema")
         {
            auto new_schema = jsonschema::make_json_schema(json::parse(request.body()));
            auto lock = std::unique_lock{schema_mutex};
            schema = std::move(new_schema);
            response.result(http::status::ok);
            response.body() = "schema set";
         }
         else if (request.target() == "/")
         {
            auto j = json::parse(request.body());
            auto lock = std::shared_lock{schema_mutex};
            if (!schema)
               throw std::runtime_error("please set schema first at /schema");
            json_decoder<json> decoder;
            schema->validate(j, decoder);
            response.result(http::status::ok);
            response.set(http::field::content_type, "application/json");
            encode_json_pretty(decoder.get_result(), response.body());
         }
         else
         {
            throw std::runtime_error{std::format("{} not found", request.target())};
         }
      }
      catch (std::runtime_error& ex)
      {
         response.result(http::status::bad_request);
         response.set(http::field::content_type, "text/plain");
         response.body() = ex.what();
      }
      response.body() += '\n';
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
