#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"

#ifndef _EDITOR
#include "../../xrEngine/render.h"
#endif

#include "../../xrEngine/tntQAVI.h"
#include "../../xrEngine/xrTheora_Surface.h"
#include "../xrRender/gifPlayer.h"

#include "../xrRender/dxRenderDeviceRender.h"

#include "StateManager/dx10ShaderResourceStateCache.h"

#define		PRIORITY_HIGH	12
#define		PRIORITY_NORMAL	8
#define		PRIORITY_LOW	4

void resptrcode_texture::create(LPCSTR _name)
{
	PROF_EVENT("resptrcode_texture::create");
	_set(DEV->_CreateTexture(_name));
}


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CTexture::CTexture()
{
	pSurface = NULL;
	m_pSRView = NULL;
	pAVI = NULL;
	pTheora = NULL;
    gifPlayer = nullptr;
	desc_cache = 0;
	seqMSPF = 0;
	flags.MemoryUsage = 0;
	flags.bLoaded = false;
	flags.bUser = false;
	flags.seqCycles = FALSE;
	flags.bLoadedAsStaging = FALSE;
	m_material = 1.0f;
	loadState.store(LoadStateUnloaded, std::memory_order_relaxed);
	loadKind.store(0, std::memory_order_relaxed);
	bind = xr_make_delegate(this, &CTexture::apply_load);
}

CTexture::~CTexture()
{
	Unload();

	// release external reference
	DEV->_DeleteTexture(this);
}

void CTexture::surface_set(ID3DBaseTexture* surf)
{
	wait_for_loading();

	if (cName.size() && strstr(cName.c_str(), "$user$"))
		flags.bUser = true;
	if (surf) surf->AddRef();
	_RELEASE(pSurface);
	_RELEASE(m_pSRView);

	pSurface = surf;

	if (pSurface)
	{
		desc_update();

		D3D_RESOURCE_DIMENSION type;
		pSurface->GetType(&type);
		if (D3D_RESOURCE_DIMENSION_TEXTURE2D == type)
		{
			D3D_SHADER_RESOURCE_VIEW_DESC ViewDesc;

			if (desc.MiscFlags & D3D_RESOURCE_MISC_TEXTURECUBE)
			{
				ViewDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURECUBE;
				ViewDesc.TextureCube.MostDetailedMip = 0;
				ViewDesc.TextureCube.MipLevels = desc.MipLevels;
			}
			else
			{
				if (desc.SampleDesc.Count <= 1)
				{
					ViewDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
					ViewDesc.Texture2D.MostDetailedMip = 0;
					ViewDesc.Texture2D.MipLevels = desc.MipLevels;
				}
				else
				{
					ViewDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2DMS;
					ViewDesc.Texture2D.MostDetailedMip = 0;
					ViewDesc.Texture2D.MipLevels = desc.MipLevels;
				}
			}

			ViewDesc.Format = DXGI_FORMAT_UNKNOWN;

			switch (desc.Format)
			{
			case DXGI_FORMAT_R24G8_TYPELESS:
				ViewDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
				break;
			case DXGI_FORMAT_R32_TYPELESS:
				ViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
				break;
			}

			// this would be supported by DX10.1 but is not needed for stalker
			// if( ViewDesc.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS )
			if ((desc.SampleDesc.Count <= 1) || (ViewDesc.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS))
				CHK_DX(HW.pDevice->CreateShaderResourceView(pSurface, &ViewDesc, &m_pSRView));
			else
				m_pSRView = 0;
		}
		else
			CHK_DX(HW.pDevice->CreateShaderResourceView(pSurface, NULL, &m_pSRView));
	}
}

ID3DBaseTexture* CTexture::surface_get()
{
	wait_for_loading();
	if (flags.bLoadedAsStaging)
		ProcessStaging();

	if (pSurface) pSurface->AddRef();
	return pSurface;
}

