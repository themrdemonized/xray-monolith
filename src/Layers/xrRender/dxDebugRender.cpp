#include "stdafx.h"
#include "dxDebugRender.h"
#include "dxUIShader.h"
#include "dxRenderDeviceRender.h"

dxDebugRender DebugRenderImpl;

extern float psHUD_FOV;

void dxDebugRender::Render()
{
	RCache.set_xform_world(Fidentity);

	if (!m_line_vertices_hud.empty())
	{
		// Change projection
		Fmatrix Pold = Device.mProject;
		Fmatrix FTold = Device.mFullTransform;
		Fmatrix FVold = Device.mView;
		Device.mView.build_camera_dir(Device.vCameraPosition, Device.vCameraDirection, Device.vCameraTop);
		Device.mProject.build_projection(
			deg2rad(psHUD_FOV * 83.f),
			Device.fASPECT, R_VIEWPORT_NEAR,
			g_pGamePersistent->Environment().CurrentEnv->far_plane);

		Device.mFullTransform.mul(Device.mProject, Device.mView);
		RCache.set_xform_project(Device.mProject);

		// Rendering
		::Render->rmNear();

		for (auto& m_vert : m_line_vertices_hud)
		{
			const u32& color = m_vert.first;
			std::vector<FVF::L>& vert_vec = m_vert.second;
			auto& ind_vec = m_line_indices_hud.at(color);

#if defined(USE_DX10) || defined(USE_DX11)
			RCache.set_Shader(dxRenderDeviceRender::Instance().m_WireShader);
			RCache.set_c("tfactor", float(color_get_R(color)) / 255.f, float(color_get_G(color)) / 255.f, float(color_get_B(color)) / 255.f, float(color_get_A(color)) / 255.f);
#endif
			RCache.dbg_Draw(D3DPT_LINELIST, &vert_vec.front(), vert_vec.size(), &ind_vec.front(), ind_vec.size() / 2);
		}

		m_line_vertices_hud.clear();
		m_line_indices_hud.clear();

		::Render->rmNormal();

		// Restore projection
		Device.mProject = Pold;
		Device.mFullTransform = FTold;
		Device.mView = FVold;
		RCache.set_xform_project(Device.mProject);
	}

	if (m_line_vertices.empty())
		return;

	for (auto& m_vert : m_line_vertices)
	{
		const u32& color = m_vert.first;
		std::vector<FVF::L>& vert_vec = m_vert.second;
		auto& ind_vec = m_line_indices.at(color);

#if defined(USE_DX10) || defined(USE_DX11)
		RCache.set_Shader(dxRenderDeviceRender::Instance().m_WireShader);
		RCache.set_c("tfactor", float(color_get_R(color)) / 255.f, float(color_get_G(color)) / 255.f, float(color_get_B(color)) / 255.f, float(color_get_A(color)) / 255.f);
#endif
		RCache.dbg_Draw(D3DPT_LINELIST, &vert_vec.front(), vert_vec.size(), &ind_vec.front(), ind_vec.size() / 2);
	}

	m_line_vertices.clear();
	m_line_indices.clear();
}

