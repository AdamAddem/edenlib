#pragma once
#include "type_aliases.hpp"
#include <concepts>

namespace eden {

namespace detail { struct FlagBase { protected: consteval FlagBase() noexcept = default; }; }
template <class T> concept EdenFlag = std::derived_from<T, detail::FlagBase>;

namespace detail { struct ExclusivityFlagBase : FlagBase {}; }
struct Exclusive : detail::ExclusivityFlagBase {};
struct Inclusive : detail::ExclusivityFlagBase {};
template <class T> concept ExclusivityFlag = std::derived_from<T, detail::ExclusivityFlagBase>;

namespace detail { struct OrderingFlagBase : FlagBase {}; }
template <class T> concept OrderingFlag = std::derived_from<T, detail::OrderingFlagBase>;
struct Less : detail::OrderingFlagBase {};
struct LessEQ : detail::OrderingFlagBase {};
struct Greater : detail::OrderingFlagBase {};
struct GreaterEQ : detail::OrderingFlagBase {};

struct DoNotInitialize : detail::FlagBase {}; static constexpr DoNotInitialize do_not_initialize;
struct UnsafeTag : detail::FlagBase {}; static constexpr UnsafeTag unsafe;

template <sz_t N> struct ReserveInitial : detail::FlagBase {};

static constexpr sz_t DynamicReserveInitial = 0;
template <sz_t N = DynamicReserveInitial> static constexpr ReserveInitial<N> reserve_initial;

}