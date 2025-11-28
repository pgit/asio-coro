/**
 * HTTP/1.1 server accepting JSON documents and returning them pretty-printed.
 *
 * Shows how to use Boost.Beast for asynchronously reading/writing a HTTP request/response pair.
 *
 * The computationally intensive JSON parsing/serializing is delegated to a thread pool. This works,
 * but is slower than running everything in a multi-threaded executor in the first place. As there
 * is no need for any synchronization, this approach works fine.
 */
#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <jsoncons/json.hpp>

using namespace boost::asio;
using namespace boost::beast;
using namespace jsoncons;
using ip::tcp;

thread_pool pool{20};
awaitable<void> session(tcp::socket socket)
{
   flat_buffer buffer;
   for (;;)
   {
      http::request<http::string_body> request;
      co_await http::async_read(socket, buffer, request);
      co_await post(bind_executor(pool));

      http::response<http::string_body> response;
      try
      {
         response.result(http::status::ok);
         response.set(http::field::content_type, "application/json");
         encode_json_pretty(json::parse(request.body()), response.body());
      }
      catch (json_exception& jex)
      {
         response.result(http::status::bad_request);
         response.set(http::field::content_type, "text/plain");
         response.body() = jex.what();
      }
      response.body() += "\r\n";
      response.prepare_payload();

      co_await post(deferred);
      co_await http::async_write(socket, response);
   }
}

awaitable<void> server(tcp::acceptor a)
{
   for (;;)
      co_spawn(a.get_executor(), session(co_await a.async_accept()), detached);
}

int main()
{
   io_context context;
   co_spawn(context, server({context, {tcp::v6(), 55555}}), detached);
   context.run();
}
