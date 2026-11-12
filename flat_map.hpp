#pragma once
#include "macros.hpp"
#include "typedefs.hpp"
#include "vectors/contiguous_soa.hpp"
#include "utils.hpp"

#include <ranges>
#include <algorithm>

namespace eden {

  template <class Key, class Value>
  requires requires (Key a, Key b) { a < b; a > b; a == b; a != b; }
  class flat_map_a {
    struct KeyAndIndex {
      friend class flat_map_a<Key, Value>;
      using index_t = std::conditional_t<(sizeof(Key) > sizeof(u32_t)), sz_t, u32_t>; // use u32_t to make this struct smaller if it wouldn't hurt
      Key key;
      index_t idx;
    };

    static constexpr bool KITriviallyRelocatable = edenTriviallyRelocatable(KeyAndIndex);
    static constexpr bool ValueTriviallyRelocatable = edenTriviallyRelocatable(Value);

    static constexpr auto KITypeIndex = 0;
    static constexpr auto ValueTypeIndex = 1;
    contiguous_soa<KeyAndIndex, Value> arr;

    KeyAndIndex* upper_bound(Key const& key) noexcept {
      return std::to_address( std::ranges::upper_bound(keys(), key, std::less{}, &KeyAndIndex::key) );
    }
    KeyAndIndex const* upper_bound(Key const& key) const noexcept {
      return std::to_address( std::ranges::upper_bound(keys(), key, std::less{}, &KeyAndIndex::key) );
    }

    KeyAndIndex* lower_bound(Key const& key) noexcept {
      return std::to_address( std::ranges::lower_bound(keys(), key, std::less{}, &KeyAndIndex::key) );
    }
    KeyAndIndex const* lower_bound(Key const& key) const noexcept {
      return std::to_address( std::ranges::lower_bound(keys(), key, std::less{}, &KeyAndIndex::key) );
    }
    
    edenInlineNodiscardCXPR Value& getValue(sz_t idx) noexcept { return arr.template get<ValueTypeIndex>(idx); }
    edenInlineNodiscardCXPR Value const& getValue(sz_t idx) const noexcept { return arr.template get<ValueTypeIndex>(idx); }
    
    edenInlineNodiscardCXPR std::span<KeyAndIndex> keys() noexcept { return arr.template to_span<KITypeIndex>(); }
  public:

    edenInlineNodiscardCXPR std::span<KeyAndIndex const> keys() const noexcept { return arr.template to_span<KITypeIndex>(); }
    edenInlineNodiscardCXPR std::span<Value> values() noexcept { return arr.template to_span<ValueTypeIndex>(); }
    edenInlineNodiscardCXPR std::span<Value const> values() const noexcept { return arr.template to_span<ValueTypeIndex>(); }

    edenInlineCXPR flat_map_a() noexcept = default;
    edenInlineNodiscardCXPR bool empty() const noexcept { return arr.empty(); }
    edenInlineNodiscardCXPR auto size() const noexcept { return arr.size(); }
    edenInlineNodiscardCXPR auto capacity() const noexcept { return arr.capacity(); }
    
    // key must be contained within
    Value& get(Key const& key) noexcept {
      auto const ptr = lower_bound(key);
      assert(ptr != keys().end().base()); assert(ptr->key == key);
      return getValue(ptr->idx);
    }

    // key must be contained within
    Value const& get(Key const& key) const noexcept {
      auto const ptr = lower_bound(key);
      assert(ptr != keys().end().base()); assert(ptr->key == key);
      return getValue(ptr->idx);
    }

    // nullptr on search failure
    Value* find(Key const& key) noexcept {
      auto const ptr = lower_bound(key);
      if(ptr == keys().end().base()) return nullptr;
      if(ptr->key != key) return nullptr;
      return getValue(ptr->idx) | addrof;
    }

    // nullptr on search failure
    Value const* find(Key const& key) const noexcept {
      auto const ptr = lower_bound(key);
      if(ptr == keys().end().base()) return nullptr;
      if(ptr->key != key) return nullptr;
      return getValue(ptr->idx) | addrof;
    }

