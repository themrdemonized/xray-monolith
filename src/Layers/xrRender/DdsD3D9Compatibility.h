#pragma once

#include <cstdio>

extern "C" void xrCompressDXT5Block(const unsigned char* rgba, void* output);

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable: 4244 4267 4505)
#include "../../3rd party/bcdec/bcdec.h"
#pragma warning(pop)
#undef BCDEC_IMPLEMENTATION
#undef BCDEC_STATIC

namespace DdsD3D9Compatibility
{
    struct PixelFormat
    {
        u32 size, flags, fourCC, bits, red, green, blue, alpha;
    };

    struct Header
    {
        u32 size, flags, height, width, pitch, depth, mipCount, reserved[11];
        PixelFormat format;
        u32 caps, caps2, caps3, caps4, reserved2;
    };

    struct Extension
    {
        u32 format, dimension, miscFlags, arraySize, alphaMode;
    };

    static_assert(sizeof(Header) == 124 && sizeof(Extension) == 20, "DDS header layout");
    constexpr u32 magic = MAKEFOURCC('D', 'D', 'S', ' ');
    constexpr u32 legacySize = 128;
    constexpr u32 extendedSize = 148;

    struct Layout
    {
        Header header;
        u32 faces;
        u32 payloadSize;
        bool bc7;
    };

    inline bool readLayout(const IReader& source, Layout& layout)
    {
        if (source.length() < int(extendedSize))
            return false;
        const u8* data = static_cast<const u8*>(source.pointer());
        u32 signature;
        memcpy(&signature, data, sizeof(signature));
        memcpy(&layout.header, data + 4, sizeof(Header));
        Extension extension;
        memcpy(&extension, data + legacySize, sizeof(extension));
        Header& header = layout.header;
        if (signature != magic || header.size != 124 || header.format.size != 32 ||
            !(header.format.flags & 4) || header.format.fourCC != MAKEFOURCC('D', 'X', '1', '0'))
            return false;
        u32 fourCC = 0;
        const bool rgba = extension.format >= 27 && extension.format <= 29;
        const bool bgra = extension.format == 87 || extension.format == 90 || extension.format == 91;
        layout.bc7 = extension.format >= 97 && extension.format <= 99;
        if (layout.bc7 || (extension.format >= 76 && extension.format <= 78))
            fourCC = MAKEFOURCC('D', 'X', 'T', '5');
        else if (extension.format >= 70 && extension.format <= 72)
            fourCC = MAKEFOURCC('D', 'X', 'T', '1');
        else if (extension.format >= 73 && extension.format <= 75)
            fourCC = MAKEFOURCC('D', 'X', 'T', '3');
        else if (!rgba && !bgra)
            return false;
        if (extension.dimension != 3 || extension.arraySize != 1 || header.depth > 1 ||
            !header.width || !header.height || header.width > 16384 || header.height > 16384)
            return false;

        layout.faces = (extension.miscFlags & 4) ? 6 : 1;
        if (layout.faces == 6 && header.width != header.height)
            return false;
        header.mipCount = header.mipCount ? header.mipCount : 1;
        u32 maxMips = 1;
        for (u32 dimension = _max(header.width, header.height); dimension > 1; dimension >>= 1)
            ++maxMips;
        if (header.mipCount > maxMips)
            return false;
        if (!fourCC && (header.flags & 8) && header.pitch && header.pitch != header.width * 4)
            return false; // Do not reinterpret padded raw rows as tightly packed pixels.

        u64 payload = 0;
        const u32 blockSize = fourCC == MAKEFOURCC('D', 'X', 'T', '1') ? 8 : 16;
        u32 width = header.width, height = header.height;
        for (u32 mip = 0; mip < header.mipCount; ++mip)
        {
            payload += fourCC ? u64((width + 3) / 4) * ((height + 3) / 4) * blockSize : u64(width) * height * 4;
            width = _max(1u, width / 2);
            height = _max(1u, height / 2);
        }
        payload *= layout.faces;
        if (payload + extendedSize > u64(source.length()) || payload + legacySize > INT_MAX)
            return false;
        layout.payloadSize = u32(payload);

        header.flags = (header.flags & ~(8u | 0x80000u)) | (fourCC ? 0x80000u : 8u);
        header.pitch = fourCC ? ((header.width + 3) / 4) * ((header.height + 3) / 4) * blockSize : header.width * 4;
        header.depth = 0;
        ZeroMemory(header.reserved, sizeof(header.reserved));
        if (fourCC)
            header.format = {32, 4, fourCC, 0, 0, 0, 0, 0};
        else
            header.format = {32, 0x41, 0, 32, rgba ? 0xFFu : 0xFF0000u, 0xFF00u,
                rgba ? 0xFF0000u : 0xFFu, 0xFF000000u};
        header.caps = 0x1000 | ((header.mipCount > 1 || layout.faces == 6) ? 8 : 0);
        if (header.mipCount > 1)
        {
            header.flags |= 0x20000;
            header.caps |= 0x400000;
        }
        else
            header.flags &= ~0x20000u;
        header.caps2 = layout.faces == 6 ? 0xFE00 : 0;
        header.caps3 = header.caps4 = header.reserved2 = 0;
        return true;
    }

