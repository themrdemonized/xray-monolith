#include "stdafx.h"
#include "Weapon.h"
#include "actor.h"
#include "actoreffector.h"
#include "../xrEngine/CameraBase.h"
#include "../xrEngine/CameraManager.h"
#include "level.h"
#include "player_hud.h"

// pip weapon raise settled threshold, GetZRotatingFactor at or above this reads as fully aimed
static const float SVP_SETTLED_ROT = 0.999f;

// pip hosts the swing envelope that drives the scope shadow crescent
void CWeapon::ApplySvpSightAnchor(CActor* pActor, Fmatrix& trans)
{
	auto& vp = Device.m_SecondViewport;
	// the aim reference axis for the swing envelope
	{
		const Fvector cr = pActor->cam_FirstEye()->vDirection;
		// swing envelope for the shadow, aim axis angular acceleration reads recoil kicks and
		// hard swings, smooth tracking holds near zero so calm aim never charges it
		{
			static Fvector s_env_dir = {0.f, 0.f, 1.f};
			static float s_env_w = 0.f;
			static float s_env_accel = 0.f;
			const float dt = _max(Device.fTimeDelta, 0.001f);
			// cross magnitude keeps the tiny frame angles numerically clean, a short smooth
			// window drops the single frame spikes that flickered the crescent at rest
			Fvector cx;
			cx.crossproduct(s_env_dir, cr);
			float sind = cx.magnitude();
			clamp(sind, 0.f, 1.f);
			const float w = asinf(sind) / dt;
			s_env_accel += (_abs(w - s_env_w) / dt - s_env_accel) * (1.f - expf(-dt / 0.04f));
			// the smoothed lateral rate picks the crescent side, the direction latches so the
			// fading crescent keeps its side instead of recentering into a ring
			{
				static Fvector2 s_env_swing = {0.f, 0.f};
				Fvector dm;
				dm.sub(cr, s_env_dir);
				Fvector up;
				up.set(pActor->cam_FirstEye()->vNormal);
				Fvector rt;
				rt.crossproduct(up, cr);
				rt.normalize_safe();
				const float ks = 1.f - expf(-dt / 0.12f);
				s_env_swing.x += (dm.dotproduct(rt) / dt - s_env_swing.x) * ks;
				s_env_swing.y += (dm.dotproduct(up) / dt - s_env_swing.y) * ks;
				const float sm = _sqrt(s_env_swing.x * s_env_swing.x + s_env_swing.y * s_env_swing.y);
				if (sm > 0.05f)
				{
					vp.svp_swing_x = s_env_swing.x / sm;
					vp.svp_swing_y = s_env_swing.y / sm;
				}
			}
			s_env_dir.set(cr);
			s_env_w = w;
			// charges only at settled aim so the raise and zoom rotate never draw the tunnel
			static int s_prev_ammo = -1;
			const int ammo = GetAmmoElapsed();
			const bool shot = (s_prev_ammo != -1 && ammo < s_prev_ammo);
			s_prev_ammo = ammo;
			extern int g_svp_crescent;
			if (g_svp_crescent && vp.IsSVPActive() && GetZRotatingFactor() > 0.999f)
			{
				// every shot kicks the crescent in, swings must clear a gate the walk bob cannot
				const float dz = _max(24.f / _max(vp.svp_mag, 1.f), 12.f);
				float sw = (s_env_accel - dz) / (0.5f * dz);
				clamp(sw, 0.f, 1.f);
				if (shot)
				{
					vp.svp_shadow_gain += 0.45f;
					clamp(vp.svp_shadow_gain, 0.f, 1.f);
				}
				else if (sw > vp.svp_shadow_gain)
					vp.svp_shadow_gain += (sw - vp.svp_shadow_gain) * (1.f - expf(-dt / 0.06f));
				// tuning numbers for the feel report, quiet at true rest
				if (s_env_accel > 2.f || vp.svp_shadow_gain > 0.05f)
				{
					static u32 s_sw_ms = 0;
					if (Device.dwTimeGlobal - s_sw_ms > 1000)
					{
						s_sw_ms = Device.dwTimeGlobal;
						PipMsg("[SVP-SWING] accel %.1f dz %.1f gain %.2f mag %.1f",
							s_env_accel, dz, vp.svp_shadow_gain, vp.svp_mag);
					}
				}
			}
		}
	}
}