void CTexture::PostLoad()
{
	if (pTheora) bind = xr_make_delegate(this, &CTexture::apply_theora);
	else if (pAVI) bind = xr_make_delegate(this, &CTexture::apply_avi);
	else if (!seqDATA.empty()) bind = xr_make_delegate(this, &CTexture::apply_seq);
	else if (gifPlayer) bind = xr_make_delegate(this, &CTexture::apply_gif);
	else bind = xr_make_delegate(this, &CTexture::apply_normal);
}

void CTexture::apply_load(u32 dwStage)
{
    if (!is_loaded()) Load();
    else PostLoad();
    if (bind == xr_make_delegate(this, &CTexture::apply_load))
    {
        // This should not happen - if bind is still apply_load, fall back to apply_normal
        // which will just apply the (potentially unloaded) surface
        apply_normal(dwStage);
    }
    else
    {
        bind(dwStage);
    }
};

void CTexture::ProcessStaging()
{
	VERIFY(pSurface);
	VERIFY(flags.bLoadedAsStaging);

	ID3DBaseTexture* pTargetSurface = 0;

	D3D_RESOURCE_DIMENSION type;
	pSurface->GetType(&type);

	switch (type)
	{
	case D3D_RESOURCE_DIMENSION_TEXTURE2D:
		{
			ID3DTexture2D* T = (ID3DTexture2D*)pSurface;
			D3D_TEXTURE2D_DESC TexDesc;
			T->GetDesc(&TexDesc);
			TexDesc.Usage = D3D_USAGE_DEFAULT;
			TexDesc.BindFlags = D3D_BIND_SHADER_RESOURCE;
			TexDesc.CPUAccessFlags = 0;

			T = 0;

			CHK_DX(HW.pDevice->CreateTexture2D( &TexDesc, // Texture desc
				NULL, // Initial data
				&T )); // [out] Texture

			pTargetSurface = T;
		}
		break;
	case D3D_RESOURCE_DIMENSION_TEXTURE3D:
		{
			ID3DTexture3D* T = (ID3DTexture3D*)pSurface;
			D3D_TEXTURE3D_DESC TexDesc;
			T->GetDesc(&TexDesc);
			TexDesc.Usage = D3D_USAGE_DEFAULT;
			TexDesc.BindFlags = D3D_BIND_SHADER_RESOURCE;
			TexDesc.CPUAccessFlags = 0;

			T = 0;

			CHK_DX(HW.pDevice->CreateTexture3D( &TexDesc, // Texture desc
				NULL, // Initial data
				&T )); // [out] Texture

			pTargetSurface = T;
		}
		break;
	default:
		VERIFY(!"CTexture::ProcessStaging unsupported dimensions.");
	}

	HW.pContext->CopyResource(pTargetSurface, pSurface);
	/*
	for( int i=0; i<iNumSubresources; ++i)
	{
		HW.pDevice->CopySubresourceRegion(
			pTargetSurface,
			i,
			0,
			0,
			0,
			pSurface,
			i,
			0
			);
	}
	*/


	flags.bLoadedAsStaging = FALSE;

	//	Check if texture was not copied _before_ it was converted.
	ULONG RefCnt = pSurface->Release();
	pSurface = 0;

	VERIFY(!RefCnt);

	surface_set(pTargetSurface);

	_RELEASE(pTargetSurface);
}

void CTexture::Apply(u32 dwStage)
{
	wait_for_loading();
	dwLastUsedFrame = RDEVICE.dwFrame;

	if (flags.bLoadedAsStaging)
		ProcessStaging();

	//if( !RImplementation.o.dx10_msaa )
	//   VERIFY( !((!pSurface)^(!m_pSRView)) );	//	Both present or both missing
	//else
	//{
	//if( ((!pSurface)^(!m_pSRView)) )
	//   return;
	//}

	if (dwStage < rstVertex) //	Pixel shader stage resources
	{
		//HW.pDevice->PSSetShaderResources(dwStage, 1, &m_pSRView);
		SRVSManager.SetPSResource(dwStage, m_pSRView);
	}
	else if (dwStage < rstGeometry) //	Vertex shader stage resources
	{
		//HW.pDevice->VSSetShaderResources(dwStage-rstVertex, 1, &m_pSRView);
		SRVSManager.SetVSResource(dwStage - rstVertex, m_pSRView);
	}
	else if (dwStage < rstHull) //	Geometry shader stage resources
	{
		//HW.pDevice->GSSetShaderResources(dwStage-rstGeometry, 1, &m_pSRView);
		SRVSManager.SetGSResource(dwStage - rstGeometry, m_pSRView);
	}
#ifdef USE_DX11
	else if (dwStage < rstDomain) //	Geometry shader stage resources
	{
		SRVSManager.SetHSResource(dwStage - rstHull, m_pSRView);
	}
	else if (dwStage < rstCompute) //	Geometry shader stage resources
	{
		SRVSManager.SetDSResource(dwStage - rstDomain, m_pSRView);
	}
	else if (dwStage < rstInvalid) //	Geometry shader stage resources
	{
		SRVSManager.SetCSResource(dwStage - rstCompute, m_pSRView);
	}
#endif
	else
		VERIFY("Invalid stage");
}

