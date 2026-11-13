#pragma once
#include "../metaprogramming/packs.hpp"
#include "../type_flags.hpp"
#include "../type_aliases.hpp"

#include <cstring>
#include <utility>
#include <span>
#include <tuple>
#include <new>
#include <type_traits>
#include <cassert>

namespace eden {

namespace detail {

template<class... Ts>
class Slices {
  static constexpr packs::PackData<Ts...> pack_data{};
  static constexpr auto buffer_begin_idx = pack_data.alignidx_to_typeidx[0];
  std::tuple<Ts*...> ptrs{};
  
  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void
  destroy_all_impl(sz_t size_each) noexcept {
    if(size_each == 0) return;
    if constexpr(sizeof...(Rest)) destroy_all_impl<idx + 1, Rest...>(size_each);
  
    if constexpr(not std::is_trivially_destructible_v<First>) {
      auto const begin = get<idx>();
      auto end = begin + size_each - 1;
      while(true) {
        std::destroy_at(end);
        if(end == begin) break;
        end -= 1;
      }
    }
  }

  template<sz_t idx, class First, class... Rest>
  edenInlineCXPR void 
  reslice_with_impl(byte_t* alloc, sz_t new_capacity_each) noexcept {
    auto const slice = alloc + pack_data.alignidx_to_offset[ pack_data.typeidx_to_alignidx[idx] ] * new_capacity_each;
    get<idx>() = std::start_lifetime_as_array<First>(slice, new_capacity_each);
    if constexpr(sizeof...(Rest))
      return reslice_with_impl<idx + 1, Rest...>(alloc, new_capacity_each);
  }
  
  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void
  relocate_impl(Slices& new_slices, sz_t size_each) noexcept {
    auto old_ptr = get<idx>();
    auto construct_location = new_slices.get<idx>();
  
    if constexpr(edenTriviallyRelocatable(First))
      std::memcpy( construct_location, old_ptr, size_each * sizeof(First) );
    else
      for(sz_t i{}; i<size_each; ++i)
        std::construct_at( construct_location + i, std::move(old_ptr[i]) ), 
        std::destroy_at( old_ptr + i ); // technically not reverse order but who really cares
  
    if constexpr(sizeof...(Rest))
      relocate_impl<idx + 1, Rest...>(new_slices, size_each);
  }
  
  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void
  add_to_end_unchecked_impl(sz_t size_each, auto&& first_args, auto&&... rest_args) noexcept {
    auto end = get<idx>() + size_each;
  
    std::apply(
      [end]<class... Args>(Args&&... args) {
        std::construct_at( end, std::forward<Args>(args)... );
      },
      std::forward<decltype(first_args)>(first_args)
    );
  
    if constexpr(sizeof...(Rest))
      add_to_end_unchecked_impl<idx + 1, Rest...>(size_each, std::forward<decltype(rest_args)>(rest_args)...);
  }

  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void 
  value_initialize_elements_impl(sz_t count) noexcept {
    auto begin = get<idx>();

    if constexpr(std::is_trivially_default_constructible_v<First>)
      std::memset(begin, 0, count * sizeof(First));
    else
      for(sz_t i{}; i<count; ++i) std::construct_at(begin);

    if constexpr(sizeof...(Rest))
      return value_initialize_elements_impl<idx + 1, Rest...>(count);
  }

  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void
  pop_back_impl(sz_t size_each) noexcept {
    if constexpr(not std::is_trivially_destructible_v<First>) {
      auto begin = get<idx>();
      for(sz_t i{}; i<size_each; ++i) std::destroy_at(begin); // not reverse order but who cares
    }
    
    if constexpr(sizeof...(Rest))
      return pop_back_impl<idx + 1, Rest...>(size_each);
  }
  
public:
  
  template<sz_t idx> edenInlineCXPR auto& get() noexcept { return std::get<idx>(ptrs); }
  template<sz_t idx> edenInlineCXPR auto& get() const noexcept { return std::get<idx>(ptrs); }
  edenInlineNodiscardCXPR auto buffer_begin_slice() noexcept { return std::get<buffer_begin_idx>(ptrs); }
  
