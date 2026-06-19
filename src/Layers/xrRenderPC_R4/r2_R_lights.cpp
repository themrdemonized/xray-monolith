#include "stdafx.h"

IC bool pred_area(light* _1, light* _2)
{
	u32 a0 = _1->X.S.size;
	u32 a1 = _2->X.S.size;
	return a0 > a1; // reverse -> descending
}

bool check_grass_shadow(light* L, CFrustum VB)
{
	// Grass shadows are allowed?
	if (ps_ssfx_grass_shadows.x < 3 || !psDeviceFlags2.test(rsGrassShadow))
		return false;

	// Inside the range?
	if (L->vis.distance > ps_ssfx_grass_shadows.z)
		return false;

	// Is in view? L->vis.visible?
	u32 mask = 0xff;
	if (!VB.testSphere(L->position, L->range * 0.6f, mask))
		return false;

	return true;
}

IC void hud_light_apply(xr_map<light*, std::pair<Fvector, Fvector>>& saved_pos, xr_vector<light*>& source)
{
	for (u32 it = 0; it < source.size(); it++)
	{
		light* L = source[it];
		if (!L->get_hud_mode()) continue;

		saved_pos.emplace(L, mk_pair(L->position, L->direction));

		Device.hud_to_world(L->position);
		Device.hud_to_world_dir(L->direction);
	}
}

IC void hud_light_restore(xr_map<light*, std::pair<Fvector, Fvector>>& saved_pos, xr_vector<light*>& source)
{
	for (const auto& saved : saved_pos)
	{
		light* L = saved.first;
		if (!L->get_hud_mode()) continue;

		L->position = saved.second.first;
		L->direction = saved.second.second;
	}
}

// pip build half, pack all shadowed lights into one scaled atlas and render their shadow maps once for the main viewport
// the accumulate half render_lights is then re-callable per viewport
void CRender::render_lights_shadowmaps(light_Package& LP)
{
	PIX_EVENT(SHADOWED_LIGHTS);

	Target->phase_smap_spot_clear(Target->rt_smap_depth);
	HOM.Disable();

	// Rebuild atlas until all lights fit (shrink size_factor until they do)
	bool success = false;
	float size_factor = 1.0;
	while (!success)
	{
		success = true;
		LP_smap_pool.initialize(RImplementation.o.smapsize);
		for (auto L : LP.v_shadowed)
		{
			LR.compute_xf_spot(L);
			SMAP_Rect R;
			if (LP_smap_pool.push(R, int(L->X.S.size * size_factor)))
			{
				L->X.S.posX = R.min.x;
				L->X.S.posY = R.min.y;
			}
			else
			{
				success = false;
			}
		}
		if (!success)
			size_factor *= 0.8;
	}

	// Render the shadowmaps
	for (auto L : LP.v_shadowed)
	{
		stats.s_used++;

		// Make sure to use the scaled size we computed above
		L->X.S.size *= size_factor;

		{
			PIX_EVENT(LIGHT_SHADOWMAP_RENDER);

			// render
			phase = PHASE_SMAP;
			if (RImplementation.o.Tshadows) r_pmask(true, true);
			else r_pmask(true, false);
			PIX_EVENT(SHADOWED_LIGHTS_RENDER_SUBSPACE);
			r_dsgraph_render_subspace(L->spatial.sector, L->X.S.combine, L->position, TRUE);
			bool bNormal = mapNormalPasses[0][0].size() || mapMatrixPasses[0][0].size();
			bool bSpecial = mapNormalPasses[1][0].size() || mapMatrixPasses[1][0].size() || mapSorted.size();
			if (bNormal || bSpecial)
			{
				stats.s_merged++;
				Target->phase_smap_spot(L);
				RCache.set_xform_world(Fidentity);
				RCache.set_xform_view(L->X.S.view);
				RCache.set_xform_project(L->X.S.project);
				r_dsgraph_render_graph(0);
				if (Details)
				{
					if (check_grass_shadow(L, ViewBase))
					{
						Details->fade_distance = -1; // Use light position to calc "fade"
						Details->light_position.set(L->position);
						Details->Render();
					}
				}
				L->X.S.transluent = FALSE;
				if (bSpecial)
				{
					L->X.S.transluent = TRUE;
					Target->phase_smap_spot_tsh(L);
					PIX_EVENT(SHADOWED_LIGHTS_RENDER_GRAPH);
					r_dsgraph_render_graph(1); // normal level, secondary priority
					PIX_EVENT(SHADOWED_LIGHTS_RENDER_SORTED);
					r_dsgraph_render_sorted(); // strict-sorted geoms
				}
			}
			else
			{
				stats.s_finalclip++;
			}
			r_pmask(true, false);
		}
	}
}