void dxDebugRender::add_lines(Fvector const* vertices, u32 const& vertex_count, u16 const* pairs, u32 const& pair_count, u32 const& color, bool bHud)
{
	size_t all_verts_count{}, all_inds_count{};
	for (auto& m_vert : m_line_vertices)
	{
		const u32& color = m_vert.first;
		const std::vector<FVF::L>& vert_vec = m_vert.second;
		all_verts_count += vert_vec.size();
		all_inds_count += m_line_indices.at(color).size();
	}

	for (auto& m_vert : m_line_vertices_hud)
	{
		const u32& color = m_vert.first;
		const std::vector<FVF::L>& vert_vec = m_vert.second;
		all_verts_count += vert_vec.size();
		all_inds_count += m_line_indices_hud.at(color).size();
	}

	if (((all_verts_count + vertex_count) >= u16(-1)) || ((all_inds_count + 2 * pair_count) >= u16(-1)))
		Render();

	auto& vert_vec = bHud ? m_line_vertices_hud[color] : m_line_vertices[color];
	auto& ind_vec = bHud ? m_line_indices_hud[color] : m_line_indices[color];

	const auto vertices_size = vert_vec.size(), indices_size = ind_vec.size();

	ind_vec.resize(indices_size + 2 * pair_count);
	auto I = ind_vec.begin() + indices_size, E = ind_vec.end();
	const u16* J = pairs;
	for (; I != E; ++I, ++J)
		*I = vertices_size + *J;

	vert_vec.resize(vertices_size + vertex_count);
	auto i = vert_vec.begin() + vertices_size, e = vert_vec.end();
	Fvector const* j = vertices;
	for (; i != e; ++i, ++j) {
		i->color = color;
		i->p = *j;
	}
}

void dxDebugRender::NextSceneMode()
{
//	This mode is not supported in DX10
#if	!(defined(USE_DX10) || defined(USE_DX11))
	HW.Caps.SceneMode			= (HW.Caps.SceneMode+1)%3;
#endif	//	USE_DX10
}

void dxDebugRender::ZEnable(bool bEnable)
{
	RCache.set_Z(bEnable);
}

void dxDebugRender::OnFrameEnd()
{
	RCache.OnFrameEnd();
}

void dxDebugRender::SetShader(const debug_shader &shader)
{
	RCache.set_Shader(((dxUIShader*)&*shader)->hShader);
}

void dxDebugRender::CacheSetXformWorld(const Fmatrix& M)
{
	RCache.set_xform_world(M);
}

void dxDebugRender::CacheSetCullMode(CullMode m)
{
	RCache.set_CullMode	(CULL_NONE+m);
}

void dxDebugRender::SetAmbient(u32 colour)
{
#if defined(USE_DX10) || defined(USE_DX11)
	//	TODO: DX10: Check if need this for DX10
	VERIFY(!"Not implemented for DX10");
#else	//	USE_DX10
	CHK_DX(HW.pDevice->SetRenderState (D3DRS_AMBIENT, colour));
#endif	//	USE_DX10
}

void dxDebugRender::SetDebugShader(dbgShaderHandle shdHandle)
{
	R_ASSERT(shdHandle<dbgShaderCount);

	static const LPCSTR dbgShaderParams[][2] = 
	{
		{"hud\\default", "ui\\ui_pop_up_active_back"},
	};

	if(!m_dbgShaders[shdHandle])
		m_dbgShaders[shdHandle].create(
			dbgShaderParams[shdHandle][0], dbgShaderParams[shdHandle][1]);
	
	RCache.set_Shader(m_dbgShaders[shdHandle]);
}

void dxDebugRender::DestroyDebugShader(dbgShaderHandle shdHandle)
{
	R_ASSERT(shdHandle<dbgShaderCount);
	m_dbgShaders[shdHandle].destroy();
}

void dxDebugRender::dbg_DrawTRI(Fmatrix& T, Fvector& p1, Fvector& p2, Fvector& p3, u32 C)
{
	RCache.dbg_DrawTRI(T, p1, p2, p3, C);
}

struct RDebugRender: public dxDebugRender, public pureRender
{
public:
	RDebugRender()
	{
		Device.seqRender.Add(this,REG_PRIORITY_LOW-100);
	}

	virtual	~RDebugRender()
	{
		Device.seqRender.Remove(this);
	}

	void OnRender()
	{
		Render();
	}

	virtual void add_lines(Fvector const *vertices, u32 const &vertex_count, u16 const *pairs, u32 const &pair_count, u32 const &color, bool bHud = false)
	{
		__super::add_lines(vertices, vertex_count, pairs, pair_count, color, bHud);
	}
} rdebug_render_impl;

dxDebugRender *rdebug_render = &rdebug_render_impl; 