  edenInlineCXPR void add_to_end_unchecked(sz_t size_each, auto&&... args) noexcept { return add_to_end_unchecked_impl<0, Ts...>(size_each, std::forward<decltype(args)>(args)...); }
  edenInlineCXPR void destroy_all(sz_t size_each) noexcept { return destroy_all_impl<0, Ts...>(size_each); }

  // discards current slices and sets them to alloc, sliced
  edenInlineCXPR void reslice_with(void* alloc, sz_t new_capacity_each) noexcept { return reslice_with_impl<0, Ts...>((byte_t*)alloc, new_capacity_each); }

  // relocates everything to new_slices
  edenInlineCXPR void relocate_to(Slices& new_slices, sz_t size_each) noexcept { return relocate_impl<0, Ts...>(new_slices, size_each); }
  
  edenInlineCXPR void value_initialize_elements(sz_t count) noexcept { return value_initialize_elements_impl<0, Ts...>(count); }

  edenInlineCXPR void pop_back(sz_t size_each) noexcept { return pop_back_impl<0, Ts...>(size_each); }
};

}

// TODO: 
// - Add settings and custom allocator support
// - Explore potential size optimization where we only store one pointer and two sz_ts, slicing is done on-demand instead. Probably not very performant but it may matter.
template <bool Small = false, class... Ts>
requires (sizeof...(Ts) > 1)
class contiguous_soa_t {
  static constexpr auto NumTs = sizeof...(Ts);
  static constexpr packs::PackData<Ts...> pack_data{};
  static constexpr auto FirstAllocCapacity = 2;

  detail::Slices<Ts...> slices;

public: using count_t = std::conditional_t<Small, u32_t, sz_t>; private:

  count_t size_each{};
  count_t capacity_each{};

  static constexpr bool doAlignedAllocation = pack_data.biggest_alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__;

  edenInlineNodiscardCXPR static count_t grow(count_t n) noexcept { return n * 2; }
  
  edenInlineNodiscard static void* 
  allocate(sz_t num_bytes) noexcept {
    if constexpr(doAlignedAllocation)
      return ::operator new(num_bytes, (align_t) pack_data.biggest_alignment); 
    else 
      return ::operator new(num_bytes); 
  }
  
  edenInlineCXPR static void 
  deallocate_at(void* alloc, sz_t alloc_size_bytes) noexcept { 
    if constexpr(doAlignedAllocation)
      ::operator delete(alloc, alloc_size_bytes, (align_t) pack_data.biggest_alignment);
    else 
      ::operator delete(alloc, alloc_size_bytes);
  }

  edenInlineCXPR void deallocate() noexcept { deallocate_at(slices.buffer_begin_slice(), buffer_size_bytes()); }

  edenInlineNodiscardCXPR sz_t buffer_size_bytes() const noexcept { return capacity_each * pack_data.total_size; }

  edenInlineCXPR void 
  destroy_all() noexcept { 
    slices.destroy_all(size_each);
    size_each = 0; 
  }

  void expand_to(count_t new_capacity_each) noexcept {
    assert(new_capacity_each >= capacity_each);
    auto const old_buffer_size_bytes = buffer_size_bytes();
    capacity_each = new_capacity_each;
    auto const new_alloc = allocate(buffer_size_bytes());
    auto const old_alloc = slices.buffer_begin_slice();
    
    auto old_slices = slices;
    slices.reslice_with(new_alloc, new_capacity_each);

    // this branch isn't strictly necessary
    // TODO: benchmark with and without
    if(size_each != 0) old_slices.relocate_to(slices, size_each);
    deallocate_at(old_alloc, old_buffer_size_bytes);
  }

  void allocate_from_empty(count_t new_capacity_each = FirstAllocCapacity) noexcept {
    assert(slices.buffer_begin_slice() == nullptr); assert(size_each == 0); assert(capacity_each == 0);
    capacity_each = new_capacity_each;
    auto const new_allocation = allocate( buffer_size_bytes() );
    slices.reslice_with(new_allocation, new_capacity_each);
  }

