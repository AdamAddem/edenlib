#pragma once
#include "typedefs.hpp"
#include "macros.hpp"
#include "metaprogramming/concepts.hpp"
#include "type_flags.hpp"

#include <utility>
#include <concepts>
#include <cmath>
namespace eden {

// T must not contain any padding
template <class T>
edenConst edenNodiscardCXPR
bool areBitwiseEqual(T const* first, T const* second) {
  auto const p1 =     (byte_t const*)  first;
  auto const p1_end = (byte_t const*) (first + 1);
  auto const p2 =     (byte_t const*)  second;
  auto const p2_end = (byte_t const*) (second + 1);
  return std::equal(p1, p1_end, p2, p2_end);
}

// T must not contain any padding
// first and second may not overlap
template <class T>
edenConst edenNodiscardCXPR bool
areBitwiseEqualRestrict(T const* edenRestrict first, T const* edenRestrict second) {
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

template <ExclusivityFlag lower = flags::Inclusive, ExclusivityFlag higher = flags::Inclusive, enum_c T>
edenConst edenInlineNodiscardCXPR bool
enumBetween(T value, T lower_bound, T higher_bound) noexcept {
  static constexpr bool lower_exclusive = std::is_same_v<lower, flags::Exclusive>;
  static constexpr bool higher_exclusive = std::is_same_v<higher, flags::Exclusive>;

  bool res;
  if constexpr(lower_exclusive)
    res = std::to_underlying(value) > std::to_underlying(lower_bound);
  else
    res = std::to_underlying(value) >= std::to_underlying(lower_bound);

  if constexpr (higher_exclusive)
    res &= (std::to_underlying(value) < std::to_underlying(higher_bound));
  else
    res &= (std::to_underlying(value) <= std::to_underlying(higher_bound));

  return res;
}

template <enum_c T>
edenConst edenInlineNodiscardCXPR bool
enumLessThan(T lower, T higher) {
  return std::to_underlying(lower) < std::to_underlying(higher);
}

static constexpr struct{
  edenConst edenInlineNodiscardCXPR static auto operator()(auto& a) noexcept { return std::addressof(a); } 
}addrof;
edenConst edenInlineNodiscardCXPR auto* operator|(auto& a, decltype(addrof)) noexcept { return addrof(a); }

static constexpr struct{
  edenConst edenInlineNodiscardCXPR static auto operator()(pointer_c auto a) noexcept -> decltype(a) { return nullptr; }
}nullptrof;

edenConst edenInlineNodiscardCXPR
auto operator|(pointer_c auto a, decltype(nullptrof)) { return nullptrof(a); }

}