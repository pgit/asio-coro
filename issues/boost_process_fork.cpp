#undef BOOST_PROCESS_USE_STD_FS
#define BOOST_PROCESS_V2_DISABLE_NOTIFY_FORK

#include <boost/asio.hpp>
#include <boost/process/v2/process.hpp>

#include <print>
#include <ranges>
#include <thread>

using namespace boost::asio;
namespace bp = boost::process::v2;
namespace rv = std::ranges::views;

awaitable<int> execute(boost::filesystem::path path, std::vector<std::string> args = {})
{
   bp::process child(co_await this_coro::executor, path, args);
   co_return co_await child.async_wait();
}

int main()
{
   io_context context;
   for (size_t i = 0; i < 100; ++i)
      co_spawn(context, execute("/usr/bin/sleep", {"1"}), detached);

   auto threads = rv::iota(0) | rv::take(std::max(1u, std::thread::hardware_concurrency())) |
                  rv::transform([&](int) { return std::jthread([&] { context.run(); }); }) |
                  std::ranges::to<std::vector>();

   std::println("number of threads: {}", threads.size());
   context.run();
}