  template <class ...ArgTuples>
  void grow_and_emplace(ArgTuples&&... args_for_each_element) noexcept {
    if (slices.buffer_begin_slice() == nullptr) {
      allocate_from_empty();
      slices.add_to_end_unchecked(size_each, std::forward<ArgTuples>(args_for_each_element)...);
      ++size_each;
      return;
    }
    assert(size_each != 0);

    auto const new_capacity_each = grow(capacity_each); assert(new_capacity_each >= capacity_each);
    auto const old_buffer_size_bytes = buffer_size_bytes();
    capacity_each = new_capacity_each;
    auto const new_alloc = allocate(buffer_size_bytes());
    auto const old_alloc = slices.buffer_begin_slice();
    
    auto old_slices = slices;
    slices.reslice_with(new_alloc, new_capacity_each);
    slices.add_to_end_unchecked(size_each, std::forward<ArgTuples>(args_for_each_element)...);
    
    old_slices.relocate_to(slices, size_each);
    deallocate_at(old_alloc, old_buffer_size_bytes);
    ++size_each;
  }

public:

  constexpr contiguous_soa_t() = default;

  explicit contiguous_soa_t(count_t count) noexcept {
    allocate_from_empty(count);
    slices.value_initialize_elements(count);
    size_each = count;
  }

  template <count_t N>
  explicit contiguous_soa_t(ReserveInitial<N>) noexcept
  { allocate_from_empty(N); }

  constexpr contiguous_soa_t(contiguous_soa_t&& other) noexcept
  : slices(other.slices), size_each(other.size_each), capacity_each(other.capacity_each) {
    other.slices = {}; other.size_each = 0; other.capacity_each = 0;
  }

  contiguous_soa_t& operator=(contiguous_soa_t&& other) noexcept {
    destroy_all(); deallocate();
    slices = other.slices; other.slices = {};
    size_each = other.size_each; other.size_each = 0; 
    capacity_each = other.capacity_each; other.capacity_each = 0;
    return *this;
  }

  constexpr ~contiguous_soa_t() {
    destroy_all();
    deallocate();
  }

  edenInlineNodiscardCXPR count_t size()     const noexcept { return size_each; }
  edenInlineNodiscardCXPR count_t capacity() const noexcept { return capacity_each; }
  edenInlineNodiscardCXPR bool empty()       const noexcept { return size_each == 0; }
  edenInlineCXPR void clear() noexcept { destroy_all(); }

  template <sz_t IDX> edenInlineNodiscard auto&& front(this auto&& self) noexcept { assert(self.size_each not_eq 0); return std::forward_like<decltype(self)>(self.slices.template get<IDX>()[0]); }
  template <sz_t IDX> edenInlineNodiscard auto&& back(this auto&& self) noexcept { assert(self.size_each not_eq 0); return std::forward_like<decltype(self)>(self.slices.template get<IDX>()[self.size_each - 1]); }
  template <sz_t IDX> edenInlineNodiscardCXPR auto data(this auto&& self) noexcept { return self.slices.template get<IDX>(); }

  template <sz_t IDX> edenInlineNodiscardCXPR auto to_span()       noexcept { return std::span( data<IDX>(), size_each ); }
  template <sz_t IDX> edenInlineNodiscardCXPR auto to_span() const noexcept { return std::span( data<IDX>(), size_each ); }

  void reserve(count_t new_capacity_each) noexcept {
    if(capacity_each >= new_capacity_each) return;
    if (slices.buffer_begin_slice()) // shouldn't matter which slice we check
      expand_to(new_capacity_each);
    else [[unlikely]]
      allocate_from_empty(new_capacity_each);     
  }

  template <class... ArgTuples> // should be tuples of arguments, one tuple per member
  requires (sizeof...(ArgTuples) == NumTs)
  void emplace_back(ArgTuples&&... args_for_each_element) noexcept {
    if(size_each != capacity_each) {
      slices.add_to_end_unchecked(size_each, std::forward<ArgTuples>(args_for_each_element)...);
      ++size_each;
      return;
    }
    
    grow_and_emplace(std::forward<ArgTuples>(args_for_each_element)...);
  }

