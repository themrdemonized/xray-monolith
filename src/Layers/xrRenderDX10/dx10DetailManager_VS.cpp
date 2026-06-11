#include "stdafx.h"
#include "../xrRender/DetailManager.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "../xrRenderDX10/dx10BufferUtils.h"

// Vars to store wind prev frame data ( Motion vectors )
static u32 prev_frame = -1;
static float prev_time = 0;
static Fvector4	prev_dir1 = { 0, 0, 0 }, prev_dir2 = { 0, 0, 0 };

const int quant = 16384;
const int c_hdr = 10;
const int c_size = 4;

static D3DVERTEXELEMENT9 dwDecl[] =
{
	{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, // pos
	{0, 12, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, // uv
	D3DDECL_END()
};

#pragma pack(push,1)
struct vertHW
{
	float x, y, z;
	short u, v, t, mid;
};
#pragma pack(pop)

short QC(float v);
//{
//	int t=iFloor(v*float(quant)); clamp(t,-32768,32767);
//	return short(t&0xffff);
//}

float GoToValue(float& current, float go_to)
{
	float diff = abs(current - go_to);

	float r_value = Device.fTimeDelta;

	if (diff - r_value <= 0)
	{
		current = go_to;
		return 0;
	}

	return current < go_to ? r_value : -r_value;
}

// f32 -> f16 (round-to-nearest; inputs are tame: [-1..1] normals, [0..1] scalars)
ICF u16 dm_f32tof16(float v)
{
	union { float f; u32 u; } c;
	c.f = v;
	u32 sign = (c.u >> 16) & 0x8000u;
	s32 exp = s32((c.u >> 23) & 0xFFu) - 127 + 15;
	u32 mant = c.u & 0x7FFFFFu;
	if (exp <= 0) return u16(sign);            // underflow -> signed zero
	if (exp >= 31) return u16(sign | 0x7BFFu); // overflow -> max finite half
	u32 h = sign | (u32(exp) << 10) | (mant >> 13);
	h += (mant >> 12) & 1u;                    // round up; carry into exponent is correct
	return u16(h);
}

// new instance record layout
// half4(terrain normal xyz, alpha)
// half4(sun, hemi, _, _)
#pragma pack(push, 1)
struct InstanceHW
{
	Fvector4 m0, m1, m2;
	u16 na[4]; // terrain normal xyz + alpha
	u16 sh[4]; // sun, hemi, spare, spare
};
#pragma pack(pop)
static_assert(sizeof(InstanceHW) == CDetailManager::hw_InstanceStride, "InstanceHW must match hw_InstanceStride");

void CDetailManager::hw_Load_Shaders()
{
	// Create shader to access constant storage
	ref_shader S;
	S.create("details\\set");
	R_constant_table& T0 = *(S->E[0]->passes[0]->constants);
	R_constant_table& T1 = *(S->E[1]->passes[0]->constants);
	hwc_consts = T0.get("consts");
	hwc_wave = T0.get("wave");
	hwc_wind = T0.get("dir2D");
	hwc_array = T0.get("array");
	hwc_s_consts = T1.get("consts");
	hwc_s_xform = T1.get("xform");
	hwc_s_array = T1.get("array");
}

void CDetailManager::hw_Render()
{
	// Render-prepare
	//	Update timer
	//	Can't use Device.fTimeDelta since it is smoothed! Don't know why, but smoothed value looks more choppy!
	float fDelta = Device.fTimeGlobal - m_global_time_old;
	if ((fDelta < 0) || (fDelta > 1)) fDelta = 0.03;
	m_global_time_old = Device.fTimeGlobal;

	m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
	m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
	m_time_pos += fDelta * swing_current.speed;

	//float		tm_rot1		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot1);
	//float		tm_rot2		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot2);
	float tm_rot1 = m_time_rot_1;
	float tm_rot2 = m_time_rot_2;

	Fvector4 dir1, dir2;
	dir1.set(_sin(tm_rot1), 0, _cos(tm_rot1), 0).normalize().mul(swing_current.amp1);
	dir2.set(_sin(tm_rot2), 0, _cos(tm_rot2), 0).normalize().mul(swing_current.amp2);

	// Setup geometry and DMA
	RCache.set_Geometry(hw_Geom);

	// bind the per-instance vertex stream to slot 1 (filled once per frame in hw_Fill_Instances)
	{
		UINT istride = hw_InstanceStride;
		UINT ioffset = 0;
		HW.pContext->IASetVertexBuffers(1, 1, &hw_instanceVB, &istride, &ioffset);
	}

    // fill this once per frame, and let later render phases (cascades, grass shadow lights) reuse it
	if (hw_frame_filled != Device.dwFrame)
	{
		Device.Statistic->RenderDUMP_DT_Count = 0; // accumulates across phases this frame
		hw_Fill_Instances();
	}

	// Wave0
	float scale = 1.f / float(quant);
	Fvector4 wave, prev_wave;
	Fvector4 consts;
	consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
	//wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
	prev_wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, prev_time);
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir1, prev_wave.div(PI_MUL_2), prev_dir1, 1, 0);

	// Wave1
	wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
	prev_wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, prev_time);
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 2, 0);

	// Still
	consts.set(scale, scale, scale, 1.f);
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 0, 1);

	if (prev_frame != Device.dwFrame)
	{
		prev_frame = Device.dwFrame;

		// Prev Frame swing time
		prev_time = m_time_pos;

		// Prev frame dir
		prev_dir1.set(dir1);
		prev_dir2.set(dir2);
	}
}