    inline bool convert(const IReader& source, const Layout& layout, CMemoryWriter& output)
    {
        output.clear();
        output.w_u32(magic);
        output.w(&layout.header, sizeof(Header));
        const u8* input = static_cast<const u8*>(source.pointer()) + extendedSize;
        if (!layout.bc7)
        {
            // BC1/2/3 and RGBA/BGRA8 already have D3D9 representations; keep their pixel bytes.
            output.w(input, layout.payloadSize);
            return true;
        }
        for (u32 face = 0; face < layout.faces; ++face)
        {
            u32 width = layout.header.width, height = layout.header.height;
            for (u32 mip = 0; mip < layout.header.mipCount; ++mip)
            {
                for (u32 y = 0; y < height; y += 4)
                {
                    for (u32 x = 0; x < width; x += 4, input += 16)
                    {
                        if (!input[0]) // BC7 must select one of its eight modes.
                        {
                            output.clear();
                            return false;
                        }
                        u8 pixels[64];
                        bcdec_bc7(input, pixels, 16);
                        const u32 lastX = _min(3u, width - x - 1);
                        const u32 lastY = _min(3u, height - y - 1);
                        for (u32 row = 0; row < 4; ++row)
                        {
                            for (u32 column = 0; column < 4; ++column)
                            {
                                const u8* pixel = pixels + 4 * (4 * _min(row, lastY) + _min(column, lastX));
                                u8* destination = pixels + 4 * (4 * row + column);
                                if (destination != pixel)
                                    memcpy(destination, pixel, 4);
                            }
                        }
                        u8 block[16];
                        xrCompressDXT5Block(pixels, block);
                        output.w(block, sizeof(block));
                    }
                }
                width = _max(1u, width / 2);
                height = _max(1u, height / 2);
            }
        }
        return true;
    }

    inline bool load(LPCSTR name, IReader& source, CMemoryWriter& output)
    {
        Layout layout;
        if (!readLayout(source, layout))
            return false;
        if (!layout.bc7)
        {
            convert(source, layout, output);
            Msg("* D3D9 DX10 DDS legacy header: %s", name);
            return true;
        }

        // ponytail: serialize cold conversions to bound transient RAM and cache publication races.
        // Narrow the lock if MT loading profiles demonstrate contention.
        static xrCriticalSection conversionLock;
        xrCriticalSectionGuard guard(conversionLock);
        const u32 sourceCRC = crc32(source.pointer(), source.length());
        const u32 nameCRC = path_crc32(name, u32(xr_strlen(name)));
        const u32 encodedSize = legacySize + layout.payloadSize;
        string_path relative, cachedPath;
        xr_sprintf(relative, "textures_cache\\d3d9\\bc7_v1_%08x_%08x_%08x.bin",
            nameCRC, sourceCRC, u32(source.length()));
        FS.update_path(cachedPath, "$app_data_root$", relative);
        // Derived cache files use checked stdio: the engine file writer asserts on short writes,
        // and the locator's rename neither reports failure nor atomically replaces the destination.
        FILE* cached = nullptr;
        if (fopen_s(&cached, cachedPath, "rb") == 0)
        {
            u32 checksums[2];
            bool valid = fseek(cached, 0, SEEK_END) == 0 && ftell(cached) == s64(encodedSize) + 8 &&
                fseek(cached, 0, SEEK_SET) == 0 && fread(checksums, 1, sizeof(checksums), cached) == sizeof(checksums) &&
                checksums[0] == sourceCRC;
            output.clear();
            u8 buffer[65536];
            for (u32 remaining = encodedSize; valid && remaining;)
            {
                const u32 count = _min(remaining, u32(sizeof(buffer)));
                valid = fread(buffer, 1, count, cached) == count;
                if (valid)
                    output.w(buffer, count);
                remaining -= count;
            }
            valid = valid && checksums[1] == crc32(output.pointer(), output.size()) &&
                memcmp(output.pointer(), &magic, sizeof(magic)) == 0 &&
                memcmp(static_cast<const u8*>(output.pointer()) + 4, &layout.header, sizeof(Header)) == 0;
            fclose(cached);
            if (valid)
            {
                Msg("* D3D9 BC7 cache hit: %s", name);
                return true;
            }
            Msg("~ D3D9 BC7 cache invalid, rebuilding: %s", name);
        }

        CTimerBase timer;
        timer.Start();
        if (!convert(source, layout, output))
        {
            Msg("! Invalid BC7 texture data: %s", name);
            return false;
        }
        const u32 conversionMs = timer.GetElapsed_ms();
        string_path temporary;
        xr_sprintf(temporary, "%s.%u.tmp", cachedPath, GetCurrentProcessId());
        VerifyPath(temporary);
        FILE* writer = nullptr;
        bool saved = false;
        if (fopen_s(&writer, temporary, "wb") == 0)
        {
            const u32 checksums[] = {sourceCRC, crc32(output.pointer(), output.size())};
            saved = fwrite(checksums, 1, sizeof(checksums), writer) == sizeof(checksums) &&
                fwrite(output.pointer(), 1, output.size(), writer) == output.size();
            if (fclose(writer) != 0)
                saved = false;
            if (saved)
                saved = MoveFileExA(temporary, cachedPath, MOVEFILE_REPLACE_EXISTING) != FALSE;
        }
        if (!saved)
        {
            DeleteFileA(temporary);
            Msg("~ D3D9 BC7 cache write unavailable, using memory: %s", cachedPath);
        }
        Msg("* D3D9 BC7 converted to DXT5: %s (%ux%u, %u mips, %u faces, %u ms, %u bytes)",
            name, layout.header.width, layout.header.height, layout.header.mipCount, layout.faces,
            conversionMs, output.size());
        return true;
    }
}
