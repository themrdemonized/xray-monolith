#include "stdafx.h"
#include "../../xrEngine/xr_object.h"
#include "FBasicVisual.h"
#include "SkeletonCustom.h"

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

void CRender::render_lights(light_Package& LP)
{
	xr_map<light*, std::pair<Fvector, Fvector>> saved_pos;
	//////////////////////////////////////////////////////////////////////////
	// 0. apply hud_mode projection if necessary
	hud_light_apply(saved_pos, LP.v_shadowed);
	hud_light_apply(saved_pos, LP.v_point);
	hud_light_apply(saved_pos, LP.v_spot);

	{
#if defined(USE_DX10) || defined(USE_DX11)
		PIX_EVENT(SHADOWED_LIGHTS);
#endif
		{
#if defined(USE_DX10) || defined(USE_DX11)
			PIX_EVENT(PHASE_VIS_UPDATE);
#endif
			xr_vector<light*>& source = LP.v_shadowed;
			source.erase(std::remove_if(source.begin(), source.end(), [](light* L)
			{
				if(L->m_parent)
				{
					if(L->m_parent->omnipart[0] == L)
					{
						L->m_parent->vis_update();
						for (int f = 0; f < 6; f++)
						{
							L->m_parent->omnipart[f]->vis.pending = L->m_parent->vis.pending;
							L->m_parent->omnipart[f]->vis.visible = L->m_parent->vis.visible;
						}
					}
				}
				else
					L->vis_update();
				if (!L->vis.visible)
					return true;

				L->optimize_smap_size();

				return false;
			}), source.end());
		}

		{
#if defined(USE_DX10) || defined(USE_DX11)
			PIX_EVENT(PHASE_CALC_POOLS);
#endif
			xr_vector<light*>& source = LP.v_shadowed;
			static xr_vector<light*> refactored;
			refactored.clear();
			u32 total = (u32)source.size();

			for (u16 smap_ID = 0; refactored.size() != total; smap_ID++)
			{
				LP_smap_pool.initialize(o.smapsize);
				std::sort(source.begin(), source.end(), [](light* _1, light* _2) {return _1->X.S.size > _2->X.S.size; });
				source.erase(std::remove_if(source.begin(), source.end(), [smap_ID](light* L)
				{
					SMAP_Rect R;
					if (RImplementation.LP_smap_pool.push(R, L->X.S.size))
					{
						L->X.S.posX = R.min.x;
						L->X.S.posY = R.min.y;
						L->vis.smap_ID = smap_ID;
						refactored.push_back(L);
						return true;
					}
					return false;
				}), source.end());
			}

			std::reverse(refactored.begin(), refactored.end());
			LP.v_shadowed = refactored;
		}

		//////////////////////////////////////////////////////////////////////////
		// sort lights by importance???
		// while (has_any_lights_that_cast_shadows) {
		//		if (has_point_shadowed)		->	generate point shadowmap
		//		if (has_spot_shadowed)		->	generate spot shadowmap
		//		switch-to-accumulator
		//		if (has_point_unshadowed)	-> 	accum point unshadowed
		//		if (has_spot_unshadowed)	-> 	accum spot unshadowed
		//		if (was_point_shadowed)		->	accum point shadowed
		//		if (was_spot_shadowed)		->	accum spot shadowed
		//	}
		//	if (left_some_lights_that_doesn't cast shadows)
		//		accumulate them
		while (!LP.v_shadowed.empty())
		{
			// if (has_spot_shadowed)
			static xr_vector<light*> L_spot_s;
			{
#if defined(USE_DX10) || defined(USE_DX11)
				PIX_EVENT(GENERATE_SHMAPS);
#endif
				// generate spot shadowmap
				Target->phase_smap_spot_clear();
				xr_vector<light*>& source = LP.v_shadowed;
				light* L = source.back();
				u16			sid = L->vis.smap_ID;
				while (!source.empty())
				{
					if (source.empty())		break;
					L = source.back();
					if (L->vis.smap_ID != sid)	break;
					source.pop_back();
					// render
					phase = PHASE_SMAP;
#if defined(USE_DX10) || defined(USE_DX11)
					PIX_EVENT(RENDER_SHADOWS);
#endif
					bool decorative_light = false;
					if (L->flags.bHudMode)
					{
						L_spot_s.push_back(L);
						decorative_light = true;
					}
					else
					{
						if ((L->decor_object[0] && !L->decor_object[0]->getDestroy()) || (L->decor_object[1] && !L->decor_object[1]->getDestroy()) || (L->decor_object[2] && !L->decor_object[2]->getDestroy()) || (L->decor_object[3] && !L->decor_object[3]->getDestroy()) || (L->decor_object[4] && !L->decor_object[4]->getDestroy()) || (L->decor_object[5] && !L->decor_object[5]->getDestroy()))
						{
							for (int f = 0; f < 6; f++)
							{
								if (L->decor_object[f] && !L->decor_object[f]->getDestroy())
								{
									L->decor_object[f]->renderable_Render(&L->GMLight);
									decorative_light = true;
								}
							}
						}
						else
						{
							if (L->m_moving_frames<32u)
							{
								L->GMLight.RGraph.clear_static<false>();
								L->GMLight.traverse((CSector*)L->SpatialComponent->spatial.sector, L->X.S.frustum, L->position, L->X.S.combine);
								L->GMLight.r_dsgraph_capture_static();
								L->m_moving_frames++;
							}
							L->GMLight.r_dsgraph_capture_dynamic(L->ignore_object);
						}
					}

					bool bDeffered_Shadows = L->GMLight.RGraph.mapStaticPasses[0][0].size() || L->GMLight.RGraph.mapDynamicPasses[0][0].size();
					bool bForward_Shadows = L->GMLight.RGraph.mapStaticPasses[1][0].size() || L->GMLight.RGraph.mapDynamicPasses[1][0].size() || L->GMLight.RGraph.mapStaticSorted.Sorted.size() || L->GMLight.RGraph.mapDynamicSorted.Sorted.size();
					if (bDeffered_Shadows || bForward_Shadows)
					{
						L_spot_s.push_back(L);
						Target->phase_smap_spot(L);
						RCache.set_xform_world(Fidentity);
						RCache.set_xform_view(L->X.S.view);
						RCache.set_xform_project(L->X.S.project);
						L->GMLight.r_dsgraph_render_static(0, false);
						L->GMLight.r_dsgraph_render_dynamic(0, true);
						if (Details && Details->dtFS && check_grass_shadow(L, ViewBase) && L->flags.bShadow && !decorative_light)
						{
							Details->fade_distance = -1; // Use light position to calc "fade"
							Details->light_position.set(L->position);
							Details->hw_Render(L);
						}
					
						L->X.S.transluent = FALSE;
						if (bForward_Shadows)
						{
							L->X.S.transluent = TRUE;
							Target->phase_smap_spot_tsh(L);
					
							L->GMLight.r_dsgraph_render_static(1, false);
							L->GMLight.r_dsgraph_render_dynamic(1, true);
					
							L->GMLight.r_dsgraph_render_sorted();			// strict-sorted geoms
						}
					}
					else if (L->flags.bVolumetric && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS))
					{
						L_spot_s.push_back(L);
					}
				}
			}
			//		if (was_spot_shadowed)		->	accum spot shadowed
			if (!L_spot_s.empty())
			{
				PROF_EVENT("ACCUM_SPOT");
				for (light* L : L_spot_s)
				{
					Target->accum_spot(L);
					render_indirect(L);
					if (L->flags.bVolumetric && RImplementation.o.advancedpp && ps_r2_ls_flags.is(R2FLAG_VOLUMETRIC_LIGHTS))
					{
#ifdef USE_DX11
						// Current Resolution
						float w = float(Device.dwWidth);
						float h = float(Device.dwHeight);

						// Adjust resolution
						if (RImplementation.o.ssfx_volumetric)
							Target->set_viewport_size(HW.pContext, w / 8, h / 8);
#endif
						Target->accum_volumetric(L);
#ifdef USE_DX11
						// Restore resolution
						if (RImplementation.o.ssfx_volumetric)
							Target->set_viewport_size(HW.pContext, w, h);
#endif
					}
				}

				L_spot_s.clear();
			}
		}
	}

	{
#if defined(USE_DX10) || defined(USE_DX11)
		PIX_EVENT(UNSHADOWED_LIGHTS);
#endif
		{
#if defined(USE_DX10) || defined(USE_DX11)
			PIX_EVENT(POINT_LIGHTS_ACCUM_UNSH);
#endif
			// Point lighting (unshadowed, if left)
			if (!LP.v_point.empty())
			{
				for (light* L : LP.v_point)
				{
					L->vis_update();
					if (!L->vis.visible)
						continue;

					Target->accum_point(L);
					render_indirect(L);
				}
				LP.v_point.clear();
			}
		}
		{
#if defined(USE_DX10) || defined(USE_DX11)
			PIX_EVENT(SPOT_LIGHTS_ACCUM_UNSH);
#endif
			// Spot lighting (unshadowed, if left)
			if (!LP.v_spot.empty())
			{
				for (light* L : LP.v_spot)
				{
					L->vis_update();
					if (!L->vis.visible)
						continue;

					Target->accum_spot(L);
					render_indirect(L);
				}
				LP.v_spot.clear();
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
		LIGEN.SpatialComponent->spatial.sector = LI.S;
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
