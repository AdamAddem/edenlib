#pragma once
#include "../typedefs.hpp"
#include <atomic>
#include <bit>
#include <memory>
#include <optional>
#include <utility>

template <class T, sz_t N>
requires (std::has_single_bit(N))
class SPSCQueue {
  static constexpr auto Tsz = sizeof(T);

  // element not byte index
  struct {
    alignas(64) std::atomic<sz_t> begin{};
    alignas(64) std::atomic<sz_t> end{};
  }m;

  alignas(T)
  byte_t data[N * Tsz];

public:

  sz_t size()  { return m.end.load() - m.begin.load(); }
  bool full()  { return size() == N; }
  bool empty() { return size() == 0; }
  
  std::optional<T> 
  try_pop() {
    auto const b = m.begin.load(std::memory_order_relaxed);
    auto const e = m.end.load(std::memory_order_acquire);
    if(e - b == 0) return std::nullopt;

    auto const idx = b & (N - 1);
    auto const obj_ptr = (T*)(&data[Tsz * idx]);
    
    std::optional<T> res{std::in_place, std::move( *obj_ptr )};
    std::destroy_at(obj_ptr);
    m.begin.store(b + 1, std::memory_order_release);
    return res;
  }

  // returns true on failure
  bool try_push(T&& element) {
    auto const b = m.begin.load(std::memory_order_acquire);
    auto const e = m.end.load(std::memory_order_relaxed);
    if(e - b == N) return true;
    
    auto const idx = e & (N - 1);
    std::construct_at( (T*)&data[Tsz * idx], std::move(element) );
    m.end.store(e + 1, std::memory_order_release);
    return false;
  }

  ~SPSCQueue() {
    // destructor logic here
  }
  
};