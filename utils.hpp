#pragma once
#include "typedefs.hpp"
#include "macros.hpp"
#include "metaprogramming/concepts.hpp"
#include "type_flags.hpp"

#include <utility>
#include <concepts>
#include <cmath>
#include <array>
#include <algorithm>
#include <charconv>

namespace eden {

template <class T>
edenConst edenNodiscardCXPR
bool areBitwiseEqual(T const* first, T const* second) {
  auto const p1 =     (byte_t const*)  first;
  auto const p1_end = (byte_t const*) (first + 1);
  auto const p2 =     (byte_t const*)  second;
  auto const p2_end = (byte_t const*) (second + 1);
  return std::equal(p1, p1_end, p2, p2_end);
}

template <std::floating_point T>
requires (sizeof(T) > sizeof(float))
edenConst edenNodiscardCXPR bool
canBeRepresentedByFloat(T val) {
  auto const as_float = (float) val;
  return std::isfinite( as_float ) and (val == (T) as_float);
}

template <std::integral T>
edenConst edenInlineNodiscardCXPR
T narrow(std::integral auto val) {
  edenAssume( sz_t(val) == sz_t((T)val) );
  return (T) val;
}

}

// piping utils
namespace eden {
  
static constexpr struct{
  edenConst edenInlineNodiscardCXPR static auto operator()(auto& a) noexcept { return std::addressof(a); } 
}addrof;
edenConst edenInlineNodiscardCXPR auto operator|(auto& a, decltype(addrof)) noexcept { return addrof(a); }

static constexpr struct{
  edenConst edenInlineNodiscardCXPR static auto operator()(pointer_c auto a) noexcept -> decltype(a) { return nullptr; }
}nullptrof;

edenConst edenInlineNodiscardCXPR
auto operator|(pointer_c auto a, decltype(nullptrof)) { return nullptrof(a); } 

}

// enum utils
namespace eden {
template <ExclusivityFlag lower = Inclusive, ExclusivityFlag higher = Inclusive, enum_c T>
edenConst edenInlineNodiscardCXPR bool
enumBetween(T value, T lower_bound, T higher_bound) noexcept {
  static constexpr bool lower_exclusive = std::is_same_v<lower, Exclusive>;
  static constexpr bool higher_exclusive = std::is_same_v<higher, Exclusive>;
  auto const value_u = std::to_underlying(value);
  auto const lower_u = std::to_underlying(lower_bound);
  auto const higher_u = std::to_underlying(higher_bound);

  bool res;
  if constexpr(lower_exclusive)
    res = value_u > lower_u;
  else
    res = value_u >= lower_u;

  if constexpr (higher_exclusive)
    res &= value_u < higher_u;
  else
    res &= value_u <= higher_u;

  return res;
}

template <enum_c T>
edenConst edenInlineNodiscardCXPR bool
enumLessThan(T lower, T higher) {
  return std::to_underlying(lower) < std::to_underlying(higher);
}
}

// string utils
namespace eden {

template<sz_t N>
struct TemplateString {
  std::array<char, N> data;
  consteval TemplateString(const char (&str)[N]) {
    std::copy_n(str, N, data.begin());
  }

  template<sz_t OtherN>
  consteval bool
  operator==(const TemplateString<OtherN>& other) const noexcept {
    if constexpr (N not_eq OtherN)
      return false;
    else
      return data == other.data;
  }

  static consteval sz_t size() noexcept {return N - 1;}
};

template<>
struct TemplateString<0> {
  std::array<char, 1> data{'\0'};
  static consteval sz_t size() noexcept {return 1;}
};

template <i64_t N, TemplateString str, sz_t str_size = str.data.size()>
[[nodiscard]] consteval auto append_number_to_literal_helper_func() noexcept
-> const char (&)[str_size+22] {
  struct scuffed {
    char arr[str_size+22]{};
    consteval scuffed() {
      std::copy_n(str.data.data(), str_size, arr);
      arr[str_size-1] = '<';
      auto end = std::to_chars(&arr[str_size], &arr[str_size+20], N).ptr;
      *end = '>';
      *(++end) = '\0';
    }
  };
  static constexpr scuffed x;
  return x.arr;
}

template <i64_t N, TemplateString str, sz_t str_size = str.data.size()>
static constexpr decltype(auto) append_number_to_literal = append_number_to_literal_helper_func<N, str>();

// an implementation of POSIX's stpcpy
// returns a pointer to the end of the destination string
// assumes src and dest are pointing to different buffers
// assumes neither ptr is null
edenNodiscardCXPR char*
stpcpy_restrict(char* edenRestrict dest, const char* edenRestrict src) noexcept {
  assert(dest); assert(src);
  while (true) {
    char const c = *src;
    *dest = c; ++src;
    if (c == '\0') break;
    ++dest;
  }

  return dest;
}

// an implementation of POSIX's stpcpy
// returns a pointer to the end of the destination string
// assumes neither ptr is null
edenNodiscardCXPR char*
stpcpy(char* dest, char const* src) noexcept {
  assert(dest); assert(src);
  while (true) {
    char const c = *src;
    *dest = c; ++src;
    if (c == '\0') break;
    ++dest;
  }
  return dest;
}

edenNodiscardCXPR bool
streq_restrict(const char* edenRestrict first, const char* edenRestrict second, sz_t len) noexcept {
  assert(first); assert(second);
  auto i{0uz};
  while (i not_eq len) {
    if (first[i] not_eq second[i])
      return false;
    ++i;
  }
  return true;
}

edenNodiscardCXPR bool
streq_restrict(const char* edenRestrict first, const char* edenRestrict second) noexcept {
  assert(first); assert(second);

  auto i{0uz};
  while (true) {
    if (first[i] not_eq second[i])
      return false;
    if (first[i] == '\0')
      return true;
    ++i;
  }
}

edenNodiscardCXPR bool
streq(const char* first, const char* second, sz_t len) noexcept {
  auto i{0uz};
  while (i not_eq len) {
    if (first[i] not_eq second[i])
      return false;
    ++i;
  }
  return true;
}

edenNodiscardCXPR bool
streq(const char* first, const char* second) noexcept {
  auto i{0uz};
  while (true) {
    if (first[i] not_eq second[i])
      return false;
    if (first[i] == '\0')
      return true;
    ++i;
  }
}

}