#include "stdafx.h"

#include "gifPlayer.h"
#include "../../xrEngine/GIFResource.h"



CGIFAnimationPlayer::CGIFAnimationPlayer()
    : m_Frames(),
      m_ActiveFrame(nullptr),
      m_CurrFrameIdx(0),
      m_TimeAccum(0),
      m_LastdwTimeContinual(0),
      m_MemUsed(0)
{
    //
}

CGIFAnimationPlayer::~CGIFAnimationPlayer()
{
    for (Frame& f : m_Frames)
    {
        _RELEASE(f.surface);
#if (defined(USE_DX10) || defined(USE_DX11))
        _RELEASE(f.srv);
#endif
    }
}

bool CGIFAnimationPlayer::Load(const char* fname)
{
    VERIFY(m_Frames.empty());

    CGIFResource res;
    if(!res.Load(fname))
        return false;

    const xr_vector<CGIFResource::Image>& images = res.GetImages();
    if (images.empty())
        return false;

    const u32 imageCount = images.size();
    const u32 width = res.GetWidth();
    const u32 height = res.GetHeight();
    const u32 imageSize = res.GetImageSize();
    const u32 pitch = width * 4;

#if (defined(USE_DX10) || defined(USE_DX11))
    D3D_TEXTURE2D_DESC desc_10_11;
    desc_10_11.Width = width;
    desc_10_11.Height = height;
    desc_10_11.MipLevels = 1;
    desc_10_11.ArraySize = 1;
    desc_10_11.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc_10_11.SampleDesc.Count = 1;
    desc_10_11.SampleDesc.Quality = 0;
    desc_10_11.Usage = D3D_USAGE_IMMUTABLE;
    desc_10_11.BindFlags = D3D_BIND_SHADER_RESOURCE;
    desc_10_11.CPUAccessFlags = 0;
    desc_10_11.MiscFlags = 0;

    D3D_SUBRESOURCE_DATA subRes_10_11{};
    subRes_10_11.SysMemPitch = pitch;
#endif

    m_Frames.reserve(imageCount);
    for (u32 i = 0; i < imageCount; ++i)
    {
        ID3DTexture2D* texture = nullptr;
        Frame& f = m_Frames.emplace_back();
        const CGIFResource::Image& img = images[i];

        f.delay = img.delay;

#if (defined(USE_DX10) || defined(USE_DX11))
        subRes_10_11.pSysMem = img.data;

        const HRESULT result = HW.pDevice->CreateTexture2D(&desc_10_11, &subRes_10_11, &texture);
        if (FAILED(result))
        {
            m_Frames.pop_back();
	        FATAL("Can't create gif texture");
	        R_CHK(result);
            return false;
        }

        CHK_DX(HW.pDevice->CreateShaderResourceView(texture, nullptr, &f.srv));
#else
        HRESULT result = HW.pDevice->CreateTexture(
            width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
        if (FAILED(result))
        {
            m_Frames.pop_back();
	        FATAL("Can't create gif texture");
	        R_CHK(result);
            return false;
        }

		D3DLOCKED_RECT R;
		R_CHK(texture->LockRect(0, &R, nullptr, 0));
        R_ASSERT(R.Pitch >= INT(pitch));

        u8* pDst = static_cast<u8*>(R.pBits);
        const u8* pSrc = img.data;
        for (u32 y = 0; y < height; ++y)
        {
            CopyMemory(pDst, pSrc, pitch);
            pDst += R.Pitch;
            pSrc += pitch;
        }

        R_CHK(texture->UnlockRect(0));
#endif
        f.surface = texture;
    }

    m_MemUsed = imageCount * imageSize;
    return true;
}

bool CGIFAnimationPlayer::UpdateFrame()
{
    VERIFY(IsPlaying());

    const u32 tc = Device.dwTimeContinual;

    m_TimeAccum += (tc - m_LastdwTimeContinual);
    m_LastdwTimeContinual = tc;
    if (m_TimeAccum < m_ActiveFrame->delay)
        return false;

    const u32 maxFrames = m_Frames.size();
    constexpr u32 maxSkipFrames = 10;
    u32 framesSkipped = 0;
    do
    {
        m_TimeAccum -= m_ActiveFrame->delay;
        ++m_CurrFrameIdx;
        if (m_CurrFrameIdx == maxFrames)
        {
            m_CurrFrameIdx = 0;
        }
        m_ActiveFrame = &m_Frames[m_CurrFrameIdx];

        if (framesSkipped == maxSkipFrames)
        {
            m_TimeAccum = 0;
            break;
        }
        ++framesSkipped;
    } while (m_TimeAccum >= m_ActiveFrame->delay);
    return true;
}

void CGIFAnimationPlayer::Play()
{
    if (IsValid())
    {
        m_TimeAccum = 0;
        m_LastdwTimeContinual = Device.dwTimeContinual;
        m_CurrFrameIdx = 0;
        m_ActiveFrame = &m_Frames[0];
    }
}

void CGIFAnimationPlayer::Stop()
{
    m_ActiveFrame = nullptr;
}
