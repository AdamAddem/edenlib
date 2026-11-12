#pragma once
#include "typedefs.hpp"
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

struct DoNotInitialize : detail::FlagBase {};
static constexpr DoNotInitialize do_not_initialize;

template <sz_t N> requires (N > 0) struct ReserveInitial : detail::FlagBase {};

static constexpr sz_t DynamicReserveInitial = sz_max;
template <sz_t N = DynamicReserveInitial> static constexpr ReserveInitial<N> reserve_initial;

}