  template <class... ArgTuples> // should be tuples of arguments, one tuple per member
  requires (sizeof...(ArgTuples) == NumTs)
  void emplace_back_unchecked(UnsafeTag, ArgTuples&&... args_for_each_element) noexcept {
    assert(size_each != capacity_each);
    slices.add_to_end_unchecked(size_each, std::forward<ArgTuples>(args_for_each_element)...);
    ++size_each;
  }

  template <class... Args>
  edenAlwaysInline void 
  push_back(Args&&... new_elements) noexcept {
    return emplace_back(
      std::forward_as_tuple(std::forward<Args>(new_elements))...
    );
  }

  template <class... Args>
  edenAlwaysInline void 
  push_back_unchecked(UnsafeTag, Args&&... new_elements) noexcept { 
    return emplace_back_unchecked(
      unsafe,
      std::forward_as_tuple(std::forward<Args>(new_elements))...
    ); 
  }

  edenAlwaysInline void 
  pop_back() noexcept {
    assert(not empty());
    slices.pop_back(size_each);
    --size_each;
  }

  template<sz_t IDX> 
  edenInlineCXPR auto&&
  get(this auto&& self, count_t idx) noexcept {
    assert(idx < self.size_each);
    return std::forward_like<decltype(self)>( self.template data<IDX>()[idx] );
  }

  // growth_handler recieves the current data slices as the first parameter with the new bigger uninitialized slices as the second
  // growth_handler is responsible for ensuring new slices end up properly filled, such that elements 0 to size() are all valid living elements
  // growth_handler is also responsible for the destruction of the old elements
  // growth_handler is NOT responsible for allocation
  edenInlineCXPR void
  perform_manual_growth(UnsafeTag, std::invocable< detail::Slices<Ts...>&, detail::Slices<Ts...>& > auto&& growth_handler) noexcept {
    if (slices.buffer_begin_slice() == nullptr) {
      allocate_from_empty();
      return;
    }

    auto const new_capacity_each = grow(capacity_each); assert(new_capacity_each >= capacity_each);
    auto const old_buffer_size_bytes = buffer_size_bytes();
    capacity_each = new_capacity_each;
    auto const new_alloc = allocate(buffer_size_bytes());
    auto const old_alloc = slices.buffer_begin_slice();
    
    auto old_slices = slices;
    slices.reslice_with(new_alloc, new_capacity_each);
    growth_handler(old_slices, slices);
    deallocate_at(old_alloc, old_buffer_size_bytes);
    ++size_each;
  }
  
  // will emplace args into one slice but will not update the internal 'size' counter
  // you cannot call any normal methods after this besides to_span, data, front, empty, or the size/capacity methods until you either:
  // - call pop_back_one on this same index before destruction
  // - call have_emplaced_all after calling this function or manually constructing the other types at their ends
  // args may not alias this vector
  template <sz_t IDX>
  void emplace_back_one(UnsafeTag, auto&&... args) noexcept {
    if (size_each == capacity_each) {
      if (slices.buffer_begin_slice() == nullptr) allocate_from_empty();
      else expand_to( grow(capacity_each) );
    }
    
    auto const slice = slices.template get<IDX>();
    std::construct_at(slice + size_each, std::forward<decltype(args)>(args)...);
  }

  // see above, args may alias this vector
  template <sz_t IDX>
  void emplace_back_one_unchecked(UnsafeTag, auto&&... args) noexcept {
    auto const slice = slices.template get<IDX>();
    std::construct_at(slice + size_each, std::forward<decltype(args)>(args)...);
  }

  // read above
  template <sz_t IDX>
  void pop_back_one(UnsafeTag) noexcept { 
    assert(not empty());
    auto slice = slices.template get<IDX>();
    std::destroy_at(slice + size_each - 1);
  }

  // read above
  void have_emplaced_all(UnsafeTag) noexcept { ++size_each; }

};

template <class... Ts>
using contiguous_soa = contiguous_soa_t<false, Ts...>; 

template <class... Ts>
using small_contiguous_soa = contiguous_soa_t<true, Ts...>; 

}
