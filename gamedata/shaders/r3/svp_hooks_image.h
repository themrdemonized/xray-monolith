// svp_hooks_image 20260715 thinhook
// relocated true-PiP scope image effects, included by scope_custom_image.h after LCD_RES
#ifndef SVP_HOOKS_IMAGE_INCLUDED
#define SVP_HOOKS_IMAGE_INCLUDED

#include "svp_hooks_common.h"

// contrast-adaptive sharpen delta (FidelityFX CAS core, cross taps), added onto the composited
// sample so chroma and blur stay intact, amp backs off at local contrast so nothing rings
float3 cas_delta(float2 tc, float amount)
{
	float2 ts = screen_res.zw;
	float3 c = sampleScopeTex(tc).rgb;
	float3 u = sampleScopeTex(tc + float2(0, -ts.y)).rgb;
	float3 d = sampleScopeTex(tc + float2(0, ts.y)).rgb;
	float3 l = sampleScopeTex(tc + float2(-ts.x, 0)).rgb;
	float3 r = sampleScopeTex(tc + float2(ts.x, 0)).rgb;
	float3 mn = min(c, min(min(u, d), min(l, r)));
	float3 mx = max(c, max(max(u, d), max(l, r)));
	float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 0.001)));
	float3 w = amp * (-0.2 * amount);
	return (c + (u + d + l + r) * w) / (1.0 + 4.0 * w) - c;
}

// true PiP NVG highlight roll-off, bright sources compress toward a phosphor bleach instead of the
// stock hard clamp to flat white. bleach tint/onset are authored placeholders
float3 apply_nvg_bleach(float2 tc, float3 img, float bleach, float sens)
{
	img = BlackandWhite(img);
	img = Brightness(img, 0.45, 7);
	float lum = dot(img, float3(0.299, 0.587, 0.114));
	float3 compressed = img / (1.0 + img);
	float roll = saturate((lum * sens - 1.0) * 2.0);
	float3 tube_bleach = float3(0.85, 1.0, 0.85);
	img = lerp(saturate(img), max(compressed, tube_bleach * roll), roll * bleach);
	img = LevelsPass(img);
	img = Grain1(img, tc);
	img = Grain2(img, tc);
	return img;
}

float svp_img_chroma_power(float CHROMA_POWER, Scope s)
{
	// true PiP chromatic aberration grows with magnification and linearly off-axis, the
	// optical center stays clean like real first-order lateral CA
	if (shader_scope_params.w < -1.5)
	{
		CHROMA_POWER *= clamp(curMag() / max(minMag(), 1.0), 1.0, 3.0);
		float2 ca_r = (s.tc0.xy - 0.5) * 2.0;
		CHROMA_POWER *= saturate(length(ca_r));
	}
	return CHROMA_POWER;
}

void svp_img_flat_window(inout float2 scope_tc, Scope s)
{
	// true PiP flat screen (binocular), the engine sizes the SVP to the panel aspect so tc0 fills the
	// panel directly, svp_glass2.w = the exact V-crop (1 = no crop when the SVP matches the panel)
	if (RETICLE_TYPE == RT_FLAT_SCREEN && shader_scope_params.w < -1.5)
	{
		float vcrop = (svp_glass2.w > 0.0001) ? svp_glass2.w : 1.0;
		scope_tc = float2(s.tc0.x, 0.5 + (s.tc0.y - 0.5) * vcrop);
	}
}

void svp_img_fix_flipped_lens(inout float2 scope_tc, Scope s)
{
	// auto-correct a mesh-inverted lens: some scope models author the lens V flipped so the world renders
	// upside down. test the raw mesh UV gradient so the image and reticle always agree
	if (shader_scope_params.w < -1.5 && svp_env.w > 0.5 && ddy(s.tc0.y) < 0.0)
		scope_tc.y = 1.0 - scope_tc.y;
}

