#include "stdafx.h"

static char const* print_time(time_t const& src_time, string64& dest_time)
{
	tm* tmp_tm = _localtime64(&src_time);
	xr_sprintf(dest_time, sizeof(dest_time),
	           "%02d.%02d.%d_%02d:%02d:%02d",
	           tmp_tm->tm_mday,
	           tmp_tm->tm_mon + 1,
	           tmp_tm->tm_year + 1900,
	           tmp_tm->tm_hour,
	           tmp_tm->tm_min,
	           tmp_tm->tm_sec
	);
	return dest_time;
}
