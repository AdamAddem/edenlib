#pragma once
#include "../type_aliases.hpp"
#include "../type_flags.hpp"
#include "../macros.hpp"
#include "../metaprogramming/packs.hpp"

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
  byte_t* alloc{};
  sz_t capacity;
  
  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void
  destroy_all_impl(sz_t size) noexcept {
    if(size == 0) return;
    if constexpr(sizeof...(Rest)) destroy_all_impl<idx + 1, Rest...>(size);
  
    if constexpr(not std::is_trivially_destructible_v<First>) {
      auto const begin = get<idx>();
      auto end = begin + size - 1;
      while(true) {
        std::destroy_at(end);
        if(end == begin) break;
        end -= 1;
      }
    }
  }

  template<sz_t idx, class First, class... Rest>
  edenInlineCXPR void 
  start_slice_lifetimes_impl() noexcept {
    auto const slice = alloc + pack_data.typeIDXToOffset(idx) * capacity;
    (void)std::start_lifetime_as_array<First>(slice, capacity);
    if constexpr(sizeof...(Rest))
      return start_slice_lifetimes_impl<idx + 1, Rest...>();
  }
  
  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void
  relocate_impl(Slices new_slices, sz_t size) noexcept {
    auto old_ptr = get<idx>();
    auto construct_location = new_slices.get<idx>();
  
    if constexpr(edenTriviallyRelocatable(First))
      std::memcpy(construct_location, old_ptr, size * sizeof(First));
    else
      for(sz_t i{}; i<size; ++i)
        std::construct_at(construct_location + i, std::move(old_ptr[i])), 
        std::destroy_at(old_ptr + i); // technically not reverse order but who really cares
  
    if constexpr(sizeof...(Rest))
      relocate_impl<idx + 1, Rest...>(new_slices, size);
  }
  
  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void
  add_to_end_unchecked_impl(sz_t size, auto&& first_args, auto&&... rest_args) noexcept {
    auto end = get<idx>() + size;
  
    std::apply(
      [end]<class... Args>(Args&&... args) {
        std::construct_at(end, std::forward<Args>(args)...);
      },
      std::forward<decltype(first_args)>(first_args)
    );
  
    if constexpr(sizeof...(Rest))
      add_to_end_unchecked_impl<idx + 1, Rest...>(size, std::forward<decltype(rest_args)>(rest_args)...);
  }

  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void 
  value_initialize_elements_impl(sz_t count) noexcept {
    auto const begin = get<idx>();
    for(sz_t i{}; i<count; ++i) std::construct_at(begin + i);

    if constexpr(sizeof...(Rest))
      return value_initialize_elements_impl<idx + 1, Rest...>(count);
  }

  template <sz_t idx, class First, class... Rest>
  edenInlineCXPR void
  pop_back_impl(sz_t size) noexcept {
    auto const begin = get<idx>();
    std::destroy_at(begin + size - 1);

    if constexpr(sizeof...(Rest))
      return pop_back_impl<idx + 1, Rest...>(size);
  }
  
public:
  
  constexpr Slices() = default;
  constexpr Slices(byte_t* alloc, sz_t capacity) noexcept : alloc(alloc), capacity(capacity) {}

  template<sz_t idx> 
  edenInlineCXPR auto get() const noexcept {
    using T = packs::type_at_idx<idx, Ts...>;
    auto const offset = pack_data.typeIDXToOffset(idx) * capacity;
    return (T*) (alloc + offset);
  }
  
  edenInlineNodiscardCXPR byte_t* buffer_begin() noexcept { return alloc; }
  
  edenInlineCXPR void add_to_end_unchecked(sz_t size, auto&&... args) noexcept { return add_to_end_unchecked_impl<0, Ts...>(size, std::forward<decltype(args)>(args)...); }
  edenInlineCXPR void destroy_all(sz_t size) noexcept { return destroy_all_impl<0, Ts...>(size); }

  edenInlineCXPR void start_slice_lifetimes() noexcept { start_slice_lifetimes_impl<0, Ts...>(); }

  // relocates everything to new_slices
  edenInlineCXPR void relocate_to(Slices new_slices, sz_t size) noexcept { return relocate_impl<0, Ts...>(new_slices, size); }
  
  edenInlineCXPR void value_initialize_elements(sz_t count) noexcept { return value_initialize_elements_impl<0, Ts...>(count); }

  edenInlineCXPR void pop_back(sz_t size) noexcept { return pop_back_impl<0, Ts...>(size); }
};

}

// TODO:
// - Add custom allocator support
template <bool Small = false, class... Ts>
requires (sizeof...(Ts) > 1)
class contiguous_soa_t {
  static constexpr auto NumTs = sizeof...(Ts);
  static constexpr packs::PackData<Ts...> pack_data{};
  static constexpr auto FirstAllocCapacity = 2;
  using slices_t = detail::Slices<Ts...>;

public: using count_t = std::conditional_t<Small, u32_t, sz_t>; private:
  byte_t* allocation{};
  count_t size_each{};
  count_t capacity_each{};

