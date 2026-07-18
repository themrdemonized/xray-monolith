#include "stdafx.h"
#include "ShaderSourceCRC.h"

namespace
{
struct ShaderSourceDependency
{
	xr_string path;
	u32 crc;
	u32 size_real;
	u32 size_compressed;
	u32 modif;
};

struct ShaderSourceCrcCacheEntry
{
	u32 crc;
	xr_vector<ShaderSourceDependency> dependencies;
};

xrCriticalSection& ShaderSourceCrcCacheLock()
{
	static xrCriticalSection* lock = xr_new<xrCriticalSection>();
	return *lock;
}

xr_map<xr_string, ShaderSourceCrcCacheEntry>& ShaderSourceCrcCache()
{
	static xr_map<xr_string, ShaderSourceCrcCacheEntry>* cache =
		xr_new<xr_map<xr_string, ShaderSourceCrcCacheEntry>>();
	return *cache;
}

void addShaderSourceCrc32(const void* sourceData, u32 sourceSize, LPCSTR shaderPath, u32& crc,
	xr_vector<ShaderSourceDependency>* dependencies);

IReader* openShaderInclude(LPCSTR shaderPath, LPCSTR includeName, string_path& resolved)
{
	string_path includePath;
	strconcat(sizeof(includePath), includePath, shaderPath ? shaderPath : "", includeName);

	if (FS.exist(resolved, "$game_shaders$", includePath))
		return FS.r_open(resolved);

	if (FS.exist(resolved, "$game_shaders$", includeName))
		return FS.r_open(resolved);
	return nullptr;
}

bool getIncludeName(LPCSTR sourceLine, string_path& includeName)
{
	string4096 line;
	xr_strcpy(line, sourceLine);
	_Trim(line);

	if (!line[0] || line[0] != '#' || !strstr(line, "#include"))
		return false;

	if (!_GetItem(line, 1, includeName, '"'))
		return false;

	xr_strlwr(includeName);
	return true;
}

void addIncludedShaderCrc32(LPCSTR shaderPath, LPCSTR includeName, u32& crc,
	xr_vector<ShaderSourceDependency>* dependencies)
{
	string_path resolved;
    IReader* includeReader = openShaderInclude(shaderPath, includeName, resolved);
    if (!includeReader)
    {
        Msg("! Shader source CRC: can't find include '%s', skipping it for cache validation", includeName);
        return;
    }

	if (dependencies)
	{
		const CLocatorAPI::file* file = FS.exist(resolved);
		if (file)
			dependencies->push_back({resolved, file->crc, file->size_real, file->size_compressed, file->modif});
	}
    addShaderSourceCrc32(includeReader->pointer(), includeReader->length(), shaderPath, crc, dependencies);
    FS.r_close(includeReader);
}

void parseShaderIncludes(const char* sourceData, u32 sourceSize, LPCSTR shaderPath, u32& crc,
	xr_vector<ShaderSourceDependency>* dependencies)
{
	const char* cursor = sourceData;
	const char* const sourceEnd = sourceData + sourceSize;

	while (cursor < sourceEnd)
	{
		const char* const lineBegin = cursor;
		while (cursor < sourceEnd && *cursor != '\r' && *cursor != '\n')
			++cursor;

		const size_t lineLength = cursor - lineBegin;

		while (cursor < sourceEnd && (*cursor == '\r' || *cursor == '\n'))
			++cursor;

		if (lineLength >= sizeof(string4096))
			continue;

		string4096 line;
		CopyMemory(line, lineBegin, lineLength);
		line[lineLength] = 0;

		string_path includeName;
		if (getIncludeName(line, includeName))
			addIncludedShaderCrc32(shaderPath, includeName, crc, dependencies);
	}
}

void addShaderSourceCrc32(const void* sourceData, u32 sourceSize, LPCSTR shaderPath, u32& crc,
	xr_vector<ShaderSourceDependency>* dependencies)
{
	crc = crc32(sourceData, sourceSize, crc);
	parseShaderIncludes(static_cast<const char*>(sourceData), sourceSize, shaderPath, crc, dependencies);
}

bool dependenciesUnchanged(const ShaderSourceCrcCacheEntry& entry)
{
	for (const ShaderSourceDependency& dependency : entry.dependencies)
	{
		const CLocatorAPI::file* file = FS.exist(dependency.path.c_str());
		if (!file || file->crc != dependency.crc || file->size_real != dependency.size_real ||
			file->size_compressed != dependency.size_compressed || file->modif != dependency.modif)
		{
			return false;
		}
	}
	return true;
}
} // namespace

u32 getShaderSourceCrc32(const void* sourceData, u32 sourceSize, LPCSTR shaderPath)
{
	u32 crc = 0;
	addShaderSourceCrc32(sourceData, sourceSize, shaderPath, crc, nullptr);
	return crc;
}

u32 getShaderSourceCrc32Cached(const void* sourceData, u32 sourceSize, LPCSTR shaderPath, LPCSTR sourceName,
	LPCSTR target)
{
	xr_string relative = shaderPath ? shaderPath : "";
	relative += sourceName ? sourceName : "";
	relative += '.';
	if (target && target[0])
	{
		relative += target[0];
		if (target[1])
			relative += target[1];
	}

	xr_string key = relative;
	string_path resolved;
	if (const CLocatorAPI::file* source = FS.exist(resolved, "$game_shaders$", relative.c_str()))
	{
		string128 identity;
		xr_sprintf(identity, "|%08x:%08x:%08x:%08x", source->crc, source->size_real,
			source->size_compressed, source->modif);
		key = resolved;
		key += identity;
	}
	else
	{
		string32 size;
		xr_sprintf(size, "|%08x", sourceSize);
		key += size;
	}

	ShaderSourceCrcCacheEntry cached_entry = {};
	bool cached = false;
	{
		xrCriticalSectionGuard guard(ShaderSourceCrcCacheLock());
		auto found = ShaderSourceCrcCache().find(key);
		if (found != ShaderSourceCrcCache().end())
		{
			cached_entry = found->second;
			cached = true;
		}
	}
	if (cached && dependenciesUnchanged(cached_entry))
		return cached_entry.crc;

	ShaderSourceCrcCacheEntry entry = {};
	addShaderSourceCrc32(sourceData, sourceSize, shaderPath, entry.crc, &entry.dependencies);
	{
		xrCriticalSectionGuard guard(ShaderSourceCrcCacheLock());
		ShaderSourceCrcCache()[key] = entry;
	}
	return entry.crc;
}

void clearShaderSourceCrcCache()
{
	xrCriticalSectionGuard guard(ShaderSourceCrcCacheLock());
	ShaderSourceCrcCache().clear();
}