// pip accumulate half, accumulate the already-built shadow maps and unshadowed point/spot into the active target
// no shadow-map generation here so it is re-callable per viewport against the same maps
void CRender::render_lights(light_Package& LP)
{
	xr_map<light*, std::pair<Fvector, Fvector>> saved_pos;
	//////////////////////////////////////////////////////////////////////////
	// 0. apply hud_mode projection if necessary
	hud_light_apply(saved_pos, LP.v_shadowed);
	hud_light_apply(saved_pos, LP.v_point);
	hud_light_apply(saved_pos, LP.v_spot);

	{
		PIX_EVENT(SHADOWED_LIGHTS);
		HOM.Disable();
		Target->phase_accumulator();
		for (auto L : LP.v_shadowed)
		{
			PIX_EVENT(SPOT_LIGHTS_ACCUM_VOLUMETRIC);
			//		if (was_spot_shadowed)		->	accum spot shadowed
			Target->accum_spot(L);
			render_indirect(L);

			if (RImplementation.o.advancedpp && ps_r2_ls_flags.is(R2FLAG_VOLUMETRIC_LIGHTS))
			{
				// Current Resolution
				float w = float(Device.dwWidth);
				float h = float(Device.dwHeight);

				// Adjust resolution (base volumetric specifics: fixed /8, no volsize/_lv/mode)
				if (RImplementation.o.ssfx_volumetric)
					Target->set_viewport_size(HW.pContext, w / 8, h / 8);

				Target->accum_volumetric(L);

				// Restore resolution
				if (RImplementation.o.ssfx_volumetric)
					Target->set_viewport_size(HW.pContext, w, h);
			}
		}
	}

	{
		PIX_EVENT(POINT_LIGHTS_ACCUM);
		// Point lighting (unshadowed, if left)
		if (!LP.v_point.empty())
		{
			xr_vector<light*>& Lvec = LP.v_point;
			for (u32 pid = 0; pid < Lvec.size(); pid++)
			{
				render_indirect(Lvec[pid]);
				Target->accum_point(Lvec[pid]);
			}
		}
	}

	{
		PIX_EVENT(SPOT_LIGHTS_ACCUM);
		// Spot lighting (unshadowed, if left)
		if (!LP.v_spot.empty())
		{
			xr_vector<light*>& Lvec = LP.v_spot;
			for (u32 pid = 0; pid < Lvec.size(); pid++)
			{
				LR.compute_xf_spot(Lvec[pid]);
				render_indirect(Lvec[pid]);
				Target->accum_spot(Lvec[pid]);
			}
		}
	}

	// restore world projection if necessary
	hud_light_restore(saved_pos, LP.v_shadowed);
	hud_light_restore(saved_pos, LP.v_point);
	hud_light_restore(saved_pos, LP.v_spot);
}

void CRender::render_indirect(light* L)
{
	if (!ps_r2_ls_flags.test(R2FLAG_GI)) return;

	light LIGEN;
	LIGEN.set_type(IRender_Light::REFLECTED);
	LIGEN.set_shadow(false);
	LIGEN.set_cone(PI_DIV_2 * 2.f);

	xr_vector<light_indirect>& Lvec = L->indirect;
	if (Lvec.empty()) return;
	float LE = L->color.intensity();
	for (u32 it = 0; it < Lvec.size(); it++)
	{
		light_indirect& LI = Lvec[it];

		// energy and color
		float LIE = LE * LI.E;
		if (LIE < ps_r2_GI_clip) continue;
		Fvector T;
		T.set(L->color.r, L->color.g, L->color.b).mul(LI.E);
		LIGEN.set_color(T.x, T.y, T.z);

		// geometric
		Fvector L_up, L_right;
		L_up.set(0, 1, 0);
		if (_abs(L_up.dotproduct(LI.D)) > .99f) L_up.set(0, 0, 1);
		L_right.crossproduct(L_up, LI.D).normalize();
		LIGEN.spatial.sector = LI.S;
		LIGEN.set_position(LI.P);
		LIGEN.set_rotation(LI.D, L_right);

		// range
		// dist^2 / range^2 = A - has infinity number of solutions
		// approximate energy by linear fallof Emax / (1 + x) = Emin
		float Emax = LIE;
		float Emin = 1.f / 255.f;
		float x = (Emax - Emin) / Emin;
		if (x < 0.1f) continue;
		LIGEN.set_range(x);

		Target->accum_reflected(&LIGEN);
	}
}
