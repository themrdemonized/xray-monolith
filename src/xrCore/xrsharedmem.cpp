#include "stdafx.h"
#pragma hdrstop

using namespace std;

XRCORE_API smem_container* g_pSharedMemoryContainer = NULL;

smem_value* smem_container::dock(u32 dwCRC, u32 dwLength, void* ptr)
{
	VERIFY(dwCRC && dwLength && ptr);

	cs.Enter();
	smem_value* result = 0;

	cdb::iterator it = container.find(key{dwCRC, dwLength, static_cast<const u8*>(ptr)});
	if (it != container.end()) result = it->second;

	// if not found - create new entry
	if (0 == result)
	{
		result = (smem_value*)Memory.mem_alloc(4 * sizeof(u32) + dwLength
#ifdef DEBUG_MEMORY_NAME
                                               , "storage: smem"
#endif // DEBUG_MEMORY_NAME
		);
		result->dwReference.store(0, std::memory_order_relaxed);
		result->dwCRC = dwCRC;
		result->dwLength = dwLength;
		CopyMemory(result->value, ptr, dwLength);
		container.emplace(key{dwCRC, dwLength, result->value}, result);
	}

	// exit
	cs.Leave();
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

u32 smem_container::stat_economy()
{
	cs.Enter();
	s64 counter = 0;
	counter -= sizeof(*this);
	const int node_size = 20;
	for (const auto& item : container)
	{
		const smem_value* value = item.second;
		counter -= 16;
		counter -= node_size;
		counter += s64((s64(value->dwReference.load(std::memory_order_relaxed)) - 1) * s64(value->dwLength));
	}
	cs.Leave();

	return u32(s64(counter) / s64(1024));
}

smem_container::~smem_container()
{
	clean();
}
