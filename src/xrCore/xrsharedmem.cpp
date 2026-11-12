#include "stdafx.h"
#pragma hdrstop

#include <malloc.h>
#include <string_view>

using namespace std;

XRCORE_API smem_container* g_pSharedMemoryContainer = NULL;

smem_value* smem_container::dock(u32 dwCRC, u32 dwLength, void* ptr)
{
	VERIFY(dwLength && ptr);

	// Asset CRCs can be zero or shared by many distinct blocks. Hash the complete
	// binary payload once, outside the lock, and retain the hash for table growth.
	const size_t content_hash = xr_hash<std::string_view>()(
		std::string_view(static_cast<const char*>(ptr), dwLength));
	xrCriticalSectionGuard guard(cs);
	smem_value* result = 0;

	cdb::iterator it = container.find(key{dwCRC, dwLength, static_cast<const u8*>(ptr), content_hash});
	if (it != container.end()) result = it->second;

	// if not found - create new entry
	if (0 == result)
	{
		result = (smem_value*)Memory.mem_alloc(sizeof(smem_value) + dwLength
#ifdef DEBUG_MEMORY_NAME
                                               , "storage: smem"
#endif // DEBUG_MEMORY_NAME
		);
		result->dwReference.store(0, std::memory_order_relaxed);
		result->dwCRC = dwCRC;
		result->dwLength = dwLength;
		CopyMemory(result->value, ptr, dwLength);
		container.emplace(key{dwCRC, dwLength, result->value, content_hash}, result);
	}

	return result;
}

void smem_container::clean()
{
	cs.Enter();
	for (cdb::iterator it = container.begin(); it != container.end();)
	{
		smem_value* value = it->second;
		if (value->dwReference.load(std::memory_order_relaxed) == 0)
		{
			it = container.erase(it);
			xr_free(value);
		}
		else
			++it;
	}
	cs.Leave();
}

void smem_container::dump()
{
	cs.Enter();
	FILE* F = fopen("x:\\$smem_dump$.txt", "w");
	for (const auto& item : container)
	{
		const smem_value* value = item.second;
		fprintf(F, "%4u : crc[%6x], %u bytes\n", value->dwReference.load(std::memory_order_relaxed), value->dwCRC, value->dwLength);
	}
	fclose(F);
	cs.Leave();
}

s64 smem_container::stat_economy(size_t* memory_usage)
{
	xrCriticalSectionGuard guard(cs);
#ifdef USE_ROBINHOOD
	// Robin Hood owns malloc allocations, including old tables reused by its node
	// pool. Query them directly instead of estimating a fixed cost per live entry.
	size_t used = sizeof(*this) + container.allocated_bytes([](void* allocation) {
		return _msize(allocation);
	});
#else
	size_t used = sizeof(*this) + index_bytes;
#endif
	s64 unshared = 0;
	for (const auto& item : container)
	{
		const smem_value* value = item.second;
		used += sizeof(smem_value) + value->dwLength;
		unshared += s64(value->dwReference.load(std::memory_order_relaxed)) * value->dwLength;
	}

#ifdef USE_ROBINHOOD
	const auto buckets = container.bucket_statistics();
	Msg("* [x-ray]: shared memory: count[%zu], load factor[%.2f], max_collisions[%zu]",
		container.size(), container.load_factor(), buckets.max_collisions);
#else
	size_t collisions = 0;
	size_t max_collisions = 0;
	for (size_t i = 0; i < container.bucket_count(); ++i)
	{
		const size_t size = container.bucket_size(i);
		if (size != 0)
			collisions += size - 1;
		max_collisions = (std::max)(max_collisions, size);
	}
	// Standard unordered containers expose bucket occupancy, but no probe distance.
	Msg("* [x-ray]: shared memory: count[%zu], load factor[%.2f], max_collisions[%zu]",
		container.size(), container.load_factor(), max_collisions);
#endif

	if (memory_usage)
		*memory_usage = used;
	return unshared - s64(used);
}

smem_container::~smem_container()
{
	clean();
}