    // returns a 
    // - reference to the element at key
    // - a bool with value false if the key is not unique and the element was not added
    // key may not be one of the values, and value may not be one of the keys
    // key and value may not reference the same thing
    std::pair<Value&, bool> 
    emplace(Key&& key, Value&& value) noexcept {
      static constexpr bool DUPLICATE_KEY = false;
      static constexpr bool UNIQUE_KEY = true;

      if constexpr(std::same_as<Key, Value>)
        assert((key | addrof) != (value | addrof));
      
      auto candidate = lower_bound(key);
      if(candidate == keys().end().base()) {
        auto const sz = arr.size();
        arr.push_back(KeyAndIndex{std::move(key), eden::narrow<typename KeyAndIndex::index_t>(sz)}, std::move(value));
        return {getValue(sz), UNIQUE_KEY};
      }
      
      if(candidate->key == key) return { getValue(candidate->idx), DUPLICATE_KEY };

      auto const sz = arr.size();
      auto const candidate_idx = eden::narrow<sz_t>(candidate - arr.template data<KITypeIndex>()); // need index bcuz candidate ptr may be invalidated
      
      if(arr.size() == arr.capacity()) {
        arr.unsafe_perform_manual_growth(
          [&] (detail::Slices<KeyAndIndex, Value>& old_slices, detail::Slices<KeyAndIndex, Value>& new_slices) {
            auto const old_kis = old_slices.template get<KITypeIndex>();
            auto const new_kis = new_slices.template get<KITypeIndex>();
            if constexpr(KITriviallyRelocatable) {
              std::memcpy(new_kis, old_kis, sizeof(KeyAndIndex) * candidate_idx); // copy everything up to but not including candidate
              std::construct_at(new_kis + candidate_idx, std::move(key), sz); // copy KI into candidate slot
              std::memcpy(new_kis + candidate_idx + 1, old_kis + candidate_idx, sizeof(KeyAndIndex) * (sz - candidate_idx)); // copy everything after and including original candidate
            }
            else {
              for(sz_t i{}; i<candidate_idx; ++i)
                std::construct_at(new_kis + i, std::move(old_kis[i]));
              std::construct_at(new_kis + candidate_idx, std::move(key), sz);
              for(sz_t i{candidate_idx}; i<sz; ++i)
                std::construct_at(new_kis + i + 1, std::move(old_kis[i]));
              for(sz_t i{}; i<sz; ++i)
                std::destroy_at(old_kis + i);
            }

            auto const old_values = old_slices.template get<ValueTypeIndex>();
            auto const new_values = new_slices.template get<ValueTypeIndex>();
            if constexpr(ValueTriviallyRelocatable) {
              std::construct_at(new_values + sz, std::move(value));
              std::memcpy(new_values, old_values, sizeof(Value) * sz);
            }
            else {
              std::construct_at(new_values + sz, std::move(value));
              for(sz_t i{}; i<sz; ++i)
                std::construct_at(new_values + i, std::move(old_values[i])),
                std::destroy_at(old_values + i);
            }
          }
        );
        return {getValue(sz), UNIQUE_KEY};
      }
      
      arr.template unsafe_emplace_back_one_unchecked<ValueTypeIndex>(std::move(value));

      auto const key_slice = arr.template data<KITypeIndex>();
      auto const key_slice_end = key_slice + sz; // is a valid slot due to above
      
      if constexpr(KITriviallyRelocatable) {
        auto const num_elements_to_shift = key_slice_end - candidate;
        std::memmove(candidate + 1, candidate, sizeof(KeyAndIndex) * num_elements_to_shift);
      }
      else {
        auto p{key_slice_end};
        while(p != candidate) {
          auto const prev = p--;
          std::construct_at(prev, std::move(*p));
          std::destroy_at(p);
        }
      }

      auto const ki = std::construct_at(candidate, std::move(key), sz);
      arr.unsafe_have_emplaced_all();
      auto& x = getValue(ki->idx);
      return {x, UNIQUE_KEY};
    }
  };

  template <class Key, class Value, bool Small = false>
  requires requires (Key a, Key b) { a < b; a > b; a == b; a != b; }
  class flat_map_b {
    static constexpr bool DoSmallOptimization = Small or sizeof(Key) <= sizeof(u32_t); // use u32_t to make indexes smaller if it wouldn't hurt
    using count_t = std::conditional_t<DoSmallOptimization, u32_t, sz_t>;

    static constexpr bool KeyTriviallyRelocatable = edenTriviallyRelocatable(Key);
    static constexpr bool ValueTriviallyRelocatable = edenTriviallyRelocatable(Value);
    
