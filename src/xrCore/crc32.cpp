#include "stdafx.h"

#include <array>

namespace
{
    constexpr u32 reflect(u32 ref, int ch) noexcept
    {
        u32 value = 0;
        for (int i = 1; i < (ch + 1); i++)
        {
            if (ref & 1)
                value |= 1 << (ch - i);
            ref >>= 1;
        }
        return value;
    }

    constexpr u32 ulPolynomial = 0x04c11db7;

    constexpr std::array<u32, 256> generate_crc32_lookup_table() noexcept
    {
        std::array<u32, 256> table{};

        for (int i = 0; i <= 0xFF; i++)
        {
            table[i] = reflect(i, 8) << 24;
            for (int j = 0; j < 8; j++)
                table[i] = (table[i] << 1) ^ ((table[i] & (1u << 31)) ? ulPolynomial : 0);
            table[i] = reflect(table[i], 32);
        }

        return table;
    }

    static constexpr auto crc32_table = generate_crc32_lookup_table();

    u32 crc32_sse42(const void* P, u32 len, u32 starting_crc = ~0u) noexcept
    {
        const u8* buffer = static_cast<const u8*>(P);
        u32 crc = starting_crc;

        while (len >= 8)
        {
            crc = (u32)_mm_crc32_u64(crc, *reinterpret_cast<const u64*>(buffer));
            buffer += 8;
            len -= 8;
        }

        if (len >= 4)
        {
            crc = _mm_crc32_u32(crc, *reinterpret_cast<const u32*>(buffer));
            buffer += 4;
            len -= 4;
        }

        if (len >= 2)
        {
            crc = _mm_crc32_u16(crc, *reinterpret_cast<const u16*>(buffer));
            buffer += 2;
            len -= 2;
        }

        if (len)
        {
            crc = _mm_crc32_u8(crc, *buffer);
        }

        return crc;
    }
}

u32 crc32(const void* P, u32 len)
{
    if (CPU::ID.feature & _CPU_FEATURE_SSE4_2)
    {
        return crc32_sse42(P, len);
    }

    u32 ulCRC = 0xffffffff;
    u8* buffer = (u8*)P;

    while (len--)
    {
        ulCRC = (ulCRC >> 8) ^ crc32_table[(ulCRC & 0xFF) ^ *buffer++];
    }

    return ulCRC ^ 0xffffffff;
}

u32 crc32(const void* P, u32 len, u32 starting_crc)
{
    if (CPU::ID.feature & _CPU_FEATURE_SSE4_2)
    {
        return crc32_sse42(P, len, ~starting_crc);
    }

    u32 ulCRC = 0xffffffff ^ starting_crc;
    u8* buffer = (u8*)P;

    while (len--)
    {
        ulCRC = (ulCRC >> 8) ^ crc32_table[(ulCRC & 0xFF) ^ *buffer++];
    }

    return ulCRC ^ 0xffffffff;
}

u32 path_crc32(const char* path, u32 len)
{
    u32 ulCRC = 0xffffffff;
    u8* buffer = (u8*)path;

    while (len--) {
        const u8 c = *buffer;
        if (c != '/' && c != '\\') {
            ulCRC = (ulCRC >> 8) ^ crc32_table[(ulCRC & 0xFF) ^ *buffer];
        }
        ++buffer;
    }

    return ~ulCRC;
}
