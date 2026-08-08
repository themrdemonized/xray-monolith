#include "stdafx.h"
#include "../xrCore/Stream_Reader.h"

#include "GIFResource.h"

#include "gif_lib.h"



#define DEFAULT_DELAY 10 // milliseconds



static int Read_GIF_Fn(GifFileType* handle, GifByteType* buffer, int bytesToRead)
{
    CStreamReader* const reader = static_cast<CStreamReader*>(handle->UserData);
    if (reader->eof()) return 0;

    const u32 pos = reader->tell();
    reader->r(buffer, static_cast<u32>(bytesToRead));
    return static_cast<int>(reader->tell() - pos);
}

static const char* ToString_GifRecordType(GifRecordType type)
{
#define _CASE(T) case GifRecordType::T: return #T
    switch (type)
    {
        _CASE(SCREEN_DESC_RECORD_TYPE);
        _CASE(IMAGE_DESC_RECORD_TYPE);
        _CASE(EXTENSION_RECORD_TYPE);
        _CASE(TERMINATE_RECORD_TYPE);
        default:
            return "UNDEFINED_RECORD_TYPE";
    }
#undef _CASE
}

static const char* get_gif_error_string(GifFileType* gif)
{
    const char* errDesc = GifErrorString(gif->Error);
    return errDesc ? errDesc : "<no-desc>";
}

static IC void write_pixel(u8* image, u32 offset, u8 red, u8 green, u8 blue, u8 alpha)
{
#if (defined(STATIC_RENDERER_R1) || defined(STATIC_RENDERER_R2))
    image[offset + 0] = blue;
    image[offset + 1] = green;
    image[offset + 2] = red;
    image[offset + 3] = alpha;
#else
    image[offset + 0] = red;
    image[offset + 1] = green;
    image[offset + 2] = blue;
    image[offset + 3] = alpha;
#endif
}

static IC void copy_gif_image_rect(GifFileType* gif, u8* dstImage, const u8* srcImage,
                                   GifWord top, GifWord left,
                                   GifWord width, GifWord height)
{
    for (GifWord y = 0; y < height; ++y)
    {
        const GifWord canY = top + y;
        for (GifWord x = 0; x < width; ++x)
        {
            const GifWord canX = left + x;
            const u32 offset = static_cast<u32>((canY * gif->SWidth + canX) * 4);
            dstImage[offset + 0] = srcImage[offset + 0];
            dstImage[offset + 1] = srcImage[offset + 1];
            dstImage[offset + 2] = srcImage[offset + 2];
            dstImage[offset + 3] = srcImage[offset + 3];
        }
    }
}

static IC void fill_gif_image_rect(GifFileType* gif, u8* dstImage,
                                   GifWord top, GifWord left, GifWord width, GifWord height,
                                   u8 red, u8 green, u8 blue, u8 alpha)
{
    for (GifWord y = 0; y < height; ++y)
    {
        const GifWord canY = top + y;
        for (GifWord x = 0; x < width; ++x)
        {
            const GifWord canX = left + x;
            const u32 offset = static_cast<u32>((canY * gif->SWidth + canX) * 4);
            write_pixel(dstImage, offset, red, green, blue, alpha);
        }
    }
}

