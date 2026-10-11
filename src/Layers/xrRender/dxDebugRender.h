#pragma once

#include "../../Include/xrRender/DebugRender.h"

class dxDebugRender : public IDebugRender
{
public:
	dxDebugRender() = default;

	virtual void Render();
	virtual void add_lines(Fvector const *vertices, u32 const &vertex_count, u16 const *pairs, u32 const &pair_count, u32 const &color, bool bHud = false);
	virtual void add_lines_ex(Fvector const *vertices, u32 const &vertex_count, u16 const *pairs, u32 const &pair_count, u32 const &color, bool bHud, float width, bool depth_test);

	// routed to RCache
	virtual void NextSceneMode();
	virtual void ZEnable(bool bEnable);
	virtual void OnFrameEnd();
	virtual void SetShader(const debug_shader &shader);
	virtual void CacheSetXformWorld(const Fmatrix& M);
	virtual void CacheSetCullMode(CullMode);
	virtual void SetAmbient(u32 colour);

	// Shaders
	virtual void SetDebugShader	(dbgShaderHandle shdHandle);
	virtual void DestroyDebugShader(dbgShaderHandle shdHandle);
	virtual void dbg_DrawTRI(Fmatrix& T, Fvector& p1, Fvector& p2, Fvector& p3, u32 C);

private:
	xr_unordered_map<u32, std::vector<FVF::L>> m_line_vertices;
	xr_unordered_map<u32, std::vector<u16>> m_line_indices;
	xr_unordered_map<u32, std::vector<FVF::L>> m_line_vertices_hud;
	xr_unordered_map<u32, std::vector<u16>> m_line_indices_hud;

	ref_shader m_dbgShaders[dbgShaderCount];

	// the key is the colour with the depth test flag in the low bit
	xr_unordered_map<u64, std::vector<FVF::L>> m_ex_line_vertices;
	xr_unordered_map<u64, std::vector<u16>> m_ex_line_indices;
	xr_unordered_map<u64, std::vector<FVF::L>> m_ex_line_vertices_hud;
	xr_unordered_map<u64, std::vector<u16>> m_ex_line_indices_hud;
	xr_unordered_map<u64, std::vector<FVF::L>> m_ex_tri_vertices;
	xr_unordered_map<u64, std::vector<u16>> m_ex_tri_indices;
	xr_unordered_map<u64, std::vector<FVF::L>> m_ex_tri_vertices_hud;
	xr_unordered_map<u64, std::vector<u16>> m_ex_tri_indices_hud;

	void render_ex();
	void flush_ex(xr_unordered_map<u64, std::vector<FVF::L>>& verts, xr_unordered_map<u64, std::vector<u16>>& inds, u32 prim_type);
};

extern dxDebugRender DebugRenderImpl;
extern dxDebugRender* rdebug_render;