    static constexpr auto KeyTypeIndex = 0;
    static constexpr auto IndexTypeIndex = 1;
    static constexpr auto ValueTypeIndex = 2;
    contiguous_soa_t<DoSmallOptimization, Key, count_t, Value> arr;

    edenInlineNodiscard auto* upper_bound(this auto&& self, Key const& key) noexcept { return std::to_address( std::ranges::upper_bound(self.keys(), key) ); }
    edenInlineNodiscard auto* lower_bound(this auto&& self, Key const& key) noexcept { return std::to_address( std::ranges::lower_bound(self.keys(), key) ); }
    edenInlineNodiscard count_t getIndex(Key const* key) const noexcept { return arr.template get<IndexTypeIndex>(key - arr.template data<KeyTypeIndex>()); }
    
    edenInlineNodiscardCXPR std::span<Key> keys() noexcept { return arr.template to_span<KeyTypeIndex>(); }
    edenInlineNodiscardCXPR std::span<count_t> indices() noexcept { return arr.template to_span<IndexTypeIndex>(); }
    
    edenInlineNodiscardCXPR auto* key_data(this auto&& self) noexcept { return self.arr.template data<KeyTypeIndex>(); }
    edenInlineNodiscardCXPR auto* index_data(this auto&& self) noexcept { return self.arr.template data<IndexTypeIndex>(); }
    edenInlineNodiscardCXPR auto* value_data(this auto&& self) noexcept { return self.arr.template data<ValueTypeIndex>(); }
  public:

    edenInlineNodiscardCXPR std::span<Key const> keys() const noexcept { return arr.template to_span<KeyTypeIndex>(); }
    edenInlineNodiscardCXPR std::span<Value> values() noexcept { return arr.template to_span<ValueTypeIndex>(); }
    edenInlineNodiscardCXPR std::span<Value const> values() const noexcept { return arr.template to_span<ValueTypeIndex>(); }
    edenInlineNodiscardCXPR auto&& getFromIndex(this auto&& self, count_t idx) noexcept { return std::forward_like<decltype(self)>(self.arr.template get<ValueTypeIndex>(idx)); }

    // performs linear search to find corresponding key for item at idx
#define pre assert(idx < self.size());
    edenInlineNodiscardCXPR auto&&
    getKeyOf(this auto&& self, count_t idx) noexcept {  pre
      auto const indices = self.index_data();
      auto const sz = self.size();
      count_t i{};
      for(; i<sz; ++i) if(indices[i] == idx) break;
      return std::forward_like<decltype(self)>(self.arr.template get<KeyTypeIndex>(i)); 
    }
#undef pre

    edenInlineCXPR flat_map_b() noexcept = default;
    edenInlineNodiscardCXPR bool empty() const noexcept { return arr.empty(); }
    edenInlineNodiscardCXPR count_t size() const noexcept { return arr.size(); }
    edenInlineNodiscardCXPR count_t capacity() const noexcept { return arr.capacity(); }

    // returns the index of a value located within the vector
    // parameter must be a valid pointer to a value located within this map, otherwise UB
    edenInlineCXPR count_t index_of(Value const* object_in_here) const noexcept { return object_in_here - value_data(); }
    
    // key must be contained within
    auto&& get(this auto&& self, Key const& key) noexcept {
      auto const ptr = self.lower_bound(key);
      assert(ptr != self.keys().end().base()); assert(*ptr == key);
      return std::forward_like<decltype(self)>(self.getFromIndex(self.getIndex(ptr)));
    }

    // nullptr on search failure
    auto* find(this auto&& self, Key const& key) noexcept {
      auto const ptr = self.lower_bound(key);
      if(ptr == self.keys().end().base()) return nullptrof(self.value_data());
      if(*ptr != key) return nullptrof(self.value_data());
      return self.getFromIndex(self.getIndex(ptr)) | addrof;
    }