  static constexpr bool doAlignedAllocation = pack_data.biggestAlignment() > __STDCPP_DEFAULT_NEW_ALIGNMENT__;

  edenInlineNodiscardCXPR static count_t capacityIfGrown(count_t n) noexcept { assert(n * 2 < n); return n * 2; }
  
  edenInlineNodiscardCXPR static sz_t 
  bufferSizeForCapacity(count_t cap) noexcept {
    auto const byte_count = cap * pack_data.totalSize(); 
    assert(byte_count >= cap);
    return byte_count;
  }
  
  edenInlineNodiscard static byte_t* 
  allocate(count_t capacity) noexcept {
    auto const byte_count = bufferSizeForCapacity(capacity);
    if constexpr(doAlignedAllocation)
      return (byte_t*) ::operator new(byte_count, (align_t) pack_data.biggestAlignment()); 
    else 
      return (byte_t*) ::operator new(byte_count); 
  }
  
  edenInlineCXPR void deallocate() noexcept { 
    if constexpr(doAlignedAllocation)
      ::operator delete(allocation, bufferSizeForCapacity(capacity_each), (align_t) pack_data.biggestAlignment());
    else 
      ::operator delete(allocation, bufferSizeForCapacity(capacity_each));
  }

  edenInlineNodiscardCXPR slices_t slices() const noexcept { return slices_t{allocation, capacity_each}; }
  
  edenInlineCXPR void 
  destroy_all() noexcept {
    slices().destroy_all(size_each);
    size_each = 0; 
  }

  void expand_to(count_t new_capacity_each) noexcept {
    assert(new_capacity_each >= capacity_each);
    auto const new_alloc = allocate(new_capacity_each);

    slices_t new_slices{new_alloc, new_capacity_each};
    new_slices.start_slice_lifetimes();

    // this branch isn't strictly necessary
    // TODO: benchmark with and without
    if(size_each != 0) slices().relocate_to(new_slices, size_each);
    deallocate();
    allocation = new_slices.buffer_begin();
    capacity_each = new_capacity_each;
  }

  void allocate_from_empty(count_t new_capacity_each = FirstAllocCapacity) noexcept {
    assert(allocation == nullptr); assert(size_each == 0); assert(capacity_each == 0);
    auto const new_allocation = allocate(new_capacity_each);
    slices_t new_slices{new_allocation, new_capacity_each}; new_slices.start_slice_lifetimes();

    allocation = new_slices.buffer_begin();
    capacity_each = new_capacity_each;
  }

  template <class ...ArgTuples>
  void grow_and_emplace(ArgTuples&&... args_for_each_element) noexcept {
    if (allocation == nullptr) {
      allocate_from_empty();
      slices().add_to_end_unchecked(size_each, std::forward<ArgTuples>(args_for_each_element)...);
      ++size_each;
      return;
    }
    assert(size_each != 0);
    
    auto const new_capacity_each = capacityIfGrown(capacity_each); assert(new_capacity_each >= capacity_each);
    auto const new_alloc = allocate(new_capacity_each);

    slices_t new_slices{new_alloc, new_capacity_each}; 
    new_slices.start_slice_lifetimes();
    new_slices.add_to_end_unchecked(size_each, std::forward<ArgTuples>(args_for_each_element)...);
    slices().relocate_to(new_slices, size_each);

    deallocate();
    
    ++size_each;
    allocation = new_slices.buffer_begin();
    capacity_each = new_capacity_each;
  }

public:

  constexpr contiguous_soa_t() = default;

  explicit contiguous_soa_t(count_t count) noexcept {
    assert(count != 0);
    allocate_from_empty(count);
    slices().value_initialize_elements(count);
    size_each = count;
  }

  template <sz_t N>
  requires (Small ? N <= u32_max : true)
  explicit contiguous_soa_t(ReserveInitial<N>) noexcept
  { allocate_from_empty(N); }

  constexpr contiguous_soa_t(contiguous_soa_t&& other) noexcept
  : allocation(other.allocation), size_each(other.size_each), capacity_each(other.capacity_each) {
    other.allocation = nullptr; other.size_each = 0; other.capacity_each = 0;
  }

