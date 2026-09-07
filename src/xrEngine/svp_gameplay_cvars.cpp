#include "stdafx.h"
#include "svp_gameplay_cvars.h"
#include "xr_ioconsole.h"
#include "xr_ioc_cmd.h"

float g_zoom_smooth = 12.f; // pip dynamic-scope zoom smoothing rate (lerp per second), 0 = instant stepped feel
float g_zoom_analog = 0.f; // pip dynamic-scope analog zoom, 0 = discrete config steps, >0 = fine steps across the range
int g_zoom_clicks = 1; // pip a scope authoring zoom_step_count 1 clicks between its two detents, ignoring analog and smoothing, 0 = off
int g_svp_zoom_base = 1; // pip true svp scopes derive the bottom detent in the authored 75 base so it renders 1x, 0 = legacy fov derivation
int g_svp_authored_mags = 1; // pip svp scopes read authored magnifications from the scope section, 0 = legacy scope_zoom_factor derivation
float g_svp_zero = 100.f; // pip auto-zero cap in meters, shots converge on the aimed surface up to this range, 0 = raw fire axis
int g_svp_unify_cam_fx = 1; // pip camera-only effector kicks also rotate the weapon while a PiP scope is aimed, 0 = stock split
int g_svp_world_cam_fx = 1; // pip cam effectors keep driving the main view while a PiP scope is aimed, 0 = frozen surround
int g_svp_hud_true_fov = 0; // pip the hud weapon renders at the true scene fov while a PiP scope is aimed (0 = stock viewmodel fov)
int g_svp_zoom_sync = 1; // pip the svp renders the dialed magnification through the raise, 0 = track the live zoom factor
int g_svp_crescent = 0; // pip swing crescent master switch, 0 hides the parallax crescent under a pip scope
float g_svp_sens = 1.f; // pip scoped mouse sensitivity multiplier, applies only while the svp renders
float g_svp_sens_curve = 1.f; // pip zoom sens response exponent, 1 = proportional to magnification, 0 = flat

void svp_gameplay_cvars_init()
{
	CMD4(CCC_Float, "g_zoom_smooth", &g_zoom_smooth, 0.f, 60.f);
	CMD4(CCC_Float, "g_zoom_analog", &g_zoom_analog, 0.f, 200.f);
	CMD4(CCC_Integer, "g_zoom_clicks", &g_zoom_clicks, 0, 1);
	CMD4(CCC_Integer, "g_svp_zoom_base", &g_svp_zoom_base, 0, 1);
	CMD4(CCC_Integer, "g_svp_authored_mags", &g_svp_authored_mags, 0, 1);
	CMD4(CCC_Float, "g_svp_zero", &g_svp_zero, 0.f, 1000.f);
	CMD4(CCC_Integer, "g_svp_unify_cam_fx", &g_svp_unify_cam_fx, 0, 1);
	CMD4(CCC_Integer, "g_svp_world_cam_fx", &g_svp_world_cam_fx, 0, 1);
	CMD4(CCC_Integer, "g_svp_hud_true_fov", &g_svp_hud_true_fov, 0, 1);
	CMD4(CCC_Integer, "g_svp_zoom_sync", &g_svp_zoom_sync, 0, 1);
	CMD4(CCC_Integer, "g_svp_crescent", &g_svp_crescent, 0, 1);
	CMD4(CCC_Float, "g_svp_sens", &g_svp_sens, 0.1f, 3.f);
	CMD4(CCC_Float, "g_svp_sens_curve", &g_svp_sens_curve, 0.f, 2.f);
}