void CWeapon::UpdateSecondVP()
{
	if (!(ParentIsActor() && (m_pInventory != NULL) && (m_pInventory->ActiveItem() == this)))
		return;

	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if (!scope_svp_enabled)
	{
		// legacy fake-SVP, stock activation condition unchanged
		Device.m_SecondViewport.SetSVPActive(m_zoomtype == 0 && pActor->cam_Active() == pActor->cam_FirstEye() && IsSecondVPZoomPresent() && m_zoom_params.m_fZoomRotationFactor > 0.05f);
		return;
	}

	// pip true SVP, the scope_debug force-path still needs IsZoomed so a debug cvar can't drive it off-ADS
	const bool svp_act = (scope_debug && IsSecondVPZoomPresent() && IsZoomed())
		|| (m_zoomtype == 0 && pActor->cam_Active() == pActor->cam_FirstEye() && IsSecondVPZoomPresent() && IsZoomed());
	// pip activation edge diag, every flip logs every term
	{
		auto& vp = Device.m_SecondViewport;
		static bool s_prev_act = false;
		if (svp_act != s_prev_act)
		{
			s_prev_act = svp_act;
			PipMsg("[SVP-ACT] %d zt=%d cam=%d zoomed=%d ok=%d lens_r=%.3f stale=%u zf=%.1f sec=%s",
				(int)svp_act, m_zoomtype,
				(int)(pActor->cam_Active() == pActor->cam_FirstEye()), (int)IsZoomed(),
				(int)vp.svp_sight_ok, vp.svp_lens_r,
				Device.dwFrame - vp.svp_sight_frame, GetZoomFactor(), cNameSect().c_str());
		}
	}
	Device.m_SecondViewport.SetSVPActive(svp_act);
	// the shadow swing envelope drains steadily, the charge site outruns it on real kicks
	Device.m_SecondViewport.svp_shadow_gain += (0.f - Device.m_SecondViewport.svp_shadow_gain)
		* (1.f - expf(-Device.fTimeDelta / 0.7f));
	// the lever throw holds the envelope up for the real throw time, the drain takes the tail
	{
		const float SVP_LEVER_THROW_S = 0.2f; // measured SpecterDR lever throw
		auto& vp = Device.m_SecondViewport;
		if (vp.svp_lever_ms)
		{
			const float t = (Device.dwTimeGlobal - vp.svp_lever_ms) / 1000.f;
			if (t >= 0.f && t < SVP_LEVER_THROW_S)
				vp.svp_shadow_gain = _max(vp.svp_shadow_gain, 1.f - t / SVP_LEVER_THROW_S);
			else
				vp.svp_lever_ms = 0;
		}
	}
	// aimed on irons or the launcher, the scope lens still renders but its pose is not the scope's
	Device.m_SecondViewport.svp_alt_sight = (m_zoomtype != 0 && IsZoomed());
	// authored recoil relax time publish, the recoil settle window derives from it
	Device.m_SecondViewport.svp_recoil_relax_s = (zoom_cam_recoil.RelaxSpeed > EPS)
		? zoom_cam_recoil.Dispersion / zoom_cam_recoil.RelaxSpeed : 0.f;

	// pip publish a raise transient free zoom for the svp camera, the raise holds the dialed
	// magnification and hands off to the live target after settle
	{
		extern int g_svp_zoom_sync;
		extern float scope_radius;
		auto& vp = Device.m_SecondViewport;
		// a selector, not an integrator, the raise publishes the steady dialed magnification and the
		// settled aim publishes the weapon's own glided factor so one integrator drives image + reticle
		float pub;
		if (!g_svp_zoom_sync || !vp.IsSVPActive() || !IsZoomed())
			pub = GetZoomFactor(); // fallback publishes current, the >1 guard falls back to hud_params.y when unset
		else if (GetZRotatingFactor() > SVP_SETTLED_ROT)
			pub = GetZoomFactor(); // settled, the weapon glide is the single integrator
		else
			pub = m_zoom_params.m_bUseDynamicZoom
				? (scope_radius > 0.0 ? m_fRTZoomFactor / scope_scrollpower : m_fRTZoomFactor)
				: CurrentZoomFactor(); // raise, hold the steady dialed magnification, no easing needed
		vp.svp_zoom_pub = pub;
		// config factors ride the 75 base and rescale to the live fov downstream, script
		// authored factors already carry the user fov and pass through
		extern float g_fov;
		vp.svp_fov_scale = m_zoom_params.m_bScriptedZoom ? 1.f : (g_fov / SVP_ZOOM_BASE_FOV);
		// the main view stays wide at g_fov through a pip scope, publish it as the punch free mag
		// reference so a recoil fov effector never reaches scope_magnification
		vp.svp_aim_fov = g_fov;
		// authored mag flat optics keep the clean optical mag, the panel subtense override stays off
		vp.svp_authored_mag = m_zoom_params.m_bSvpAuthoredMin;
		// authored mins and detent base mins are 75 base, the legacy optical model min rides g_fov
		vp.svp_min_75base = m_zoom_params.m_bSvpAuthoredMin || SvpDetentBase();
	}

	// pip mirror the live ballistic ray + muzzle point for the [3DB] overlay, and range the zero:
	// the shot converges where the sight line meets the aimed surface, capped at g_svp_zero
	{
		extern float g_svp_zero;
		auto& vp = Device.m_SecondViewport;
		// the pick remaps through the actor's real eye under demo_record (CHudItem::Ray)
		const SPickParam& pp = GetPick();
		vp.fire_ray_pos.set(pp.defs.start);
		vp.fire_ray_dir.set(pp.defs.dir);
		vp.muzzle_pos.set(get_LastFP());
		// the actor's true eye, immune to the demo camera, the overlay's crosshair ray
		vp.eye_ray_pos.set(pActor->cam_FirstEye()->vPosition);
		vp.eye_ray_dir.set(pActor->cam_FirstEye()->vDirection);
		float zero_eff = g_svp_zero;
		if (g_svp_zero > 0.f && vp.IsSVPActive() && vp.svp_sight_ok)
		{
			// the published sight line, never the render scratch (zeroes at frame start mid tick)
			Fvector so = vp.svp_sight_pos;
			Fvector sd = vp.svp_sight_dir;
			collide::rq_result RQ;
			if (Level().ObjectSpace.RayPick(so, sd, g_svp_zero, collide::rqtBoth, RQ, H_Parent()))
				zero_eff = _max(RQ.range, 2.f);
		}
		vp.fire_ray_zero = zero_eff;
		vp.fire_ray_frame = Device.dwFrame;
	}
}