void svp_img_thermal_fill(inout float2 scope_tc, Scope s)
{
		// true PiP panel fill, tc0 spans the panel exactly, svp_glass2.w = the engine V-crop
		// (1 = no crop when the SVP is sized to the panel aspect)
		if (shader_scope_params.w < -1.5)
		{
			float hw = (svp_glass2.w > 0.0001) ? svp_glass2.w : 1.0;
			scope_tc = float2(s.tc0.x, 0.5 + (s.tc0.y - 0.5) * hw);
			// authored pixelation on the filled frame at the optical mag (engine curMag is
			// ratio*optical), the fixed sensor grid is the fallback when the MCM toggle is off
			if (SETTING(SETTINGS, ST_THERMAL_PIXELATION)) {
				float pixelate = max(curMag() / max(svp_optics.w, 1.0), 1.0);
				scope_tc = (floor(scope_tc * LCD_RES / pixelate) * pixelate + 0.5) / LCD_RES;
			}
			else if (svp_control.w > 0.5)
				scope_tc = (floor(scope_tc * LCD_RES) + 0.5) / LCD_RES;
		}
}

void svp_img_sensor_noise(inout float3 back, float2 scope_tc)
{
		// per-cell sensor noise, reseeded ~12Hz
		if (shader_scope_params.w < -1.5 && svp_control.w > 0.5)
		{
			float2 cell = floor(scope_tc * LCD_RES);
			float n = frac(sin(dot(cell, float2(12.9898, 78.233)) + floor(timers.x * 12.0) * 3.7) * 43758.5453);
			back *= 0.92 + 0.16 * n;
		}
}

void svp_img_thermal_veil(inout float3 back, gbuffer_data gbd)
{
		// front-lens veil retired, svp_optics.z is a dead lane, no-op kept for the compat patch call
}

void svp_img_lcd_mask(inout float3 back)
{
		// LCD subpixel mask, the stock lcd_effect look re-enabled deliberately for the panel
		if (shader_scope_params.w < -1.5 && svp_control.w > 0.5)
		{
			int2 px = int2(scope.hpos.xy);
			float3 m = float3(0.4, 0.4, 0.4);
			int c = px.x % 3;
			if (c == 1) m.r = 1; else if (c == 2) m.g = 1; else m.b = 1;
			if (px.y % 3 == 0) m = float3(0.25, 0.25, 0.25);
			back *= lerp(float3(1, 1, 1), m * 2.0, 0.6);
		}
}

