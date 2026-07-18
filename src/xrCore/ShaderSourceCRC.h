#pragma once

XRCORE_API u32 getShaderSourceCrc32(const void* sourceData, u32 sourceSize, LPCSTR shaderPath);
XRCORE_API u32 getShaderSourceCrc32Cached(const void* sourceData, u32 sourceSize, LPCSTR shaderPath,
	LPCSTR sourceName, LPCSTR target);
XRCORE_API void clearShaderSourceCrcCache();
