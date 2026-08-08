#include "stdafx.h"
#include "ShaderSourceCRC.h"

namespace
{
void addShaderSourceCrc32(const void* sourceData, u32 sourceSize, LPCSTR shaderPath, u32& crc);

IReader* openShaderInclude(LPCSTR shaderPath, LPCSTR includeName)
{
	string_path includePath;
	strconcat(sizeof(includePath), includePath, shaderPath ? shaderPath : "", includeName);

	IReader* reader = FS.r_open("$game_shaders$", includePath);
	if (reader)
		return reader;

	return FS.r_open("$game_shaders$", includeName);
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

void addIncludedShaderCrc32(LPCSTR shaderPath, LPCSTR includeName, u32& crc)
{
    IReader* includeReader = openShaderInclude(shaderPath, includeName);
    if (!includeReader)
    {
        Msg("! Shader source CRC: can't find include '%s', skipping it for cache validation", includeName);
        return;
    }

    addShaderSourceCrc32(includeReader->pointer(), includeReader->length(), shaderPath, crc);
    FS.r_close(includeReader);
}

void parseShaderIncludes(const char* sourceData, u32 sourceSize, LPCSTR shaderPath, u32& crc)
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
			addIncludedShaderCrc32(shaderPath, includeName, crc);
	}
}

void addShaderSourceCrc32(const void* sourceData, u32 sourceSize, LPCSTR shaderPath, u32& crc)
{
	crc = crc32(sourceData, sourceSize, crc);
	parseShaderIncludes(static_cast<const char*>(sourceData), sourceSize, shaderPath, crc);
}
} // namespace

u32 getShaderSourceCrc32(const void* sourceData, u32 sourceSize, LPCSTR shaderPath)
{
	u32 crc = 0;
	addShaderSourceCrc32(sourceData, sourceSize, shaderPath, crc);
	return crc;
}
