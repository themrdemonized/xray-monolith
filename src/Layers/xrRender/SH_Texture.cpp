#include "stdafx.h"
#pragma hdrstop

#include "ResourceManager.h"

#ifndef _EDITOR
#include "../../xrEngine/render.h"
#endif

#include "../../xrEngine/tntQAVI.h"
#include "../../xrEngine/xrTheora_Surface.h"
#include "gifPlayer.h"

#include "dxRenderDeviceRender.h"

#define		PRIORITY_HIGH	12
#define		PRIORITY_NORMAL	8
#define		PRIORITY_LOW	4


void resptrcode_texture::create(LPCSTR _name)
{
	_set(DEV->_CreateTexture(_name));
}


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CTexture::CTexture()
{
	pSurface = NULL;
	pAVI = NULL;
	pTheora = NULL;
    gifPlayer = nullptr;
	desc_cache = 0;
	seqMSPF = 0;
	flags.MemoryUsage = 0;
	flags.bLoaded = false;
	flags.bUser = false;
	flags.seqCycles = FALSE;
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
	if (surf) surf->AddRef();

	_RELEASE(pSurface);

	pSurface = surf;
}

ID3DBaseTexture* CTexture::surface_get()
{
	wait_for_loading();
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


void CTexture::apply_theora(u32 dwStage)
{
	wait_for_loading();
	if (pTheora->Update(m_play_time != 0xFFFFFFFF ? m_play_time : RDEVICE.dwTimeContinual))
	{
		R_ASSERT(D3DRTYPE_TEXTURE == pSurface->GetType());
		ID3DTexture2D* T2D = (ID3DTexture2D*)pSurface;
		D3DLOCKED_RECT R;
		RECT rect;
		rect.left = 0;
		rect.top = 0;
		rect.right = pTheora->Width(true);
		rect.bottom = pTheora->Height(true);

		u32 _w = pTheora->Width(false);

		R_CHK(T2D->LockRect(0,&R,&rect,0));
		R_ASSERT(R.Pitch == int(pTheora->Width(false)*4));
		int _pos = 0;
		pTheora->DecompressFrame((u32*)R.pBits, _w - rect.right, _pos);
		VERIFY(u32(_pos) == rect.bottom*_w);
		R_CHK(T2D->UnlockRect(0));
	}
	CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
};

void CTexture::apply_avi(u32 dwStage)
{
	wait_for_loading();
	if (pAVI->NeedUpdate())
	{
		R_ASSERT(D3DRTYPE_TEXTURE == pSurface->GetType());
		ID3DTexture2D* T2D = (ID3DTexture2D*)pSurface;

		// AVI
		D3DLOCKED_RECT R;
		R_CHK(T2D->LockRect(0,&R,NULL,0));
		R_ASSERT(R.Pitch == int(pAVI->m_dwWidth*4));
		//		R_ASSERT(pAVI->DecompressFrame((u32*)(R.pBits)));
		BYTE* ptr;
		pAVI->GetFrame(&ptr);
		CopyMemory(R.pBits, ptr, pAVI->m_dwWidth*pAVI->m_dwHeight*4);
		//		R_ASSERT(pAVI->GetFrame((BYTE*)(&R.pBits)));

		R_CHK(T2D->UnlockRect(0));
	}
	CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
};

void CTexture::apply_seq(u32 dwStage)
{
	wait_for_loading();
	// SEQ
	u32 frame = RDEVICE.dwTimeContinual / seqMSPF; //RDEVICE.dwTimeGlobal
	u32 frame_data = seqDATA.size();
	if (flags.seqCycles)
	{
		u32 frame_id = frame % (frame_data * 2);
		if (frame_id >= frame_data) frame_id = (frame_data - 1) - (frame_id % frame_data);
		pSurface = seqDATA[frame_id];
	}
	else
	{
		u32 frame_id = frame % frame_data;
		pSurface = seqDATA[frame_id];
	}
	CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
};

void CTexture::apply_gif(u32 dwStage)
{
    wait_for_loading();
    if (gifPlayer->UpdateFrame())
    {
        const CGIFAnimationPlayer::Frame* const gifFrame = gifPlayer->GetActiveFrame();
        R_ASSERT(gifFrame);

        pSurface = gifFrame->surface;
    }
    CHK_DX(HW.pDevice->SetTexture(dwStage, pSurface));
}

void CTexture::apply_normal(u32 dwStage)
{
	wait_for_loading();
	dwLastUsedFrame = Device.dwFrame;
	CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
};

void CTexture::Preload()
{
	m_bumpmap = DEV->m_textures_description.GetBumpName(cName);
	m_material = DEV->m_textures_description.GetMaterial(cName);
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
		string_path path;
		kind = FS.exist(path, "$game_textures$", *cName, ".ogm") ||
			FS.exist(path, "$game_textures$", *cName, ".avi") ||
			FS.exist(path, "$game_textures$", *cName, ".seq") ||
			FS.exist(path, "$game_textures$", *cName, ".gif") ? 2u : 1u;
		loadKind.store(kind, std::memory_order_release);
	}
	return kind == 1;
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
	if (pSurface)
	{
		FinishLoad();
		return;
	}

	flags.bUser = false;
	flags.MemoryUsage = 0;
	if (0==_stricmp(*cName,"$null"))
	{
		FinishLoad();
		return;
	}
	if (0!=strstr(*cName,"$user$"))	
	{
		flags.bUser	= true;
		FinishLoad();
		return;
	}

	Preload();
	//#ifndef		DEDICATED_SERVER
#ifndef _EDITOR
	if (!g_dedicated_server)
#endif
	{
		// Check for OGM
		string_path fn;
		if (FS.exist(fn, "$game_textures$", *cName, ".ogm"))
		{
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
				BOOL bstop_at_end = (0 != strstr(cName.c_str(), "intro\\")) || (0 != strstr(cName.c_str(), "outro\\"));
				pTheora->Play(!bstop_at_end, RDEVICE.dwTimeContinual);

				// Now create texture
				ID3DTexture2D* pTexture = 0;
				u32 _w = pTheora->Width(false);
				u32 _h = pTheora->Height(false);

				HRESULT hrr = HW.pDevice->CreateTexture(
					_w, _h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, NULL);

				pSurface = pTexture;
				if (FAILED(hrr))
				{
					FATAL("Invalid video stream");
					R_CHK(hrr);
					xr_delete(pTheora);
					pSurface = 0;
				}
			}
		}
		else if (FS.exist(fn, "$game_textures$", *cName, ".avi"))
		{
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
				HRESULT hrr = HW.pDevice->CreateTexture(
					pAVI->m_dwWidth, pAVI->m_dwHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
					&pTexture,NULL
				);
				pSurface = pTexture;
				if (FAILED(hrr))
				{
					FATAL("Invalid video stream");
					R_CHK(hrr);
					xr_delete(pAVI);
					pSurface = 0;
				}
			}
		}
		else if (FS.exist(fn, "$game_textures$", *cName, ".seq"))
		{
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
						flags.MemoryUsage += mem;
					}
				}
			}
			pSurface = 0;
			FS.r_close(_fs);
		}
        else if (FS.exist(fn, "$game_textures$", *cName, ".gif"))
        {
            gifPlayer = xr_new<CGIFAnimationPlayer>();
            if (!gifPlayer->Load(fn))
            {
                xr_delete(gifPlayer);
                pSurface = nullptr;
            }
            else
            {
                flags.MemoryUsage = gifPlayer->GetUsedMemory();

                gifPlayer->Play();

                const CGIFAnimationPlayer::Frame* const gifFrame = gifPlayer->GetActiveFrame();
                pSurface = gifFrame->surface;
            }
        }
		else
		{
			// Normal texture
			u32 mem = 0;
			pSurface = ::RImplementation.texture_load(*cName, mem);

			// Calc memory usage and preload into vid-mem
			if (pSurface)
			{
				// pSurface->SetPriority	(PRIORITY_NORMAL);
				flags.MemoryUsage = mem;
			}
		}
		//#endif
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
	if (!seqDATA.empty())
	{
		for (u32 I = 0; I < seqDATA.size(); I++)
		{
			_RELEASE(seqDATA[I]);
		}
		seqDATA.clear();
		pSurface = 0;
	}
	flags.MemoryUsage = 0;

    if (gifPlayer)
    {
        xr_delete(gifPlayer);
        pSurface = nullptr;
    }

#ifdef DEBUG
	_SHOW_REF		(msg_buff, pSurface);
#endif // DEBUG

	_RELEASE(pSurface);

	xr_delete(pAVI);
	xr_delete(pTheora);

	bind = xr_make_delegate(this, &CTexture::apply_load);
}

void CTexture::desc_update()
{
	wait_for_loading();
	desc_cache = pSurface;
	if (pSurface && (D3DRTYPE_TEXTURE == pSurface->GetType()))
	{
		ID3DTexture2D* T = (ID3DTexture2D*)pSurface;
		R_CHK(T->GetLevelDesc(0,&desc));
	}
}

void CTexture::video_Play(BOOL looped, u32 _time)
{
	wait_for_loading();
	if (pTheora) pTheora->Play(looped, (_time != 0xFFFFFFFF) ? (m_play_time = _time) : RDEVICE.dwTimeContinual);
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
