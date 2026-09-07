#include "stdafx.h"
#include "svp_mags.h"
#include "../xrEngine/svp_gameplay_cvars.h"

// pip parse a 3dss magnifications string, single N fixed, dash M-N dynamic, comma M,N stepped toggle
// returns svp_mag_none on empty or malformed so the caller keeps the legacy factors
static svp_mag_mode parse_svp_magnifications(LPCSTR raw, float& min_mag, float& max_mag)
{
	if (!raw || !raw[0]) return svp_mag_none;
	string256 buf;
	u32 j = 0;
	for (LPCSTR p = raw; *p && j + 1 < sizeof(buf); ++p)
		if (*p != ' ' && *p != '\t') buf[j++] = *p;
	buf[j] = 0;
	if (!buf[0]) return svp_mag_none;

	float a = 0.f, b = 0.f; char extra = 0;
	if (strchr(buf, '-'))
	{
		if (sscanf(buf, "%f-%f%c", &a, &b, &extra) != 2) return svp_mag_none;
		min_mag = a; max_mag = b; return svp_mag_dynamic;
	}
	if (strchr(buf, ','))
	{
		if (sscanf(buf, "%f,%f%c", &a, &b, &extra) != 2) return svp_mag_none;
		min_mag = a; max_mag = b; return svp_mag_stepped;
	}
	if (sscanf(buf, "%f%c", &a, &extra) != 1) return svp_mag_none;
	min_mag = max_mag = a; return svp_mag_fixed;
}

// pip three tier resolution with the [SVP-MAGS] proof line, mode none keeps the legacy factors
// true when the section authors either tier, malformed stays loud and terminal
static bool resolve_from_section(LPCSTR sect, float zoom_multiple, svp_mags_data& out)
{
	float min_mag = 0.f, max_mag = 0.f;
	bool authored = false, dyn = false, stepped = false;

	LPCSTR raw = READ_IF_EXISTS(pSettings, r_string, sect, "magnifications", NULL);
	svp_mag_mode mode = parse_svp_magnifications(raw, min_mag, max_mag);
	if (mode != svp_mag_none)
	{
		authored = true;
		dyn = (mode != svp_mag_fixed);
		stepped = (mode == svp_mag_stepped);
	}
	else if (pSettings->line_exist(sect, "magnification"))
	{
		// singular tier, magnification is the top, min from min_magnification (default 1x) when dynamic
		max_mag = pSettings->r_float(sect, "magnification");
		const bool dzoom = READ_IF_EXISTS(pSettings, r_bool, sect, "scope_dynamic_zoom", false);
		min_mag = dzoom ? READ_IF_EXISTS(pSettings, r_float, sect, "min_magnification", 1.f) : max_mag;
		raw = "magnification";
		authored = true;
		dyn = dzoom;
	}

	if (!authored)
		return false;

	const float svp_mag_base = SVP_ZOOM_BASE_FOV / 0.75f; // 100, mag = base / f
	// physical band, 0.5x floor mirrors the 200 default min factor (100/200), 100x holds the top factor >= 1
	clamp(min_mag, svp_mag_base / 200.f, svp_mag_base);
	clamp(max_mag, svp_mag_base / 200.f, svp_mag_base);
	const float f_top = svp_mag_base / max_mag / zoom_multiple;
	const float f_floor = svp_mag_base / min_mag;
	// dynamic detents need an ordered range and the top factor under the base fov (NewGetZoomData VERIFY)
	if (min_mag > max_mag || (dyn && f_top >= SVP_ZOOM_BASE_FOV))
	{
		PipMsg("[SVP-MAGS] %s '%s' malformed, legacy retained", sect, raw ? raw : "");
		return true;
	}

	out.mode = dyn ? (stepped ? svp_mag_stepped : svp_mag_dynamic) : svp_mag_fixed;
	out.f_top = f_top;
	out.f_floor = f_floor;
	PipMsg("[SVP-MAGS] %s '%s' mag=[%.2f..%.2f] f=[%.1f..%.1f]",
		sect, raw ? raw : "", min_mag, max_mag, f_floor, f_top);
	return true;
}

// pip tries the passed section then the parent stripped scope section
// an unauthored wpn_x_1p59 with parent_section wpn_x resolves from 1p59
svp_mags_data svp_mags_resolve(LPCSTR sect, float zoom_multiple)
{
	svp_mags_data out;
	if (resolve_from_section(sect, zoom_multiple, out))
		return out;

	LPCSTR parent = READ_IF_EXISTS(pSettings, r_string, sect, "parent_section", NULL);
	if (parent && parent[0])
	{
		const u32 plen = xr_strlen(parent);
		if (xr_strlen(sect) > plen + 1 && 0 == strncmp(sect, parent, plen) && sect[plen] == '_')
		{
			LPCSTR suffix = sect + plen + 1;
			if (pSettings->section_exist(suffix))
				resolve_from_section(suffix, zoom_multiple, out);
		}
	}
	return out;
}
