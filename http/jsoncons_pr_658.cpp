/**
 * https://github.com/danielaparker/jsoncons/pull/658
 *
 * When using a memory resource that does not always align memory, jsoncons would deallocate with
 * a slightly larger 'size' parameter sometimes. Discovered in jsoncons-1.43, fixed by PR above.
 */
#include <jsoncons/json.hpp>

using namespace jsoncons;

class checked_resource : public std::pmr::memory_resource
{
public:
   explicit checked_resource(std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
      : upstream_(upstream)
   {
   }

   ~checked_resource() { assert(map.empty()); }

protected:
   void* do_allocate(std::size_t bytes, std::size_t alignment) override
   {
      auto p = upstream_->allocate(bytes, alignment);
      assert((reinterpret_cast<std::size_t>(p) % alignment) == 0);
      map[p] = bytes;
      std::println("    allocate: 0x{} {} alignment {}", p, bytes, alignment);
      return p;
   }

   void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
   {
      std::string message;
      if (auto it = map.find(p); it == map.end())
         message = "\x1b[1;31mNOT FOUND\x1b[0m";
      else
      {
         if (it->second != bytes) // shouldn't happen any more after PR 658
            message = std::format("\x1b[31msize mismatch: {} vs {}\x1b[0m", it->second, bytes);
         else
            message = "OK";
         map.erase(it);
      }
      std::println("  deallocate: 0x{} {} alignment {} {}", p, bytes, alignment, message);
      upstream_->deallocate(p, bytes, alignment);
   }

   bool do_is_equal(const memory_resource& other) const noexcept override { return this == &other; }

private:
   std::map<void*, size_t> map;
   std::pmr::memory_resource* upstream_;
};

void decode(std::string_view str)
{
   std::array<char, 64 * 1024> buffer;
   std::pmr::monotonic_buffer_resource resource{buffer.data(), buffer.size()};
   checked_resource checked_resource{&resource};
   std::pmr::polymorphic_allocator<char> allocator{&checked_resource};

   json_decoder<pmr::json> decoder(allocator);
   json_string_reader reader(str, decoder);
   reader.read_next();
   auto j = decoder.get_result();
}

int main() { decode(R"("123456789012345")"); }