  contiguous_soa_t& operator=(contiguous_soa_t&& other) noexcept {
    destroy_all(); deallocate();
    allocation = other.allocation; other.allocation = nullptr;
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

  template <sz_t IDX> edenInlineNodiscard auto&& front(this auto&& self) noexcept { assert(self.size_each not_eq 0); return std::forward_like<decltype(self)>(self.slices().template get<IDX>()[0]); }
  template <sz_t IDX> edenInlineNodiscard auto&& back(this auto&& self) noexcept { assert(self.size_each not_eq 0); return std::forward_like<decltype(self)>(self.slices().template get<IDX>()[self.size_each - 1]); }
  template <sz_t IDX> edenInlineNodiscardCXPR auto data(this auto&& self) noexcept { return self.slices().template get<IDX>(); }

  template <sz_t IDX> edenInlineNodiscardCXPR auto to_span()       noexcept { return std::span( data<IDX>(), size_each ); }
  template <sz_t IDX> edenInlineNodiscardCXPR auto to_span() const noexcept { return std::span( data<IDX>(), size_each ); }

  void reserve(count_t new_capacity_each) noexcept {
    if(capacity_each >= new_capacity_each) return;
    if (allocation != nullptr)
      expand_to(new_capacity_each);
    else [[unlikely]]
      allocate_from_empty(new_capacity_each);     
  }

  template <class... ArgTuples> // should be tuples of arguments, one tuple per member
  requires (sizeof...(ArgTuples) == NumTs)
  void emplace_back(ArgTuples&&... args_for_each_element) noexcept {
    if(size_each != capacity_each) {
      slices().add_to_end_unchecked(size_each, std::forward<ArgTuples>(args_for_each_element)...);
      ++size_each;
      return;
    }
    
    grow_and_emplace(std::forward<ArgTuples>(args_for_each_element)...);
  }

  template <class... ArgTuples> // should be tuples of arguments, one tuple per member
  requires (sizeof...(ArgTuples) == NumTs)
  void emplace_back_unchecked(UnsafeTag, ArgTuples&&... args_for_each_element) noexcept {
    assert(size_each != capacity_each);
    slices().add_to_end_unchecked(size_each, std::forward<ArgTuples>(args_for_each_element)...);
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
    slices().pop_back(size_each);
    --size_each;
  }

  template<sz_t IDX> 
  edenInlineCXPR auto&&
  get(this auto&& self, count_t idx) noexcept {
    assert(idx < self.size_each);
    return std::forward_like<decltype(self)>(self.template data<IDX>()[idx]);
  }

  // growth_handler recieves the current data slices as the first parameter with the new bigger uninitialized slices as the second
  // growth_handler is responsible for ensuring new slices end up properly filled, such that elements 0 to size() are all valid living elements
  // growth_handler is also responsible for the destruction of the old elements
  // growth_handler is NOT responsible for allocation
  edenInlineCXPR void
  perform_manual_growth(UnsafeTag, std::invocable<slices_t const&, slices_t&> auto&& growth_handler) noexcept {
    if (allocation == nullptr) return allocate_from_empty();

    auto const new_capacity_each = capacityIfGrown(capacity_each); assert(new_capacity_each >= capacity_each);
    auto const new_alloc = allocate(new_capacity_each);
    
    slices_t new_slices{new_alloc, new_capacity_each}; new_slices.start_slice_lifetimes();
    growth_handler(slices(), new_slices);
    deallocate();
    
    ++size_each;
    allocation = new_slices.buffer_begin();
    capacity_each = new_capacity_each;
  }
  
  // will emplace args into one slice but will not update the internal 'size' counter
  // you cannot call any normal methods after this besides to_span, data, front, empty, or the size/capacity methods until you either:
  // - call pop_back_one on this same IDX before destruction
  // - call have_emplaced_all after calling this function or manually constructing the other types at their ends
  // args may not alias this vector
  template <sz_t IDX>
  void emplace_back_one(UnsafeTag, auto&&... args) noexcept {
    if (size_each == capacity_each) {
      if (allocation == nullptr) allocate_from_empty();
      else expand_to( capacityIfGrown(capacity_each) );
    }
    
    auto const slice = slices().template get<IDX>();
    std::construct_at(slice + size_each, std::forward<decltype(args)>(args)...);
  }

  // see above, args may alias this vector
  template <sz_t IDX>
  void emplace_back_one_unchecked(UnsafeTag, auto&&... args) noexcept {
    auto const slice = slices().template get<IDX>();
    std::construct_at(slice + size_each, std::forward<decltype(args)>(args)...);
  }

  // see above
  // does not update size counter, so accessing through span, data, or back is dangerous
  template <sz_t IDX>
  void pop_back_one(UnsafeTag) noexcept { 
    auto slice = slices().template get<IDX>();
    std::destroy_at(slice + size_each);
  }

  // see above
  void have_emplaced_all(UnsafeTag) noexcept { ++size_each; }

};

template <class... Ts>
using contiguous_soa = contiguous_soa_t<false, Ts...>; 

template <class... Ts>
using small_contiguous_soa = contiguous_soa_t<true, Ts...>; 

}
