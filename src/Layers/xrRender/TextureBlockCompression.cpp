// Compiled in NVTT: keep its private headers/allocator out of renderer translation units.
#include <nvimage/ColorBlock.h>
#include <nvimage/BlockDXT.h>
#include <nvtt/QuickCompressDXT.h>
#include <cstring>

extern "C" void xrCompressDXT5Block(const unsigned char* rgba, void* output)
{
    static_assert(sizeof(nv::BlockDXT5) == 16, "DXT5 block layout");
    nv::ColorBlock colors;
    for (unsigned int pixel = 0; pixel < 16; ++pixel)
    {
        const unsigned char* value = rgba + pixel * 4;
        colors.color(pixel).setRGBA(value[0], value[1], value[2], value[3]);
    }
    nv::BlockDXT5 block;
    nv::QuickCompress::compressDXT5(colors, &block);
    std::memcpy(output, &block, sizeof(block));
}