void CDetailManager::hw_Fill_Instances()
{
    // packs all the visible instances into a contiguous instance buffer with a single map
	void* pInstData;
#ifdef USE_DX11
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHK_DX(HW.pContext->Map(hw_instanceVB, 0, D3D_MAP_WRITE_DISCARD, 0, &mapped));
	pInstData = mapped.pData;
#else
	CHK_DX(hw_instanceVB->Map(D3D_MAP_WRITE_DISCARD, 0, &pInstData));
#endif
	InstanceHW* pInst = (InstanceHW*)pInstData;

	u32 instTotal = 0;
	BOOL bOverflow = FALSE;
	for (u32 vid = 0; vid < 3; vid++)
	{
		vis_list& list = m_visibles[vid];
		for (u32 O = 0; O < objects.size(); O++)
		{
			hw_inst_base[vid][O] = instTotal;

			xr_vector<SlotItemVec*>& vis = list[O];
			xr_vector<SlotItemVec*>::iterator _vI = vis.begin();
			xr_vector<SlotItemVec*>::iterator _vE = vis.end();
			for (; _vI != _vE; _vI++)
			{
				SlotItemVec* items = *_vI;
				SlotItemVecIt _iI = items->begin();
				SlotItemVecIt _iE = items->end();
				for (; _iI != _iE; _iI++)
				{
					SlotItem& Instance = **_iI;

                    // we only need to run alpha smoothing once per frame now!
					Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);

					float scale = Instance.scale_calculated;
					if (scale <= 0 || Instance.alpha <= 0)
						break;

					if (instTotal >= (u32)hw_InstanceCapacity)
					{
						bOverflow = TRUE;
						break;
					}

					// 3x4 transform rows + packed terrain-normal/alpha + sun/hemi
					Fmatrix& M = Instance.mRotY;
					InstanceHW& R = pInst[instTotal];
					R.m0.set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
					R.m1.set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
					R.m2.set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
					R.na[0] = dm_f32tof16(Instance.normal.x);
					R.na[1] = dm_f32tof16(Instance.normal.y);
					R.na[2] = dm_f32tof16(Instance.normal.z);
					R.na[3] = dm_f32tof16(Instance.alpha);
					R.sh[0] = dm_f32tof16(Instance.c_sun);
					R.sh[1] = dm_f32tof16(Instance.c_hemi);
					R.sh[2] = 0;
					R.sh[3] = 0;
					instTotal++;
				}
				if (instTotal >= (u32)hw_InstanceCapacity)
					break;
			}
			hw_inst_count[vid][O] = instTotal - hw_inst_base[vid][O];

			// UpdateVisibleM rebuilds the lists on the next frame
			if (!vis.empty())
				vis.clear_not_free();
		}
	}

#ifdef USE_DX11
	HW.pContext->Unmap(hw_instanceVB, 0);
#else
	hw_instanceVB->Unmap();
#endif

	if (bOverflow)
		Msg("! [DETAILS] instance buffer overflow, cap(%d) - some grass dropped this frame", (u32)hw_InstanceCapacity);

	hw_frame_filled = Device.dwFrame;
}

