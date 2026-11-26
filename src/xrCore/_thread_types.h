#pragma once

#include <ppl.h>
#include <concurrent_unordered_map.h>
#include <concurrent_vector.h>
#include <atomic>
#include <functional>
#include <algorithm>
#include <iterator>
#include <type_traits>

// Atomic types
using xr_atomic_u32 = std::atomic_uint32_t;
using xr_atomic_s32 = std::atomic_int;
using xr_atomic_bool = std::atomic_bool;

// Tasks Redefinition
using xr_task_group = concurrency::task_group;

template <typename T, typename U>
using xr_concurrent_unordered_map = concurrency::concurrent_unordered_map<T, U>;

template <typename T, typename allocator = xalloc<T>>
using xr_concurrent_vector = concurrency::concurrent_vector<T, allocator>;

template<typename BlockRangeType, typename Body>
inline void xr_parallel_for(BlockRangeType Begin, BlockRangeType End, Body Functor)
{
	concurrency::parallel_for(Begin, End, Functor);
}

template<typename Index, typename Body>
inline void xr_parallel_foreach(Index Begin, Index End, Body Functor)
{
	concurrency::parallel_for_each(Begin, End, Functor);
}
// Helper to deduce the value type for the default predicate
template <typename RandomIt>
using IterValueT = typename std::iterator_traits<RandomIt>::value_type;

// Helper to check if an iterator is Random Access
template <typename It>
using IsRandomAccess = std::is_base_of<
    std::random_access_iterator_tag,
    typename std::iterator_traits<It>::iterator_category
>;

// PPL behaviour - fallback to std::sort if chunk size < 2048 and cores < 2
template<typename RandomIt, typename P = std::less<IterValueT<RandomIt>>>
IC void xr_parallel_sort(RandomIt first, RandomIt last, P pred = {})
{
    concurrency::parallel_sort(first, last, pred);
}
