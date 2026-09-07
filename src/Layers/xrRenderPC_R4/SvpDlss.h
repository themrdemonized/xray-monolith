#pragma once

// pip engine-side contract for the SVP DLSS-SR upscale. OUR struct, consumed by Ascii's future
// EvalSVP_DLSS body. ZERO Streamline/NGX/sl:: dependency, engine + D3D11 types only.
//
// MV convention (rt_ssfx_motion_vectors, ScreenSpaceShaders screenspace_mvectors.h ssfx_mv_calc):
//   value = ((cur.xy/cur.w) - (prev.xy/prev.w)), x kept, y negated, whole thing * 0.5
//   units = normalized UV / texture-space (NDC delta * 0.5), NOT pixels
//   dir   = current - previous (this frame minus last)
//   Y     = texture-space, Y down (the y component is negated)
//   jitter is baked into proj at gate 1 so the SVP MV carries the camera-jitter delta, jitter_px
//   below is the raw offset so Ascii can strip it if the eval wants jitter-free MVs
// FTreeVisual foliage emits per-viewport camera-motion velocity (wind delta ~0), interactive grass
// bend is once-per-frame (not per-viewport) so grass velocity is shared with the main camera.
//
// OPEN ITEMS to confirm against your working main-view DLSS wrapper (the engine side is plumbed, these
// depend on what your Streamline integration expects):
//  - DEPTH form: depth_srv is the SVP hardware Z (R24G8 typeless, D24_UNORM readable). if your main view
//    feeds DLSS a linear / view-space depth, provide the SVP equivalent instead (e.g. rt_Position$svp)
//  - COLOR space: color_srv is taken post-combine (post tonemap for LDR, or HDR10) where the lens samples,
//    confirm it matches the color space your main-view DLSS consumes
//  - OUTPUT UAV: out_uav is non-null ONLY when rt_secondVP is the HDR (A16B16G16R16F) format, a BGRA
//    A8R8G8B8 output cannot take a typed UAV, so a UAV-capable SVP output is required for a compute eval
//  - EXPOSURE: not provided (auto-exposure assumed), use exposure_srv if your eval needs an explicit one
//  - JITTER: the stub generator + apply are placeholders, make the offset baked into proj and reported in
//    jitter_px use your DLSS phase + convention (both live in the swappable svp_jitter_* fns)
//  - RESOURCE STATE: the seam does not unbind the combine RTV before the eval nor rebind after (the stub
//    needs neither), a compute eval must bind inputs as SRVs / output as UAV and restore (see EvalSVP_DLSS)

struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
struct ID3D11UnorderedAccessView;

struct SvpDlssInputs
{
	struct uint2 { u32 x = 0, y = 0; };

	u32 viewport_id = 1; // mirrors Device.dwViewport for the SVP

	// inputs, pre-AA SVP scene at render extent
	ID3D11ShaderResourceView* color_srv = nullptr; // rt_Generic_0$svp
	uint2 render_extent;
	ID3D11ShaderResourceView* depth_srv = nullptr; // baseZB$svp (optional, Ascii may not need it)
	ID3D11ShaderResourceView* mvec_srv = nullptr;  // rt_ssfx_motion_vectors$svp
	ID3D11ShaderResourceView* exposure_srv = nullptr; // optional, engine does not populate it (auto-exposure)

	// output at display extent
	ID3D11RenderTargetView* out_rtv = nullptr;    // rt_secondVP (stub keeps it render-extent + lens upscales, make it display-res)
	ID3D11UnorderedAccessView* out_uav = nullptr; // rt_secondVP UAV, null unless r__svp_dlss != 0 (Task 9)
	uint2 display_extent;

	// raw camera matrices from Device.matrices[1] / matrices_previous[1], row-major row-vector,
	// jitter is baked into proj, Ascii builds clipToPrevClip / prevClipToClip his own way
	Fmatrix view, proj, view_proj;
	Fmatrix prev_view, prev_proj, prev_view_proj;

	Fvector2 jitter_px;          // raw sub-pixel jitter baked into proj, pixels
	bool depth_inverted = false; // X-Ray depth is 0 near to 1 far (not reversed-Z)

	// cached scene constants from the svpCamera tail (near_plane/far_plane avoid the windef near/far macros)
	float near_plane = 0.f, far_plane = 0.f, fov = 0.f, aspect = 1.f; // fov in radians
	Fvector cam_pos, up, right, fwd;

	bool reset = false; // snapshot of dlss_reset_next at consume time, history invalidate
};
