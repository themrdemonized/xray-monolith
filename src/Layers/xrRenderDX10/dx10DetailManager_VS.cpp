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

	// Bind the per-instance vertex stream to slot 1 (filled per-pass in hw_Render_dump).
	// The backend only tracks slot 0, so we bind slot 1 directly each frame.
	{
		UINT istride = hw_InstanceStride;
		UINT ioffset = 0;
		HW.pContext->IASetVertexBuffers(1, 1, &hw_instanceVB, &istride, &ioffset);
	}

	// Wave0
	float scale = 1.f / float(quant);
	Fvector4 wave, prev_wave;
	Fvector4 consts;
	consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
	//wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
	prev_wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, prev_time);
	//RCache.set_c			(&*hwc_consts,	scale,		scale,		ps_r__Detail_l_aniso,	ps_r__Detail_l_ambient);				// consts
	//RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
	//RCache.set_c			(&*hwc_wind,	dir1);																					// wind-dir
	//hw_Render_dump			(&*hwc_array,	1, 0, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir1, prev_wave.div(PI_MUL_2), prev_dir1, 1, 0);

	// Wave1
	//wave.set				(1.f/3.f,		1.f/7.f,	1.f/5.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
	prev_wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, prev_time);
	//RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
	//RCache.set_c			(&*hwc_wind,	dir2);																					// wind-dir
	//hw_Render_dump			(&*hwc_array,	2, 0, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 2, 0);

	// Still
	consts.set(scale, scale, scale, 1.f);
	//RCache.set_c			(&*hwc_s_consts,scale,		scale,		scale,				1.f);
	//RCache.set_c			(&*hwc_s_xform,	Device.mFullTransform);
	//hw_Render_dump			(&*hwc_s_array,	0, 1, c_hdr );
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

void CDetailManager::hw_Render_dump(const Fvector4& consts, const Fvector4& wave, const Fvector4& wind, 
									const Fvector4& prev_wave, const Fvector4& prev_wind, u32 var_id, u32 lod_id)
{
	static shared_str strConsts("consts");
	static shared_str strWave("wave");
	static shared_str strDir2D("dir2D");
	static shared_str strArray("array");
	static shared_str strXForm("xform");

	// Vanilla grass/trees wind
	static shared_str strWavePrev("wave_prev");
	static shared_str strDir2DPrev("dir2D_prev");

	// Grass Benders
	static shared_str strPrevPos("benders_prevpos");
	static shared_str strPos("benders_pos");
	static shared_str strGrassSetup("benders_setup");

	static shared_str strExData("exdata");
	static shared_str strGrassAlign("grass_align");

	// Grass benders data
	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
	Fvector4 player_pos = { 0, 0, 0, 0 };
	int BendersQty = _min(16, ps_ssfx_grass_interactive.y + 1);

	// Add Player?
	if (ps_ssfx_grass_interactive.x > 0)
		player_pos.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);

	Device.Statistic->RenderDUMP_DT_Count = 0;

	vis_list& list = m_visibles[var_id];

	CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
	Fvector c_sun, c_ambient, c_hemi;
	c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
	c_sun.mul(.5f);
	c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
	c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);

	// One per-instance record: 3x4 transform rows + color + terrain-normal/alpha (80 bytes,
	// matches dwDecl slot-1 layout and hw_InstanceStride).
	struct InstanceHW { Fvector4 m0, m1, m2, c0, data; };
	struct DrawRange { u32 instBase, instCount, vOffset, iOffset, prims; };
	DrawRange ranges[dm_max_objects];

	// ---- FILL: pack all visible instances of this variant into the instance VB (one map) ----
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
	u32 vOffset = 0;
	u32 iOffset = 0;
	for (u32 O = 0; O < objects.size(); O++)
	{
		CDetail& Object = *objects[O];
		ranges[O].vOffset = vOffset;
		ranges[O].iOffset = iOffset;
		ranges[O].prims = Object.number_indices / 3;
		ranges[O].instBase = instTotal;
		ranges[O].instCount = 0;

		xr_vector<SlotItemVec*>& vis = list[O];
		if (!vis.empty())
		{
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

					Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);

					float scale = Instance.scale_calculated;

					// Sort of fade using the scale
					// fade_distance == -1 use light_position to define "fade", anything else uses fade_distance
					if (fade_distance <= -1)
						scale *= 1.0f - Instance.position.distance_to_xz_sqr(light_position) * 0.005f;
					else if (Instance.distance > fade_distance)
						scale *= 1.0f - abs(Instance.distance - fade_distance) * 0.005f;

					if (scale <= 0 || Instance.alpha <= 0)
						break;

					if (instTotal >= (u32)hw_InstanceCapacity)
						break;

					// Build matrix ( 3x4 matrix ) + color + terrain-normal/alpha
					Fmatrix& M = Instance.mRotY;
					InstanceHW& R = pInst[instTotal];
					R.m0.set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
					R.m1.set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
					R.m2.set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
					R.c0.set(Instance.c_sun, Instance.c_sun, Instance.c_sun, Instance.c_hemi);
					R.data.set(Instance.normal.x, Instance.normal.y, Instance.normal.z, Instance.alpha);
					instTotal++;
				}
				if (instTotal >= (u32)hw_InstanceCapacity)
					break;
			}
			ranges[O].instCount = instTotal - ranges[O].instBase;

			// Clean up
			// KD: we must not clear vis on r2 since we want details shadows
			if (ps_ssfx_grass_shadows.x <= 0)
			{
				if (!psDeviceFlags2.test(rsGrassShadow) || ((ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) && (RImplementation.PHASE_SMAP ==
					RImplementation.phase)) // phase smap with shadows
					|| (ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) && (RImplementation.PHASE_NORMAL == RImplementation.phase)
						&& (!RImplementation.is_sun())) // phase normal with shadows without sun
					|| (!ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) && (RImplementation.PHASE_NORMAL == RImplementation.phase))
					)) // phase normal without shadows
					vis.clear_not_free();
			}
		}
		vOffset += Object.number_vertices;
		iOffset += Object.number_indices;
	}

#ifdef USE_DX11
	HW.pContext->Unmap(hw_instanceVB, 0);
#else
	hw_instanceVB->Unmap();
#endif

	if (instTotal == 0)
		return;

	// ---- DRAW: set element + globals once (all detail objects share one shader), then one
	//      hardware-instanced draw per mesh type using StartInstanceLocation. No per-draw CB churn. ----
	ShaderElement* E = &*objects[0]->shader->E[lod_id];
	for (u32 iPass = 0; iPass < E->passes.size(); ++iPass)
	{
		RCache.set_Element(E, iPass);
		RImplementation.apply_lmaterial();

		RCache.set_c(strConsts, consts);
		RCache.set_c(strWave, wave);
		RCache.set_c(strDir2D, wind);
		RCache.set_c(strXForm, Device.mFullTransform);
		RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align);
		RCache.set_c(strWavePrev, prev_wave);
		RCache.set_c(strDir2DPrev, prev_wind);

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

		for (u32 O = 0; O < objects.size(); O++)
		{
			if (ranges[O].instCount == 0)
				continue;
			RCache.RenderInstanced(D3DPT_TRIANGLELIST, ranges[O].instCount, ranges[O].vOffset, 0,
			                       objects[O]->number_vertices, ranges[O].iOffset, ranges[O].prims, ranges[O].instBase);
			Device.Statistic->RenderDUMP_DT_Count += ranges[O].instCount;
			RCache.stat.r.s_details.add(ranges[O].instCount * objects[O]->number_vertices);
		}
	}
}
