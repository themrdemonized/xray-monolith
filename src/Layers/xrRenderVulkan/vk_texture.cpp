#include "stdafx.h"
#include "vk_texture.h"
#include "vk_buffer.h"
#include "vk_command_buffer.h"
#include "HW_Vulkan.h"

// DDS definitions
const u32 DDS_MAGIC = 0x20534444; // "DDS "

struct DDS_PIXELFORMAT {
    u32 dwSize;
    u32 dwFlags;
    u32 dwFourCC;
    u32 dwRGBBitCount;
    u32 dwRBitMask;
    u32 dwGBitMask;
    u32 dwBBitMask;
    u32 dwABitMask;
};

struct DDS_HEADER {
    u32 dwSize;
    u32 dwFlags;
    u32 dwHeight;
    u32 dwWidth;
    u32 dwPitchOrLinearSize;
    u32 dwDepth;
    u32 dwMipMapCount;
    u32 dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    u32 dwCaps;
    u32 dwCaps2;
    u32 dwCaps3;
    u32 dwCaps4;
    u32 dwReserved2;
};

// FourCC codes
const u32 FOURCC_DXT1 = 0x31545844; // "DXT1"
const u32 FOURCC_DXT3 = 0x33545844; // "DXT3"
const u32 FOURCC_DXT5 = 0x35545844; // "DXT5"

// Flags
const u32 DDPF_ALPHAPIXELS = 0x1;
const u32 DDPF_ALPHA       = 0x2;
const u32 DDPF_FOURCC      = 0x4;
const u32 DDPF_RGB         = 0x40;
const u32 DDPF_LUMINANCE   = 0x20000;

namespace VK
{

// Constructor
CVulkanTexture::CVulkanTexture()
{
}

// Destructor
CVulkanTexture::~CVulkanTexture()
{
    Destroy();
}

// Создание пустой текстуры
void CVulkanTexture::Create(u32 width, u32 height, VkFormat format, u32 mipLevels,
                            VkImageUsageFlags usage)
{
    if (m_Image != VK_NULL_HANDLE) {
        Msg("![Vulkan] Texture already created, call Destroy first");
        return;
    }

    if (width == 0 || height == 0) {
        Msg("![Vulkan] Cannot create texture with size 0");
        return;
    }

    m_Width = width;
    m_Height = height;
    m_Format = format;
    m_MipLevels = mipLevels > 0 ? mipLevels : 1;

    // Image create info
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = m_MipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    // VMA allocation info - prefer device local memory
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VK_CHECK(vmaCreateImage(VulkanHW.m_Allocator, &imageInfo, &allocInfo,
                            &m_Image, &m_Allocation, nullptr));

    m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Создаём view и sampler
    CreateImageView();
    CreateSampler();

    // Msg("[Vulkan] Texture created: %dx%d, format=%d, mips=%d", width, height, format, m_MipLevels);
}

// Создание текстуры из данных
void CVulkanTexture::CreateFromData(const void* data, u32 width, u32 height, VkFormat format,
                                    VkDeviceSize dataSize)
{
    if (!data) {
        Msg("![Vulkan] CreateFromData: data is null");
        return;
    }

    // Рассчитываем размер данных если не указан
    if (dataSize == 0) {
        if (IsCompressedFormat(format)) {
            // Для compressed форматов нужен явный размер
            Msg("![Vulkan] CreateFromData: dataSize required for compressed format");
            return;
        }
        // Для RGBA8 - 4 bytes per pixel
        dataSize = width * height * 4;
    }

    // Создаём текстуру
    Create(width, height, format, 1);
    if (m_Image == VK_NULL_HANDLE) {
        return;
    }

    // Upload данных
    UploadData(data, dataSize);
}

// Upload данных через staging buffer
void CVulkanTexture::UploadData(const void* data, VkDeviceSize size)
{
    // Создаём staging buffer
    CVulkanBuffer stagingBuffer;
    stagingBuffer.Create(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST
    );

    // Копируем данные в staging
    void* mapped = stagingBuffer.Map();
    if (!mapped) {
        Msg("![Vulkan] Failed to map staging buffer for texture");
        stagingBuffer.Destroy();
        return;
    }

    memcpy(mapped, data, size);
    stagingBuffer.Flush();
    stagingBuffer.Unmap();

    // Создаём локальный command pool и buffer для upload
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = VulkanHW.m_GraphicsFamily;

    VkCommandPool cmdPool;
    VK_CHECK(vkCreateCommandPool(VulkanHW.m_Device, &poolInfo, nullptr, &cmdPool));

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(VulkanHW.m_Device, &allocInfo, &cmd));

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // Transition: UNDEFINED -> TRANSFER_DST
    TransitionLayout(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Prepare copy regions for mipmaps
    xr_vector<VkBufferImageCopy> regions;
    VkDeviceSize offset = 0;
    u32 currentWidth = m_Width;
    u32 currentHeight = m_Height;

    for (u32 i = 0; i < m_MipLevels; i++) {
        VkBufferImageCopy region = {};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;   // Tightly packed
        region.bufferImageHeight = 0; // Tightly packed
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = i;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {currentWidth, currentHeight, 1};

        regions.push_back(region);

        // Calculate size of current mip
        VkDeviceSize currentSize = 0;
        if (IsCompressedFormat(m_Format)) {
            u32 blockSize = GetBlockSize(m_Format);
            u32 blocksX = (currentWidth + 3) / 4;
            u32 blocksY = (currentHeight + 3) / 4;
            currentSize = blocksX * blocksY * blockSize;
        } else {
            // Bytes per pixel depends on format
            u32 bpp = 4; // Default: RGBA8/BGRA8
            if (m_Format == VK_FORMAT_R8_UNORM)
                bpp = 1;
            else if (m_Format == VK_FORMAT_R8G8_UNORM)
                bpp = 2;
            currentSize = currentWidth * currentHeight * bpp;
        }

        offset += currentSize;

        // Next mip dimensions
        if (currentWidth > 1) currentWidth /= 2;
        if (currentHeight > 1) currentHeight /= 2;
    }

    vkCmdCopyBufferToImage(cmd, stagingBuffer.m_Buffer, m_Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (u32)regions.size(), regions.data());

    // Transition: TRANSFER_DST -> SHADER_READ_ONLY
    TransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit и wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(VulkanHW.m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(VulkanHW.m_GraphicsQueue);

    // Cleanup command pool
    vkDestroyCommandPool(VulkanHW.m_Device, cmdPool, nullptr);

    // Cleanup staging buffer
    stagingBuffer.Destroy();
}

// Transition image layout
void CVulkanTexture::TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = m_MipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        Msg("![Vulkan] Unsupported layout transition: %d -> %d", oldLayout, newLayout);
        return;
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    m_CurrentLayout = newLayout;
}

// Transition layout с временным command buffer
void CVulkanTexture::TransitionLayoutImmediate(VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandBuffer cmd = CommandManager.Begin();
    TransitionLayout(cmd, oldLayout, newLayout);
    CommandManager.End(cmd);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(VulkanHW.m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(VulkanHW.m_GraphicsQueue);
}

// Создание ImageView
void CVulkanTexture::CreateImageView()
{
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_Format;
    if (m_bAlphaSwizzle) {
        // Alpha-only texture (fonts): R channel → alpha, RGB = white
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_ONE;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_ONE;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_ONE;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_R;
    } else if (m_bBCSwizzle) {
        // BC/DXT textures: swap R<->B channels
        // DirectX DXT textures use BGRA order, Vulkan BC uses RGBA
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    } else {
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    }
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_MipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &viewInfo, nullptr, &m_ImageView));
}

