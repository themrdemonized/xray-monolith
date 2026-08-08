#ifndef _GIF_RESOURCE_
#define _GIF_RESOURCE_
#pragma once



class CGIFResource : private xray::noncopyable
{
public:
    struct Image
    {
        const u8* data;
        u32 delay; // ms.
    };

private:
    xr_vector<Image> m_Images;
    u32 m_Width;
    u32 m_Height;
    u32 m_ImageSize;

public:
    CGIFResource();
    ~CGIFResource();

public:
    bool Load(const char* fname);
    const xr_vector<Image>& GetImages() const;
    u32 GetWidth() const;
    u32 GetHeight() const;
    u32 GetImageSize() const;
};

#endif