void CDetailManager::hw_Render_dump(const Fvector4& consts, const Fvector4& wave, const Fvector4& wind,
									const Fvector4& prev_wave, const Fvector4& prev_wind, u32 var_id, u32 lod_id)
{
	static shared_str strConsts("consts");
	static shared_str strWave("wave");
	static shared_str strDir2D("dir2D");
	static shared_str strXForm("xform");

	static shared_str strWavePrev("wave_prev");
	static shared_str strDir2DPrev("dir2D_prev");

	static shared_str strPrevPos("benders_prevpos");
	static shared_str strPos("benders_pos");
	static shared_str strGrassSetup("benders_setup");

	static shared_str strGrassAlign("grass_align");

	// phase scale fading (now applied in the vs)
	static shared_str strFadeParams("dt_fade_params");

	u32 total = 0;
	for (u32 obj = 0; obj < objects.size(); obj++)
		total += hw_inst_count[var_id][obj];
	if (total == 0)
		return;

	// grass benders data
	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
	Fvector4 player_pos = { 0, 0, 0, 0 };
	int BendersQty = _min(16, ps_ssfx_grass_interactive.y + 1);

	// add player if the grass is interactive
	if (ps_ssfx_grass_interactive.x > 0)
		player_pos.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);

	// fade mode (mirrors the legacy CPU-side formulas, see DetailManager.cpp):
    // x = 1: camera-distance fade vs y (squared distance threshold);
	// x = 2: light xz-distance fade vs zw (light position)
	Fvector4 fade_params;
	if (fade_distance <= -1)
		fade_params.set(2.f, 0.f, light_position.x, light_position.z);
	else
		fade_params.set(1.f, fade_distance, 0.f, 0.f);

	u32 maxPasses = 1;
	for (u32 O = 0; O < objects.size(); O++)
	{
		if (!hw_inst_count[var_id][O])
			continue;
		ShaderElement* E = objects[O]->shader->E[lod_id]._get();
		if (E)
			maxPasses = _max(maxPasses, (u32)E->passes.size());
	}

	for (u32 iPass = 0; iPass < maxPasses; ++iPass)
	{
		// detail meshes may use different textures/shaders. avoid unnecessary
        // rebinds by grouping consecutive objects
		ShaderElement* curE = 0;
		u32 vOffset = 0;
		u32 iOffset = 0;
		for (u32 O = 0; O < objects.size(); O++)
		{
			CDetail& Object = *objects[O];
			u32 count = hw_inst_count[var_id][O];
			ShaderElement* E = Object.shader->E[lod_id]._get();
			if (count && E && iPass < E->passes.size())
			{
				if (E != curE)
				{
					curE = E;
					RCache.set_Element(E, iPass);
					RImplementation.apply_lmaterial();

					RCache.set_c(strConsts, consts);
					RCache.set_c(strWave, wave);
					RCache.set_c(strDir2D, wind);
					RCache.set_c(strXForm, Device.mFullTransform);
					RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align);
					RCache.set_c(strWavePrev, prev_wave);
					RCache.set_c(strDir2DPrev, prev_wind);
					RCache.set_c(strFadeParams, fade_params);

					if (ps_ssfx_grass_interactive.y > 0)
					{
						RCache.set_c(strGrassSetup, ps_ssfx_int_grass_params_1);

						Fvector4* c_grass;
						{
							void* GrassData;
							RCache.get_ConstantDirect(strPos, BendersQty * sizeof(Fvector4) * 2, &GrassData, 0, 0);
							c_grass = (Fvector4*)GrassData;
						}
						if (c_grass)
						{
							c_grass[0].set(player_pos);
							c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

							for (int Bend = 1; Bend < BendersQty; Bend++)
							{
								c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y, GData.pos[Bend].z, GData.radius_curr[Bend]);
								c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y, GData.dir[Bend].z, GData.str[Bend]);
							}
						}

						Fvector4* c_prev_grass;
						{
							void* prev_GrassData;
							RCache.get_ConstantDirect(strPrevPos, BendersQty * sizeof(Fvector4) * 2, &prev_GrassData, 0, 0);
							c_prev_grass = (Fvector4*)prev_GrassData;
						}
						if (c_prev_grass)
						{
							for (int Bend = 0; Bend < BendersQty; Bend++)
							{
								c_prev_grass[Bend].set(GData.prev_pos[Bend]);
								c_prev_grass[Bend + 16].set(GData.prev_dir[Bend]);
							}
						}
					}
				}

				RCache.RenderInstanced(D3DPT_TRIANGLELIST, count, vOffset, 0, Object.number_vertices,
				                       iOffset, Object.number_indices / 3, hw_inst_base[var_id][O]);
				Device.Statistic->RenderDUMP_DT_Count += count;
				RCache.stat.r.s_details.add(count * Object.number_vertices);
			}
			vOffset += Object.number_vertices;
			iOffset += Object.number_indices;
		}
	}
}
