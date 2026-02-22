#pragma once

enum class ECoreParams : u8
{
	// Core
	auto_load_arch,
	overlaypath,
	nolog,
	build,
	ebuild,
	mem_debug,

	// Engine
	xclsx,
	no_center_screen,
	dxdebug,
	perfhud_hack,
	nes_texture_storing,
	noprefetch,
	demomode,
	nosound,
	r2,
	r4,

	// Render
	nocolormap,
	skinw,
	disasm,
	nonvs,
	tsh,
	noshadows,
	ss_tga,
	no_occq,
	nodistort,

	// Game
	use_callstack,
	debug_ge,
	
	// API
	renderdoc,

	// Anomaly
	no_dialog_header,
	game_designer,
	r4_dev,
	no_bump_mode1,
	no_bump_mode2,
	noramtex,
	nodf24,
	r4xx,
	smap1536,
	smap2048,
	smap2560,
	smap3072,
	smap4096,
	bug,
	sunfilter,
	sjitter,
	depth16,
	mt_cdb,
	pure_alloc,
	memo,
	x86,
	editor,
	tune,
	dbg,
	dbgdev,
	ltx,
	r2a,
	slowdown,
	slowdown2x,
	dbgact,
	dbgbullet,
	nointro,
	skiplogo,
	designer,
	netsim,
	dump_traffic,
	nojit,
	_g,
	break_on_assert,
	prefetch_sounds,
	keep_lua,
	_60hz,
	dxgi_old,
	dxgi_dbg,
	gloss,
	start,
	load,
	psp,
	ignore_save_incompatibility,
	sound_constant_speed,
	savescreenshots,
	lua_studio,
	clear_cs_constants
};

namespace xrParams {
	void LoadParams();
}
