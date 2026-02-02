#include "stdafx.h"
#include <malloc.h>
#include <errno.h>

XRCORE_API void vminfo(size_t* _free, size_t* reserved, size_t* committed)
{
	MEMORY_BASIC_INFORMATION memory_info;
	memory_info.BaseAddress = 0;
	*_free = *reserved = *committed = 0;
	while (VirtualQuery(memory_info.BaseAddress, &memory_info, sizeof(memory_info)))
	{
		switch (memory_info.State)
		{
		case MEM_FREE:
			*_free += memory_info.RegionSize;
			break;
		case MEM_RESERVE:
			*reserved += memory_info.RegionSize;
			break;
		case MEM_COMMIT:
			*committed += memory_info.RegionSize;
			break;
		}
		memory_info.BaseAddress = (char*)memory_info.BaseAddress + memory_info.RegionSize;
	}
}

XRCORE_API void log_vminfo()
{
	size_t w_free, w_reserved, w_committed;
	vminfo(&w_free, &w_reserved, &w_committed);
	Msg(
		"* [win32]: free[%lld K], reserved[%lld K], committed[%lld K]",
		w_free / 1024,
		w_reserved / 1024,
		w_committed / 1024
	);
}

size_t xrMemory::mem_usage()
{
	// _heapwalk can hang or crash on corrupted heaps — skip it entirely,
	// use GlobalMemoryStatusEx instead for a safe memory report
	MEMORYSTATUSEX memInfo = {};
	memInfo.dwLength = sizeof(memInfo);
	if (GlobalMemoryStatusEx(&memInfo)) {
		return (size_t)(memInfo.ullTotalPhys - memInfo.ullAvailPhys);
	}
	return 0;
}
