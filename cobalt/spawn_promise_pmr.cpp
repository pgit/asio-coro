/**
 * Custom PMR to show allocations.
 */
#include <boost/asio.hpp>

#include <boost/cobalt.hpp>
#include <boost/cobalt/main.hpp>

#include <print>
#include <memory_resource>
#include <optional>
#include "run.hpp"

using namespace boost;
using namespace boost::asio;
using namespace std::chrono_literals;

namespace
{
class logging_resource final : public std::pmr::memory_resource
{
public:
   explicit logging_resource(std::pmr::memory_resource& upstream) noexcept
      : upstream_(upstream)
   {
   }

protected:
   void* do_allocate(std::size_t bytes, std::size_t alignment) override
   {
      void* ptr = upstream_.allocate(bytes, alignment);
      std::println("[pmr] allocate {} bytes (align {}) -> {}", bytes, alignment, ptr);
      return ptr;
   }

   void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) noexcept override
   {
      std::println("[pmr] deallocate {} bytes (align {}) <- {}", bytes, alignment, p);
      upstream_.deallocate(p, bytes, alignment);
   }

   bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
   {
      return this == &other;
   }

private:
   std::pmr::memory_resource& upstream_;
};

class cobalt_thread_context_guard
{
public:
   cobalt_thread_context_guard(asio::any_io_executor exec,
                               std::pmr::memory_resource& resource)
      : previous_resource_(cobalt::this_thread::set_default_resource(&resource))
   {
      try
      {
         previous_executor_.emplace(cobalt::this_thread::get_executor());
      }
      catch (const std::exception&)
      {
         previous_executor_.reset();
      }
      cobalt::this_thread::set_executor(std::move(exec));
   }

   ~cobalt_thread_context_guard()
   {
      cobalt::this_thread::set_default_resource(previous_resource_);
      if (previous_executor_)
      {
         cobalt::this_thread::set_executor(*previous_executor_);
      }
   }

private:
   std::optional<cobalt::executor> previous_executor_;
   std::pmr::memory_resource* previous_resource_;
};
}

cobalt::promise<void> sleep(std::string message, steady_timer::duration timeout)
{
   steady_timer timer(co_await cobalt::this_coro::executor);
   timer.expires_after(timeout);
   std::println("sleeping: {}...", message);
   auto [ec] = co_await timer.async_wait(as_tuple);
   std::println("sleeping: {}... done ({})", message, ec.message());
}

static cobalt::task<void> task()
{
   auto ex = co_await cobalt::this_coro::executor;
   auto promise = sleep("long time", 10s);
   co_await sleep("delay", 1s);
   co_await race(std::move(promise), sleep("short time", 1s));
}

int main()
{
   boost::asio::io_context context;

   // Mirror cobalt::main by giving this thread its own executor + PMR pool.
   std::pmr::unsynchronized_pool_resource cobalt_pool{std::pmr::get_default_resource()};
   logging_resource cobalt_logger{cobalt_pool};
   cobalt_thread_context_guard cobalt_scope{context.get_executor(), cobalt_logger};
   cobalt::spawn(context, task(), detached);  
   runDebug(context);
}
