#include "stdafx.h"

#include "svp_log.h"

// pip diagnostics land in $logs$\pip.log, one stamped line per call, flushed per line
void PipMsg(const char* format, ...)
{
	if (!format)
		return;

	static xrCriticalSection pipGuard;
	static FILE* pipFile = nullptr;
	static bool pipFailed = false;
	static u64 pipT0 = 0;

	string2048 buf;
	va_list mark;
	va_start(mark, format);
	int sz = _vsnprintf(buf, sizeof(buf) - 1, format, mark);
	buf[sizeof(buf) - 1] = 0;
	va_end(mark);
	if (sz <= 0)
		return;

	xrCriticalSectionGuard g(&pipGuard);
	if (pipFailed)
		return;
	if (!pipFile)
	{
		string_path fn;
		xr_strcpy(fn, "pip.log");
		if (FS.path_exist("$logs$"))
			FS.update_path(fn, "$logs$", "pip.log");
		pipFile = fopen(fn, "w");
		if (!pipFile)
		{
			pipFailed = true;
			return;
		}
		pipT0 = GetTickCount64();
	}
	const u64 ms = GetTickCount64() - pipT0;
	fprintf(pipFile, "[%6llu.%03llu] %s\n", ms / 1000, ms % 1000, buf);
	fflush(pipFile);
}