void CTexture::apply_theora(u32 dwStage)
{
	wait_for_loading();
	if (pTheora->Update(m_play_time != 0xFFFFFFFF ? m_play_time : Device.dwTimeContinual))
	{
		D3D_RESOURCE_DIMENSION type;
		pSurface->GetType(&type);
		R_ASSERT(D3D_RESOURCE_DIMENSION_TEXTURE2D == type);
		ID3DTexture2D* T2D = (ID3DTexture2D*)pSurface;
		D3D_MAPPED_TEXTURE2D mapData;
		RECT rect;
		rect.left = 0;
		rect.top = 0;
		rect.right = pTheora->Width(true);
		rect.bottom = pTheora->Height(true);

		u32 _w = pTheora->Width(false);

		//R_CHK				(T2D->LockRect(0,&R,&rect,0));
#ifdef USE_DX11
		R_CHK(HW.pContext->Map(T2D, 0, D3D_MAP_WRITE_DISCARD, 0, &mapData));
#else
		R_CHK(T2D->Map(0,D3D_MAP_WRITE_DISCARD,0,&mapData));
#endif
		//R_ASSERT			(R.Pitch == int(pTheora->Width(false)*4));
		R_ASSERT(mapData.RowPitch == int(pTheora->Width(false)*4));
		int _pos = 0;
		pTheora->DecompressFrame((u32*)mapData.pData, _w - rect.right, _pos);
		VERIFY(u32(_pos) == rect.bottom*_w);
		//R_CHK				(T2D->UnlockRect(0));
#ifdef USE_DX11
		HW.pContext->Unmap(T2D, 0);
#else
		T2D->Unmap(0);
#endif
	}
	Apply(dwStage);
	//CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
};

void CTexture::apply_avi(u32 dwStage)
{
	wait_for_loading();
	if (pAVI->NeedUpdate())
	{
		D3D_RESOURCE_DIMENSION type;
		pSurface->GetType(&type);
		R_ASSERT(D3D_RESOURCE_DIMENSION_TEXTURE2D == type);
		ID3DTexture2D* T2D = (ID3DTexture2D*)pSurface;
		D3D_MAPPED_TEXTURE2D mapData;

		// AVI
		//R_CHK	(T2D->LockRect(0,&R,NULL,0));
#ifdef USE_DX11
		R_CHK(HW.pContext->Map(T2D, 0, D3D_MAP_WRITE_DISCARD, 0, &mapData));
#else
		R_CHK(T2D->Map(0,D3D_MAP_WRITE_DISCARD,0,&mapData));
#endif
		R_ASSERT(mapData.RowPitch == int(pAVI->m_dwWidth*4));
		BYTE* ptr;
		pAVI->GetFrame(&ptr);
		CopyMemory(mapData.pData, ptr, pAVI->m_dwWidth*pAVI->m_dwHeight*4);
		//R_CHK	(T2D->UnlockRect(0));
#ifdef USE_DX11
		HW.pContext->Unmap(T2D, 0);
#else
		T2D->Unmap(0);
#endif
	}
	//CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
	Apply(dwStage);
};

