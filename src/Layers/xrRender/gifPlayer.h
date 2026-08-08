#ifndef _GIF_PLAYER_
#define _GIF_PLAYER_
#pragma once



class CGIFAnimationPlayer : xray::noncopyable
{
public:
    struct Frame
    {
        ID3DBaseTexture* surface;
#if (defined(USE_DX10) || defined(USE_DX11))
        ID3DShaderResourceView* srv;
#endif
        u32 delay;
    };

private:
    xr_vector<Frame> m_Frames;
    const Frame* m_ActiveFrame;
    u32 m_CurrFrameIdx;
    u32 m_TimeAccum;
    u32 m_LastdwTimeContinual;
    u32 m_MemUsed;

public:
    CGIFAnimationPlayer();
    ~CGIFAnimationPlayer();

public:
    bool IsValid() const
    {
        return !m_Frames.empty();
    }

    u32 GetUsedMemory() const
    {
        return m_MemUsed;
    }

    bool Load(const char* fname);
    bool UpdateFrame();

public:
    const Frame* GetActiveFrame() const
    {
        return m_ActiveFrame;
    }

    u32 GetNumFrames() const
    {
        return m_Frames.size();
    }

    bool IsPlaying() const
    {
        return static_cast<bool>(m_ActiveFrame);
    }

    void Play();
    void Stop();
};

#endif