static bool decode_gif(GifFileType* gif, u8* tempImage, u8* tempLine,
                            xr_vector<CGIFResource::Image>& out,
                            u32 imageSize,
                            const char* fname)
{
    GifRecordType record_type = GifRecordType::UNDEFINED_RECORD_TYPE;
    bool done = false;
    bool result = false;
    bool hasGCB = false;
    u32 lastDelay = DEFAULT_DELAY;
    int transparentIdx = NO_TRANSPARENT_COLOR;
    int prevDisposalMode = DISPOSAL_UNSPECIFIED;
    int nextDisposalMode = DISPOSAL_UNSPECIFIED;
    GraphicsControlBlock tempGCB{};

    GifWord left = 0;
    GifWord top = 0;
    GifWord width = 0;
    GifWord height = 0;

    // Fuck the spec...
    // https://www.w3.org/Graphics/GIF/spec-gif89a.txt
    do
    {
        if (DGifGetRecordType(gif, &record_type) != GIF_OK)
        {
            Msg("! [ERROR]: Failed to read GIF record (%s) in '%s'", get_gif_error_string(gif), fname);
            break;
        }

        switch (record_type)
        {
            case GifRecordType::EXTENSION_RECORD_TYPE:
            {
                int code = 0;
                GifByteType* ext = nullptr;
                if (DGifGetExtension(gif, &code, &ext) != GIF_OK)
                {
                    Msg("! [ERROR]: Failed to read GIF extension (%s) in '%s'", get_gif_error_string(gif), fname);
                    done = true;
                    break;
                }

                while (ext)
                {
                    if (code == GRAPHICS_EXT_FUNC_CODE)
                    {
                        DGifExtensionToGCB(ext[0], &ext[1], &tempGCB);
                        lastDelay = static_cast<u32>(tempGCB.DelayTime) * 10;
                        transparentIdx = tempGCB.TransparentColor;
                        prevDisposalMode = nextDisposalMode;
                        nextDisposalMode = tempGCB.DisposalMode;
                        hasGCB = true;

                        if(lastDelay == 0)
                        {
                            lastDelay = DEFAULT_DELAY;
                        }
                    }

                    if (DGifGetExtensionNext(gif, &ext) != GIF_OK)
                    {
                        Msg("! [ERROR]: Failed to read next GIF extension (%s) in '%s'", get_gif_error_string(gif), fname);
                        done = true;
                        break;
                    }
                }
                break;
            }
            case GifRecordType::IMAGE_DESC_RECORD_TYPE:
            {
                if (DGifGetImageDesc(gif) != GIF_OK)
                {
                    Msg("! [ERROR]: Failed to read GIF image description (%s) in '%s'", get_gif_error_string(gif), fname);
                    done = true;
                    break;
                }

                if (hasGCB)
                {
                    hasGCB = false;
                }
                else
                {
                    // Reset the graphics control block
                    lastDelay = DEFAULT_DELAY;
                    transparentIdx = NO_TRANSPARENT_COLOR;
                    nextDisposalMode = DISPOSAL_UNSPECIFIED;
                }

                if (!out.empty())
                {
                    if (prevDisposalMode == DISPOSE_BACKGROUND)
                    {
                        fill_gif_image_rect(gif, tempImage, top, left, width, height, 0x00, 0x00, 0x00, 0x00);
                    }
                    else if (prevDisposalMode == DISPOSE_PREVIOUS)
                    {
                        const u8* const prevImage = out.back().data;
                        copy_gif_image_rect(gif, tempImage, prevImage, top, left, width, height);
                    }
                    //else if (prevDisposalMode == DISPOSAL_UNSPECIFIED)
                    //{
                    //    // Skip
                    //}
                    //else if (prevDisposalMode == DISPOSE_DO_NOT)
                    //{
                    //    // Skip
                    //}
                }

                left = gif->Image.Left;
                top = gif->Image.Top;
                width = gif->Image.Width;
                height = gif->Image.Height;
                ColorMapObject* const colorMap = gif->Image.ColorMap ? gif->Image.ColorMap : gif->SColorMap;
                VERIFY(colorMap);
                VERIFY((left + width) <= gif->SWidth);
                VERIFY((top + height) <= gif->SHeight);

                if (gif->Image.Interlace)
                {
			        constexpr GifWord InterlacedOffset[]{0, 4, 2, 1};
			        constexpr GifWord InterlacedJumps[]{8, 8, 4, 2};
			        for (GifWord i = 0; i < 4 && !done; ++i)
                    {
				        for (GifWord y = InterlacedOffset[i]; y < height; y += InterlacedJumps[i])
                        {
                            if (DGifGetLine(gif, tempLine, width) != GIF_OK)
                            {
                                Msg("! [ERROR]: Failed to read GIF image line (%s) in '%s'", get_gif_error_string(gif), fname);
                                done = true;
                                break;
                            }

                            const GifWord canY = top + y;
                            for (GifWord x = 0; x < width; ++x)
                            {
                                const GifWord canX = left + x;
                                const u32 offset = static_cast<u32>((canY * gif->SWidth + canX) * 4);
                                const int idx = tempLine[x];

                                VERIFY(idx < colorMap->ColorCount);

                                if (transparentIdx == NO_TRANSPARENT_COLOR || idx != transparentIdx)
                                {
                                    const GifColorType c = colorMap->Colors[idx];
                                    write_pixel(tempImage, offset, c.Red, c.Green, c.Blue, 0xFF);
                                }
                            }
				        }
			        }
                }
                else
                {
                    for (GifWord y = 0; y < height; ++y)
                    {
                        if (DGifGetLine(gif, tempLine, width) != GIF_OK)
                        {
                            Msg("! [ERROR]: Failed to read GIF image line (%s) in '%s'", get_gif_error_string(gif), fname);
                            done = true;
                            break;
                        }

                        const GifWord canY = top + y;
                        for (GifWord x = 0; x < width; ++x)
                        {
                            const GifWord canX = left + x;
                            const int idx = tempLine[x];
                            const u32 offset = static_cast<u32>((canY * gif->SWidth + canX) * 4);

                            VERIFY(idx < colorMap->ColorCount);

                            if (transparentIdx == NO_TRANSPARENT_COLOR || idx != transparentIdx)
                            {
                                const GifColorType c = colorMap->Colors[idx];
                                write_pixel(tempImage, offset, c.Red, c.Green, c.Blue, 0xFF);
                            }
                        }
                    }
                }

                if (!done)
                {
                    u8* const imgData = static_cast<u8*>(Memory.mem_alloc(imageSize));
                    CopyMemory(imgData, tempImage, imageSize);
                    CGIFResource::Image& newImage = out.emplace_back();
                    newImage.data = imgData;
                    newImage.delay = lastDelay;
                }
                break;
            }
            case GifRecordType::TERMINATE_RECORD_TYPE:
            {
                done = true;
                result = true;
                break;
            }
            default:
            {
                Msg("! [ERROR]: Unexpected GIF record [%s] in '%s'", ToString_GifRecordType(record_type), fname);
                done = true;
                break;
            }
        }
    } while (!done);
    return result;
}



