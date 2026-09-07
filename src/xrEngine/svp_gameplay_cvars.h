#ifndef svp_gameplay_cvarsH
#define svp_gameplay_cvarsH
#pragma once

// true PiP gameplay cvars, xrEngine resident so xrGame reads them without linking xrRender

extern float g_zoom_smooth;
extern float g_zoom_analog;
extern int g_zoom_clicks;
extern int g_svp_zoom_base;
extern int g_svp_authored_mags;
extern float g_svp_zero;
extern int g_svp_unify_cam_fx;
extern int g_svp_world_cam_fx;
extern int g_svp_hud_true_fov;
extern int g_svp_zoom_sync;
extern int g_svp_crescent;
extern float g_svp_sens;
extern float g_svp_sens_curve;

// authored zoom factor convention base, a factor f renders mag (SVP_ZOOM_BASE_FOV / 0.75) / f
constexpr float SVP_ZOOM_BASE_FOV = 75.f;

// registers the svp gameplay console commands, called once from CCC_Register
extern void svp_gameplay_cvars_init();

#endif
