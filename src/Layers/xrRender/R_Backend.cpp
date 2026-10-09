#include "stdafx.h"
#pragma hdrstop

#include "../../xrEngine/IRenderable.h"

#if defined(USE_DX10) || defined(USE_DX11)
#include "../xrRenderDX10/dx10BufferUtils.h"
#endif	//	USE_DX11

CBackend RCache;

// Create Quad-IB
#if defined(USE_DX10) || defined(USE_DX11)

void CBackend::RestoreQuadIBData()
{
}

void CBackend::CreateQuadIB()
{
	static const u32 dwTriCount = 4 * 1024;
	static const u32 dwIdxCount = dwTriCount * 2 * 3;
	u16 IndexBuffer[dwIdxCount];
	u16* Indices = IndexBuffer;

	D3D_BUFFER_DESC desc;
	desc.ByteWidth = dwIdxCount * 2;

	desc.Usage = D3D_USAGE_DEFAULT;
	desc.BindFlags = D3D_BIND_INDEX_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	D3D_SUBRESOURCE_DATA subData;
	subData.pSysMem = IndexBuffer;

	{
		int Cnt = 0;
		int ICnt = 0;
		for (int i = 0; i < dwTriCount; i++)
		{
			Indices[ICnt++] = u16(Cnt + 0);
			Indices[ICnt++] = u16(Cnt + 1);
			Indices[ICnt++] = u16(Cnt + 2);

			Indices[ICnt++] = u16(Cnt + 3);
			Indices[ICnt++] = u16(Cnt + 2);
			Indices[ICnt++] = u16(Cnt + 1);

			Cnt += 4;
		}
	}

	R_CHK(HW.pDevice->CreateBuffer ( &desc, &subData, &QuadIB));
	HW.stats_manager.increment_stats_ib(QuadIB);
}

#else	//	USE_DX11

void CBackend::RestoreQuadIBData()
{
	const u32 dwTriCount = 4 * 1024;
	u16* Indices = 0;
	R_CHK(QuadIB->Lock(0,0,(void**)&Indices,0));
	{
		int Cnt = 0;
		int ICnt = 0;
		for (int i = 0; i < dwTriCount; i++)
		{
			Indices[ICnt++] = u16(Cnt + 0);
			Indices[ICnt++] = u16(Cnt + 1);
			Indices[ICnt++] = u16(Cnt + 2);

			Indices[ICnt++] = u16(Cnt + 3);
			Indices[ICnt++] = u16(Cnt + 2);
			Indices[ICnt++] = u16(Cnt + 1);

			Cnt += 4;
		}
	}
	R_CHK(QuadIB->Unlock());
}

void CBackend::CreateQuadIB()
{
	const u32 dwTriCount = 4 * 1024;
	const u32 dwIdxCount = dwTriCount * 2 * 3;
	u16* Indices = 0;
	u32 dwUsage = D3DUSAGE_WRITEONLY;
	if (HW.Caps.geometry.bSoftware) dwUsage |= D3DUSAGE_SOFTWAREPROCESSING;
	R_CHK(HW.pDevice->CreateIndexBuffer (dwIdxCount*2,dwUsage,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&QuadIB,NULL));
	HW.stats_manager.increment_stats_ib(QuadIB);

	R_CHK(QuadIB->Lock(0,0,(void**)&Indices,0));
	{
		int Cnt = 0;
		int ICnt = 0;
		for (int i = 0; i < dwTriCount; i++)
		{
			Indices[ICnt++] = u16(Cnt + 0);
			Indices[ICnt++] = u16(Cnt + 1);
			Indices[ICnt++] = u16(Cnt + 2);

			Indices[ICnt++] = u16(Cnt + 3);
			Indices[ICnt++] = u16(Cnt + 2);
			Indices[ICnt++] = u16(Cnt + 1);

			Cnt += 4;
		}
	}
	R_CHK(QuadIB->Unlock());
}

#endif	//	USE_DX11

// Device dependance
void CBackend::OnDeviceCreate()
{
	CreateQuadIB();

	// streams
	Vertex.Create();
	Index.Create();

	InitDebugDraw();

	// invalidate caching
	Invalidate();
}

void CBackend::OnDeviceDestroy()
{
	// streams
	Index.Destroy();
	Vertex.Destroy();

	DestroyDebugDraw();

	// Quad
	HW.stats_manager.decrement_stats_ib(QuadIB);
	_RELEASE(QuadIB);
}

void R_bus_object::map(R_constant* C, ShaderBus::lane* l)
{
	RCache.set_c(C, l->bound.x, l->bound.y, l->bound.z, l->bound.w);
	if (count < ShaderBus::max_object_lanes)
	{
		c[count] = C;
		lanes[count++] = l;
	}
}

void R_bus_object::write_object(IRenderable* O)
{
	const ShaderBus::object_values* block = O ? O->renderable.bus_values : nullptr;
	for (u32 i = 0; i < count; ++i)
	{
		const Fvector4& v = ShaderBus::object_bound(lanes[i], block);
		RCache.set_c(c[i], v.x, v.y, v.z, v.w);
	}
	object_applied = true;
}

void R_bus_object::write_defaults()
{
	// the material step right after an object draw keeps that object's values
	if (object_applied)
	{
		object_applied = false;
		return;
	}

	for (u32 i = 0; i < count; ++i)
		RCache.set_c(c[i], lanes[i]->bound.x, lanes[i]->bound.y, lanes[i]->bound.z, lanes[i]->bound.w);
}
