#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace static_geometry
{
constexpr std::array<std::uint32_t, 256> crc32_table()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i != 256; ++i)
    {
        std::uint32_t value = i;
        for (unsigned bit = 0; bit != 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0);
        table[i] = value;
    }
    return table;
}

inline std::uint32_t file_crc32(const void* data, std::size_t size)
{
    // Stable IEEE CRC32, independent of CPU and engine cache CRC conventions.
    static constexpr auto table = crc32_table();
    auto bytes = static_cast<const unsigned char*>(data);
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i != size; ++i)
        crc = (crc >> 8) ^ table[(crc ^ bytes[i]) & 255];
    return crc ^ 0xffffffffu;
}
}