CGIFResource::CGIFResource()
    : m_Images(), m_Width(0), m_Height(0), m_ImageSize(0)
{
    //
}

CGIFResource::~CGIFResource()
{
    for (const Image& info : m_Images)
    {
        Memory.mem_free(const_cast<u8*>(info.data));
    }
}

bool CGIFResource::Load(const char* fname)
{
    VERIFY(m_Images.empty());

    CStreamReader* reader = FS.rs_open(nullptr, fname);
    VERIFY(reader);

    int err = 0;
    GifFileType* const gif = DGifOpen(reader, &Read_GIF_Fn, &err);
    if (!gif)
    {
        const char* errStr = GifErrorString(err);
        if (!errStr) errStr = "<no-desc>";

        Msg("! Can't load gif '%s' (%s)", fname, errStr);
        FS.r_close(reader);
        return false;
    }

    m_Width = static_cast<u32>(gif->SWidth);
    m_Height = static_cast<u32>(gif->SHeight);
    m_ImageSize = m_Width * m_Height * 4;

    const u32 tempMemSize = m_Width + m_ImageSize;
    void* const tempMem = Memory.mem_alloc(tempMemSize);
    u8* const tempImage = static_cast<u8*>(tempMem);
    u8* const tempLine = static_cast<u8*>(tempMem) + m_ImageSize;

    ZeroMemory(tempMem, tempMemSize);
    const bool decodeResult = decode_gif(gif, tempImage, tempLine, m_Images, m_ImageSize, fname);

    DGifCloseFile(gif, nullptr);
    FS.r_close(reader);
    Memory.mem_free(tempMem);
    return decodeResult;
}

const xr_vector<CGIFResource::Image>& CGIFResource::GetImages() const
{
    return m_Images;
}

u32 CGIFResource::GetWidth() const
{
    return m_Width;
}

u32 CGIFResource::GetHeight() const
{
    return m_Height;
}

u32 CGIFResource::GetImageSize() const
{
    return m_ImageSize;
}