// Создание Sampler
// Using CLAMP_TO_EDGE to match R3/R4 DX behavior (shader:dx10sampler("smp_base"):clamp())
void CVulkanTexture::CreateSampler()
{
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    // Use CLAMP_TO_EDGE like R3/R4 does for UI textures
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;  // Max anisotropy
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_MipLevels);

    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &samplerInfo, nullptr, &m_Sampler));
}

// Уничтожение текстуры
void CVulkanTexture::Destroy()
{
    if (m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(VulkanHW.m_Device, m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }

    if (m_ImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(VulkanHW.m_Device, m_ImageView, nullptr);
        m_ImageView = VK_NULL_HANDLE;
    }

    if (m_Image != VK_NULL_HANDLE) {
        vmaDestroyImage(VulkanHW.m_Allocator, m_Image, m_Allocation);
        m_Image = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
    }

    m_Width = 0;
    m_Height = 0;
    m_MipLevels = 1;
    m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

// Загрузка DDS
bool CVulkanTexture::LoadDDS(const char* filename)
{
    IReader* F = FS.r_open(filename);
    if (!F) {
        Msg("![Vulkan] Failed to open texture: %s", filename);
        return false;
    }

    // Check magic
    u32 magic = 0;
    F->r(&magic, 4);
    if (magic != DDS_MAGIC) {
        Msg("![Vulkan] Invalid DDS magic in %s", filename);
        FS.r_close(F);
        return false;
    }

    // Read header
    DDS_HEADER header;
    F->r(&header, sizeof(DDS_HEADER));

    // Determine format
    VkFormat format = VK_FORMAT_UNDEFINED;
    
    if (header.ddspf.dwFlags & DDPF_FOURCC) {
        switch (header.ddspf.dwFourCC) {
            case FOURCC_DXT1:
                // DXT1 может быть с альфой или без
                if (header.ddspf.dwFlags & DDPF_ALPHAPIXELS) {
                    format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                } else {
                    format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
                }
                break;
            case FOURCC_DXT3:
                format = VK_FORMAT_BC2_UNORM_BLOCK;
                break;
            case FOURCC_DXT5:
                format = VK_FORMAT_BC3_UNORM_BLOCK;
                break;
            default:
                Msg("![Vulkan] Unsupported FourCC: %X in %s", header.ddspf.dwFourCC, filename);
                FS.r_close(F);
                return false;
        }
    } else if (header.ddspf.dwFlags & DDPF_RGB) {
        if (header.ddspf.dwRGBBitCount == 32) {
            format = VK_FORMAT_B8G8R8A8_UNORM;
        } else {
            Msg("![Vulkan] Unsupported RGB bit count: %d in %s", header.ddspf.dwRGBBitCount, filename);
            FS.r_close(F);
            return false;
        }
    } else if (header.ddspf.dwFlags & DDPF_ALPHA) {
        // Alpha-only format (A8) — used by font textures
        // Store as R8, swizzle R→A in image view
        format = VK_FORMAT_R8_UNORM;
        m_bAlphaSwizzle = true;
    } else if (header.ddspf.dwFlags & DDPF_LUMINANCE) {
        // Luminance format (L8 or L8A8)
        if (header.ddspf.dwRGBBitCount == 8) {
            format = VK_FORMAT_R8_UNORM;
        } else if (header.ddspf.dwRGBBitCount == 16) {
            format = VK_FORMAT_R8G8_UNORM;
        } else {
            Msg("![Vulkan] Unsupported luminance bit count: %d in %s", header.ddspf.dwRGBBitCount, filename);
            FS.r_close(F);
            return false;
        }
    } else {
        Msg("![Vulkan] Unsupported DDS format flags: %X in %s", header.ddspf.dwFlags, filename);
        FS.r_close(F);
        return false;
    }

    u32 width = header.dwWidth;
    u32 height = header.dwHeight;
    u32 mipLevels = (header.dwFlags & 0x20000) ? header.dwMipMapCount : 1; // DDSD_MIPMAPCOUNT
    if (mipLevels == 0) mipLevels = 1;

    // Debug: log format for magnifier texture specifically
    if (strstr(filename, "magnifier")) {
        Msg("[Vulkan Texture] MAGNIFIER DETAILED:");
        Msg("  File: %s", filename);
        Msg("  Size: %dx%d, mips=%d", width, height, mipLevels);
        Msg("  Header size: %d", header.dwSize);
        Msg("  Flags: 0x%X", header.dwFlags);
        Msg("  PitchOrLinearSize: %d", header.dwPitchOrLinearSize);
        Msg("  PixelFormat size: %d", header.ddspf.dwSize);
        Msg("  PixelFormat flags: 0x%X", header.ddspf.dwFlags);
        Msg("  FourCC: 0x%X ('%c%c%c%c')", header.ddspf.dwFourCC,
            (char)(header.ddspf.dwFourCC & 0xFF),
            (char)((header.ddspf.dwFourCC >> 8) & 0xFF),
            (char)((header.ddspf.dwFourCC >> 16) & 0xFF),
            (char)((header.ddspf.dwFourCC >> 24) & 0xFF));
        Msg("  RGBBitCount: %d", header.ddspf.dwRGBBitCount);
        Msg("  RMask: 0x%X, GMask: 0x%X, BMask: 0x%X, AMask: 0x%X",
            header.ddspf.dwRBitMask, header.ddspf.dwGBitMask,
            header.ddspf.dwBBitMask, header.ddspf.dwABitMask);
        Msg("  Caps: 0x%X, Caps2: 0x%X", header.dwCaps, header.dwCaps2);
        Msg("  VkFormat: %d", format);
    }

    // Create texture
    Create(width, height, format, mipLevels);

    // Read remaining data
    VkDeviceSize dataSize = F->length() - F->tell();
    
    // Allocate temp buffer
    void* data = xr_malloc(dataSize);
    F->r(data, dataSize);
    FS.r_close(F);

    // Upload
    UploadData(data, dataSize);

    xr_free(data);

    // Msg("[Vulkan] Loaded DDS: %s (%dx%d, mips=%d)", filename, width, height, mipLevels);
    return true;
}

// Рассчет количества mip levels
u32 CVulkanTexture::CalculateMipLevels(u32 width, u32 height)
{
    u32 mipLevels = 1;
    u32 maxDim = (width > height) ? width : height;
    while (maxDim > 1) {
        maxDim >>= 1;
        mipLevels++;
    }
    return mipLevels;
}

// Проверка compressed формата
bool CVulkanTexture::IsCompressedFormat(VkFormat format)
{
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

// Размер блока для compressed формата
u32 CVulkanTexture::GetBlockSize(VkFormat format)
{
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 8;  // 8 bytes per 4x4 block
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return 16; // 16 bytes per 4x4 block
        default:
            return 0;
    }
}

} // namespace VK