void CTexture::apply_seq(u32 dwStage)
{
	wait_for_loading();
	// SEQ
	u32 frame = Device.dwTimeContinual / seqMSPF; //Device.dwTimeGlobal
	u32 frame_data = seqDATA.size();
	if (flags.seqCycles)
	{
		u32 frame_id = frame % (frame_data * 2);
		if (frame_id >= frame_data) frame_id = (frame_data - 1) - (frame_id % frame_data);
		pSurface = seqDATA[frame_id];
		m_pSRView = m_seqSRView[frame_id];
	}
	else
	{
		u32 frame_id = frame % frame_data;
		pSurface = seqDATA[frame_id];
		m_pSRView = m_seqSRView[frame_id];
	}
	//CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
	Apply(dwStage);
};

void CTexture::apply_gif(u32 dwStage)
{
	wait_for_loading();
	if (gifPlayer->UpdateFrame())
	{
        const CGIFAnimationPlayer::Frame* const gifFrame = gifPlayer->GetActiveFrame();
        R_ASSERT(gifFrame);

        pSurface = gifFrame->surface;
        m_pSRView = gifFrame->srv;
	}
	Apply(dwStage);
}

void CTexture::apply_normal(u32 dwStage)
{
	wait_for_loading();
	//CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
	Apply(dwStage);
};

void CTexture::Preload()
{
	const shared_str& name = m_loadName.size() ? m_loadName : cName;
	if (!Core.ParamsData.test(ECoreParams::r4_dev))
	{
	m_bumpmap = DEV->m_textures_description.GetBumpName(name);
	}
	m_material = DEV->m_textures_description.GetMaterial(name);
}

void CTexture::SetLoadSource(LPCSTR logical_name, LPCSTR resolved_path, ELoadKind kind)
{
	m_loadName = logical_name;
	m_resolvedSourcePath = resolved_path;
	loadKind.store(kind, std::memory_order_release);
}

bool CTexture::TryQueueLoad()
{
	u32 expected = LoadStateUnloaded;
	return loadState.compare_exchange_strong(expected, LoadStateQueued, std::memory_order_acq_rel,
		std::memory_order_acquire);
}

void CTexture::CancelQueuedLoad()
{
	u32 expected = LoadStateQueued;
	loadState.compare_exchange_strong(expected, LoadStateUnloaded, std::memory_order_acq_rel,
		std::memory_order_acquire);
}

bool CTexture::CanLoadAsync() const
{
	u32 kind = loadKind.load(std::memory_order_acquire);
	if (!kind)
	{
		const shared_str& name = m_loadName.size() ? m_loadName : cName;
		string_path path;
		if (FS.exist(path, "$game_textures$", name.c_str(), ".ogm"))
			kind = LoadKindOgm;
		else if (FS.exist(path, "$game_textures$", name.c_str(), ".avi"))
			kind = LoadKindAvi;
		else if (FS.exist(path, "$game_textures$", name.c_str(), ".seq"))
			kind = LoadKindSequence;
		else if (FS.exist(path, "$game_textures$", name.c_str(), ".gif"))
			kind = LoadKindGif;
		else
			kind = LoadKindDds;
		loadKind.store(kind, std::memory_order_release);
	}
	return kind == LoadKindDds;
}

bool CTexture::is_loaded() const
{
	return loadState.load(std::memory_order_acquire) == LoadStateLoaded;
}

void CTexture::wait_for_loading() const
{
	for (;;)
	{
		const u32 state = loadState.load(std::memory_order_acquire);
		if (state != LoadStateQueued && state != LoadStateLoading && state != LoadStateUnloading)
			return;
		if (state == LoadStateQueued && DEV && DEV->IsTextureOwnerThread())
		{
			const_cast<CTexture*>(this)->Load();
			continue;
		}
		SwitchToThread();
	}
}