void svp_image_glass_fx(inout float3 back, float2 scope_tc, Scope s)
{
		// true PiP contrast-adaptive sharpen, recovers upscale/TAA softness without ringing
		// svp_glass3.z tapers the sharpen from the disc center to the rim, .w holds an inner crisp zone
		if (shader_scope_params.w < -1.5 && svp_glass3.x > 0.001)
		{
			float sharp_r = length((s.tc0.xy - 0.5) * 2.0);
			float sharp_knee = saturate((sharp_r - svp_glass3.w) / max(1.0 - svp_glass3.w, 1e-3));
			float sharp_taper = 1.0 - svp_glass3.z * smoothstep(0.0, 1.0, sharp_knee);
			back += cas_delta(scope_tc, svp_glass3.x * sharp_taper) * LENS_COLOR;
		}
		// true PiP heat mirage, hot distant ground shimmers, engine gates strength by sun heat + magnification
		if (shader_scope_params.w < -1.5 && svp_glass2.y > 0.001)
		{
			gbuffer_data mgb = gbuffer_load_data(scope_tc, scope_tc * screen_res.xy, 0);
			float farg = smoothstep(35.0, 120.0, mgb.P.z);
			float ground = saturate((s.tc0.y - 0.45) * 2.2);
			float amp = svp_glass2.y * farg * ground * 0.006;
			if (amp > 0.0001)
			{
				float wob = sin(s.tc0.x * 34.0 + timers.x * 7.0) * sin(s.tc0.y * 21.0 + timers.x * 5.0);
				back = sampleScopeTex(scope_tc + float2(0.0, wob * amp)).rgb * LENS_COLOR;
			}
		}
		// true PiP field curvature, the outer field softens like a real non-flat-field scope
		// flat screens have no eyepiece field, the UV radius would draw an ellipse on the wide panel
		if (shader_scope_params.w < -1.5 && svp_glass.y > 0.001 && RETICLE_TYPE != RT_FLAT_SCREEN)
		{
			float soft = smoothstep(0.65, 1.0, length((s.tc0.xy - 0.5) * 2.0)) * svp_glass.y;
			// skip the blur sample when the field curvature has no visible contribution
			if (soft > 0.001)
				back = lerp(back, blur_sample(scope_tc).rgb * LENS_COLOR, saturate(soft));
		}
		// true PiP rain on the objective glass, animated droplets refract the world (SSS hud-raindrop path)
		if (shader_scope_params.w < -1.5 && svp_env.y > 0.001)
		{
			float rain = saturate(svp_env.y * 4.0);
			float2 luv = s.tc0.xy;
			// low tiling = big coalesced droplets, not a fine mist; two layers merge into blobs
			float4 l0 = s_svp_rain.Sample(smp_base, luv * 1.8 + float2(0.0, -timers.x * 0.005));
			float4 l1 = s_svp_rain.Sample(smp_base, luv * 2.6 + 0.5);
			float4 d = max(l0, l1);
			// light rain lands sparse but real drops, the cutoff never fully starves below heavy
			float mask = saturate((1.0 - frac(d.w + timers.x * 0.35)) * d.x - (1.0 - rain) * 0.85);
			if (mask > 0.001)
			{
				float2 nrm = (float2(d.z, d.y) - 0.5) * 2.0;
				float3 refr = sampleScopeTex(scope_tc + nrm * 0.07 * mask).rgb * LENS_COLOR;
				back = lerp(back, refr, saturate(mask * 3.5)) + mask * 0.06;
			}
		}
		// true PiP scope-local eye adaptation, the capture is pre-tonemap and the main pass grades
		// it with the MAIN exposure, rescale by the scope's own measured exposure
		if (shader_scope_params.w < -1.5 && svp_exposure.x > 0)
		{
			float lm = s_tonemap.Load(int3(0, 0, 0)).x;
			float ls = s_tonemap_svp.Load(int3(0, 0, 0)).x;
			// s_tonemap_svp is bound by our packed scope_color_write.s, guard the 0 read so a
			// foreign loose scope_color_write.s (if one ever wins the VFS) can't floor the image
			if (lm > 0.0001 && ls > 0.0001)
				back *= clamp(svp_exposure.x * ls / lm, 0.25, 4.0);
		}
		// true PiP exit-pupil twilight dimming, engine-bound (zoom shrinks the exit pupil below the dark-adapted eye)
		if (shader_scope_params.w < -1.5 && svp_exposure.y > 0)
			back *= svp_exposure.y;
		// true PiP veiling glare, off-axis sun scatters off the coatings and washes the image near the sun
		if (shader_scope_params.w < -1.5 && svp_env.x > 0.001)
		{
			float g = saturate(svp_env.x);
			// the scatter carries the live sun chroma at the calibrated energy, neutral warm when the sun is dark
			float sun_l = dot(L_sun_color.rgb, float3(0.299, 0.587, 0.114));
			float3 scat = sun_l > 0.001 ? L_sun_color.rgb * (0.55 / sun_l) : float3(0.6, 0.55, 0.45);
			back = back * (1.0 - 0.5 * g) + scat * g * 0.85;
		}
		// true PiP lens coating, typical multi-coated glass transmission with a faint warm tint (AR reflects blue)
		if (shader_scope_params.w < -1.5 && svp_glass2.x > 0.001)
		{
			float ct = saturate(svp_glass2.x);
			back *= lerp(float3(1.0, 1.0, 1.0), float3(0.93, 0.915, 0.888), ct);
		}
		// lens UV debug: green top / red bottom of the SVP sample coord, a flipped gradient = flipped lens V
		if (shader_scope_params.w < -1.5 && svp_env.z > 0.5)
			back = float3(scope_tc.y, 1.0 - scope_tc.y, 0.2);
}

#endif