// pip SVP readiness, true when a fresh captured lens exists, reads the stable sight publish
bool CWeapon::GetSVPCameraMatrix()
{
	auto& vp = Device.m_SecondViewport;
	return vp.svp_sight_ok && vp.svp_lens_r > EPS && Device.dwFrame - vp.svp_sight_frame < 8;
}

// pip zeroing, the shot converges onto the sight line at the ranged zero, then the tracer
// ring records the final departure ray, called from CActor::g_fireParams
void svp_apply_zero_and_trace(const SPickParam& pp, Fvector& fire_pos, Fvector& fire_dir)
{
	// pip zeroing, the shot converges onto the sight line at the ranged zero (0 disables)
	extern float g_svp_zero;
	auto& vp = Device.m_SecondViewport;
	if (g_svp_zero > 0.f && Device.true_pip_on && vp.IsSVPActive() && vp.svp_sight_ok)
	{
		const float zero_m = (vp.fire_ray_zero > 0.f) ? vp.fire_ray_zero : g_svp_zero;
		// scoped shots depart the muzzle, the stock ray stays when the barrel is blocked
		if (!pp.barrel_blocked && vp.muzzle_pos.square_magnitude() > EPS)
			fire_pos.set(vp.muzzle_pos);
		// the stable published sight line
		Fvector so = vp.svp_sight_pos;
		Fvector axis = vp.svp_sight_dir;
		Fvector zero_pt;
		zero_pt.mad(so, axis, zero_m);
		Fvector d;
		d.sub(zero_pt, fire_pos);
		if (d.magnitude() > 1.f)
		{
			d.normalize();
			fire_dir.set(d);
		}
	}

	// pip [3DB] tracer ring, records the final departure ray of every shot for the fading overlay
	{
		auto& tr = vp.fire_traces[vp.fire_trace_head % 16];
		tr.pos.set(fire_pos);
		tr.dir.set(fire_dir);
		tr.time_ms = Device.dwTimeGlobal;
		vp.fire_trace_head++;
	}
}