bool CTexture::BeginLoad(bool queued)
{
	for (;;)
	{
		u32 expected = queued ? LoadStateQueued : LoadStateUnloaded;
		if (loadState.compare_exchange_strong(expected, LoadStateLoading, std::memory_order_acq_rel,
			std::memory_order_acquire))
		{
			return true;
		}
		if (!queued && expected == LoadStateQueued)
		{
			expected = LoadStateQueued;
			if (loadState.compare_exchange_strong(expected, LoadStateLoading, std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				return true;
			}
		}

		if (expected == LoadStateLoaded || expected == LoadStateFailed || (queued && expected == LoadStateUnloaded))
			return false;

		wait_for_loading();
	}
}

void CTexture::FinishLoad()
{
	flags.bLoaded = true;
	loadState.store(LoadStateLoaded, std::memory_order_release);
}

void CTexture::FailLoad()
{
	loadState.store(LoadStateUnloading, std::memory_order_release);
	ReleaseLoadedData();
	loadState.store(LoadStateFailed, std::memory_order_release);
}

void CTexture::Load()
{
	Load(false);
}

void CTexture::LoadQueued()
{
	Load(true);
}

void CTexture::Load(bool queued)
{
	PROF_EVENT("CTexture::Load");
	if (!BeginLoad(queued))
		return;
	try
	{

	flags.bLoaded = false;
	desc_cache = 0;
	const shared_str& name = m_loadName.size() ? m_loadName : cName;
	if (pSurface)
	{
		FinishLoad();
		return;
	}

	flags.bUser = false;
	flags.MemoryUsage = 0;
	if (0 == stricmp(name.c_str(), "$null"))
	{
		FinishLoad();
		return;
	}
	if (0 != strstr(name.c_str(), "$user$"))
	{
		flags.bUser = true;
		FinishLoad();
		return;
	}

	Preload();

	bool bCreateView = true;
	const u32 kind = loadKind.load(std::memory_order_acquire);
	const LPCSTR resolvedSource = m_resolvedSourcePath.size() ? m_resolvedSourcePath.c_str() : nullptr;

	// Check for OGM
	string_path fn;
	if (kind == LoadKindOgm || (kind == LoadKindUnknown && FS.exist(fn, "$game_textures$", name.c_str(), ".ogm")))
	{
		if (kind == LoadKindOgm)
			xr_strcpy(fn, resolvedSource);
		// AVI
		pTheora = xr_new<CTheoraSurface>();
		m_play_time = 0xFFFFFFFF;

		if (!pTheora->Load(fn))
		{
			xr_delete(pTheora);
			FATAL("Can't open video stream");
		}
		else
		{
			flags.MemoryUsage = pTheora->Width(true) * pTheora->Height(true) * 4;
			pTheora->Play(TRUE, Device.dwTimeContinual);

			// Now create texture
			ID3DTexture2D* pTexture = 0;
			u32 _w = pTheora->Width(false);
			u32 _h = pTheora->Height(false);

			//			HRESULT hrr = HW.pDevice->CreateTexture(
			//				_w, _h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, NULL );
			D3D_TEXTURE2D_DESC desc;
			desc.Width = _w;
			desc.Height = _h;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = D3D_USAGE_DYNAMIC;
			desc.BindFlags = D3D_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;
			HRESULT hrr = HW.pDevice->CreateTexture2D(&desc, 0, &pTexture);

			pSurface = pTexture;
			if (FAILED(hrr))
			{
				FATAL("Invalid video stream");
				R_CHK(hrr);
				xr_delete(pTheora);
				pSurface = 0;
				m_pSRView = 0;
			}
			else
			{
				CHK_DX(HW.pDevice->CreateShaderResourceView(pSurface, 0, &m_pSRView));
			}
		}
	}
	else if (kind == LoadKindAvi || (kind == LoadKindUnknown && FS.exist(fn, "$game_textures$", name.c_str(), ".avi")))
	{
		if (kind == LoadKindAvi)
			xr_strcpy(fn, resolvedSource);
		// AVI
		pAVI = xr_new<CAviPlayerCustom>();

		if (!pAVI->Load(fn))
		{
			xr_delete(pAVI);
			FATAL("Can't open video stream");
		}
		else
		{
			flags.MemoryUsage = pAVI->m_dwWidth * pAVI->m_dwHeight * 4;

			// Now create texture
			ID3DTexture2D* pTexture = 0;
			//HRESULT hrr = HW.pDevice->CreateTexture(
			//pAVI->m_dwWidth,pAVI->m_dwHeight,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,
			//	&pTexture,NULL
			//	);
			D3D_TEXTURE2D_DESC desc;
			desc.Width = pAVI->m_dwWidth;
			desc.Height = pAVI->m_dwHeight;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = D3D_USAGE_DYNAMIC;
			desc.BindFlags = D3D_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;
			HRESULT hrr = HW.pDevice->CreateTexture2D(&desc, 0, &pTexture);

			pSurface = pTexture;
			if (FAILED(hrr))
			{
				FATAL("Invalid video stream");
				R_CHK(hrr);
				xr_delete(pAVI);
				pSurface = 0;
				m_pSRView = 0;
			}
			else
			{
				CHK_DX(HW.pDevice->CreateShaderResourceView(pSurface, 0, &m_pSRView));
			}
		}
	}
	else if (kind == LoadKindSequence || (kind == LoadKindUnknown && FS.exist(fn, "$game_textures$", name.c_str(), ".seq")))
	{
		if (kind == LoadKindSequence)
			xr_strcpy(fn, resolvedSource);
		// Sequence
		string256 buffer;
		IReader* _fs = FS.r_open(fn);

		flags.seqCycles = FALSE;
		_fs->r_string(buffer, sizeof(buffer));
		if (0 == stricmp(buffer, "cycled"))
		{
			flags.seqCycles = TRUE;
			_fs->r_string(buffer, sizeof(buffer));
		}
		u32 fps = atoi(buffer);
		seqMSPF = 1000 / fps;

		while (!_fs->eof())
		{
			_fs->r_string(buffer, sizeof(buffer));
			_Trim(buffer);
			if (buffer[0])
			{
				// Load another texture
				u32 mem = 0;
				pSurface = ::RImplementation.texture_load(buffer, mem);
				if (pSurface)
				{
					// pSurface->SetPriority	(PRIORITY_LOW);
					seqDATA.push_back(pSurface);
					m_seqSRView.push_back(0);
					HW.pDevice->CreateShaderResourceView(seqDATA.back(), NULL, &m_seqSRView.back());
					flags.MemoryUsage += mem;
				}
			}
		}
		pSurface = 0;
		FS.r_close(_fs);
	}
    else if (kind == LoadKindGif || (kind == LoadKindUnknown && FS.exist(fn, "$game_textures$", name.c_str(), ".gif")))
    {
		if (kind == LoadKindGif)
			xr_strcpy(fn, resolvedSource);
        gifPlayer = xr_new<CGIFAnimationPlayer>();
        if (!gifPlayer->Load(fn))
        {
            xr_delete(gifPlayer);
            pSurface = nullptr;
            m_pSRView = nullptr;
        }
        else
        {
            flags.MemoryUsage = gifPlayer->GetUsedMemory();

            gifPlayer->Play();

            const CGIFAnimationPlayer::Frame* const gifFrame = gifPlayer->GetActiveFrame();
            pSurface = gifFrame->surface;
            m_pSRView = gifFrame->srv;
        }
    }
	else
	{
		// Normal texture
		u32 mem = 0;
		pSurface = ::RImplementation.texture_load(name.c_str(), mem, false,
			kind == LoadKindDds ? resolvedSource : nullptr);

		if (GetUsage() == D3D_USAGE_STAGING)
		{
			flags.bLoadedAsStaging = TRUE;
			bCreateView = false;
		}

		// Calc memory usage and preload into vid-mem
		if (pSurface)
		{
			// pSurface->SetPriority	(PRIORITY_NORMAL);
			flags.MemoryUsage = mem;
		}
		
		if (pSurface && bCreateView)
			CHK_DX(HW.pDevice->CreateShaderResourceView(pSurface, NULL, &m_pSRView));
	}
	PostLoad();
	FinishLoad();
	}
	catch (...)
	{
		FailLoad();
		throw;
	}
}

void CTexture::Unload()
{
	for (;;)
	{
		u32 state = loadState.load(std::memory_order_acquire);
		if (state == LoadStateUnloaded || state == LoadStateFailed)
			return;
		if (state == LoadStateQueued || state == LoadStateLoading || state == LoadStateUnloading)
		{
			wait_for_loading();
			continue;
		}
		if (loadState.compare_exchange_strong(state, LoadStateUnloading, std::memory_order_acq_rel,
			std::memory_order_acquire))
		{
			break;
		}
	}
	ReleaseLoadedData();
	loadState.store(LoadStateUnloaded, std::memory_order_release);
}

void CTexture::ReleaseLoadedData()
{
#ifdef DEBUG
	string_path				msg_buff;
	xr_sprintf				(msg_buff,sizeof(msg_buff),"* Unloading texture [%s] pSurface RefCount=",cName.c_str());
#endif // DEBUG

	//.	if (flags.bLoaded)		Msg		("* Unloaded: %s",cName.c_str());

	flags.bLoaded = FALSE;
	flags.bLoadedAsStaging = FALSE;
	if (!seqDATA.empty())
	{
		for (u32 I = 0; I < seqDATA.size(); I++)
		{
			_RELEASE(seqDATA[I]);
			_RELEASE(m_seqSRView[I]);
		}
		seqDATA.clear();
		m_seqSRView.clear();
		pSurface = 0;
		m_pSRView = 0;
	}

    if (gifPlayer)
    {
        xr_delete(gifPlayer);
        pSurface = nullptr;
        m_pSRView = nullptr;
    }

#ifdef DEBUG
	_SHOW_REF		(msg_buff, pSurface);
#endif // DEBUG
	_RELEASE(pSurface);
	_RELEASE(m_pSRView);

	xr_delete(pAVI);
	xr_delete(pTheora);

	bind = xr_make_delegate(this, &CTexture::apply_load);
}

void CTexture::desc_update()
{
	wait_for_loading();
	desc_cache = pSurface;
	if (pSurface)
	{
		D3D_RESOURCE_DIMENSION type;
		pSurface->GetType(&type);
		if (D3D_RESOURCE_DIMENSION_TEXTURE2D == type)
		{
			ID3DTexture2D* T = (ID3DTexture2D*)pSurface;
			T->GetDesc(&desc);
		}
	}
}

D3D_USAGE CTexture::GetUsage()
{
	D3D_USAGE res = D3D_USAGE_DEFAULT;

	if (pSurface)
	{
		D3D_RESOURCE_DIMENSION type;
		pSurface->GetType(&type);
		switch (type)
		{
		case D3D_RESOURCE_DIMENSION_TEXTURE1D:
			{
				ID3DTexture1D* T = (ID3DTexture1D*)pSurface;
				D3D_TEXTURE1D_DESC descr;
				T->GetDesc(&descr);
				res = descr.Usage;
			}
			break;

		case D3D_RESOURCE_DIMENSION_TEXTURE2D:
			{
				ID3DTexture2D* T = (ID3DTexture2D*)pSurface;
				D3D_TEXTURE2D_DESC descr;
				T->GetDesc(&descr);
				res = descr.Usage;
			}
			break;

		case D3D_RESOURCE_DIMENSION_TEXTURE3D:
			{
				ID3DTexture3D* T = (ID3DTexture3D*)pSurface;
				D3D_TEXTURE3D_DESC descr;
				T->GetDesc(&descr);
				res = descr.Usage;
			}
			break;

		default:
			VERIFY(!"Unknown texture format???");
		}
	}

	return res;
}

void CTexture::video_Play(BOOL looped, u32 _time)
{
	wait_for_loading();
	if (pTheora) pTheora->Play(looped, (_time != 0xFFFFFFFF) ? (m_play_time = _time) : Device.dwTimeContinual);
}

void CTexture::video_Pause(BOOL state)
{
	wait_for_loading();
	if (pTheora) pTheora->Pause(state);
}

void CTexture::video_Stop()
{
	wait_for_loading();
	if (pTheora) pTheora->Stop();
}

BOOL CTexture::video_IsPlaying()
{
	wait_for_loading();
	return (pTheora) ? pTheora->IsPlaying() : FALSE;
}