    // returns index of the element at key, and bool with value false if the key is not unique and the element was not added
    // key may not be one of the values, and value may not be one of the keys
    // key and value may not reference the same thing
    [[nodiscard]] std::pair<count_t, bool>
    emplace(Key&& key, Value&& value) noexcept {
      static constexpr bool DUPLICATE_KEY = false;
      static constexpr bool UNIQUE_KEY = true;
      if constexpr(std::is_same_v<Key, Value>) assert((key | addrof) != (value | addrof));
      
      auto candidate_key = lower_bound(key);
      if(candidate_key == keys().end().base()) {
        auto const sz = arr.size();
        arr.push_back(std::move(key), sz, std::move(value));
        return {sz, UNIQUE_KEY};
      }
      
      if(*candidate_key == key) return { getIndex(candidate_key), DUPLICATE_KEY };

      auto const sz = arr.size();
      auto const candidate_idx = eden::narrow<sz_t>(candidate_key - arr.template data<KeyTypeIndex>());

      if(arr.size() == arr.capacity()) {
        arr.unsafe_perform_manual_growth(
          [&] (detail::Slices<Key, count_t, Value>& old_slices, detail::Slices<Key, count_t, Value>& new_slices) {
            auto const old_keys = old_slices.template get<KeyTypeIndex>();
            auto const new_keys = new_slices.template get<KeyTypeIndex>();
            if constexpr(KeyTriviallyRelocatable) {
              std::memcpy(new_keys, old_keys, sizeof(Key) * candidate_idx); // copy everything up to but not including candidate
              std::construct_at(new_keys + candidate_idx, std::move(key)); // copy KI into candidate slot
              std::memcpy(new_keys + candidate_idx + 1, old_keys + candidate_idx, sizeof(Key) * (sz - candidate_idx)); // copy everything after and including original candidate
            }
            else {
              for(sz_t i{}; i<candidate_idx; ++i)
                std::construct_at(new_keys + i, std::move(old_keys[i]));
              std::construct_at(new_keys + candidate_idx, std::move(key));
              for(sz_t i{candidate_idx}; i<sz; ++i)
                std::construct_at(new_keys + i + 1, std::move(old_keys[i]));
              for(sz_t i{}; i<sz; ++i)
                std::destroy_at(old_keys + i);
            }

            auto const old_indexs = old_slices.template get<IndexTypeIndex>();
            auto const new_indexs = new_slices.template get<IndexTypeIndex>();
            std::memcpy(new_indexs, old_indexs, sizeof(count_t) * candidate_idx);
            std::construct_at(new_indexs + candidate_idx, sz);
            std::memcpy(new_indexs + candidate_idx + 1, old_indexs + candidate_idx, sizeof(count_t) * (sz - candidate_idx));
            
            auto const old_values = old_slices.template get<ValueTypeIndex>();
            auto const new_values = new_slices.template get<ValueTypeIndex>();
            if constexpr(ValueTriviallyRelocatable) {
              std::construct_at(new_values + sz, std::move(value));
              std::memcpy(new_values, old_values, sizeof(Value) * sz);
            }
            else {
              std::construct_at(new_values + sz, std::move(value));
              for(sz_t i{}; i<sz; ++i)
                std::construct_at(new_values + i, std::move(old_values[i])),
                std::destroy_at(old_values + i);
            }
          }
        );
        return {sz, UNIQUE_KEY};
      }
      
      arr.template unsafe_emplace_back_one_unchecked<ValueTypeIndex>( std::move(value) );
      auto const key_slice_end = arr.template data<KeyTypeIndex>() + sz; // is a valid slot due to above
      auto const num_elements_to_shift = key_slice_end - candidate_key;
      
      if constexpr(edenTriviallyRelocatable(Key)) {
        std::memmove(candidate_key + 1, candidate_key, sizeof(Key) * num_elements_to_shift);
      }
      else {
        auto p{key_slice_end};
        while(p != candidate_key) {
          auto const prev = p--;
          std::construct_at(prev, std::move(*p));
          std::destroy_at(p);
        }
      }

      auto const corresponding_index = arr.template data<IndexTypeIndex>() + candidate_idx;
      std::memmove(corresponding_index + 1, corresponding_index, sizeof(count_t) * num_elements_to_shift);
      std::construct_at(corresponding_index, sz);

      std::construct_at(candidate_key, std::move(key));
      arr.unsafe_have_emplaced_all();
      return {sz, UNIQUE_KEY};
    }
  };

  template <class Key, class Value>
  using flat_map = flat_map_b<Key, Value>;
  
  template <class Key, class Value>
  using small_flat_map = flat_map_b<Key, Value, true>;
}