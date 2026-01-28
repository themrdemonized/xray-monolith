#include "stdafx.h"
#include "vk_descriptors.h"
#include "HW_Vulkan.h"

namespace VK
{

// Constructor
CVulkanDescriptorManager::CVulkanDescriptorManager()
{
    Msg("[Vulkan] CVulkanDescriptorManager::CVulkanDescriptorManager()");
}

// Destructor
CVulkanDescriptorManager::~CVulkanDescriptorManager()
{
    Msg("[Vulkan] CVulkanDescriptorManager::~CVulkanDescriptorManager()");
    Destroy();
}

// Создание descriptor system
void CVulkanDescriptorManager::Create()
{
    if (m_bCreated) {
        Msg("![Vulkan] DescriptorManager already created");
        return;
    }

    Msg("[Vulkan] Creating Descriptor Manager...");

    CreateLayouts();
    CreatePool();

    m_bCreated = true;

    Msg("[Vulkan] Descriptor Manager created successfully");
}

// Уничтожение
void CVulkanDescriptorManager::Destroy()
{
    if (!m_bCreated) {
        return;
    }

    Msg("[Vulkan] Destroying Descriptor Manager...");

    // Destroy pool (автоматически освобождает все allocated sets)
    if (m_Pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(VulkanHW.m_Device, m_Pool, nullptr);
        m_Pool = VK_NULL_HANDLE;
    }

    // Destroy layouts
    if (m_PerFrameLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_PerFrameLayout, nullptr);
        m_PerFrameLayout = VK_NULL_HANDLE;
    }

    if (m_PerMaterialLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_PerMaterialLayout, nullptr);
        m_PerMaterialLayout = VK_NULL_HANDLE;
    }

    if (m_PerObjectLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_PerObjectLayout, nullptr);
        m_PerObjectLayout = VK_NULL_HANDLE;
    }

    if (m_LightingLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_LightingLayout, nullptr);
        m_LightingLayout = VK_NULL_HANDLE;
    }

    m_AllocatedSets = 0;
    m_bCreated = false;

    Msg("[Vulkan] Descriptor Manager destroyed");
}

// Создание layouts
void CVulkanDescriptorManager::CreateLayouts()
{
    Msg("[Vulkan] Creating descriptor set layouts...");

    // ========================================================================
    // Set 0: PerFrame (обновляется каждый кадр)
    // ========================================================================
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        m_PerFrameLayout = CreateLayout(&binding, 1);
        Msg("[Vulkan]   Set 0 (PerFrame): 1 uniform buffer");
    }

    // ========================================================================
    // Set 1: PerMaterial (текстуры материала)
    // ========================================================================
    {
        // PBR материал может использовать до 8 текстур:
        // 0: Albedo/Diffuse
        // 1: Normal map
        // 2: Roughness
        // 3: Metallic
        // 4: AO (Ambient Occlusion)
        // 5: Emissive
        // 6: Height (для parallax)
        // 7: SSS (Subsurface Scattering)

        VkDescriptorSetLayoutBinding bindings[8] = {};
        for (u32 i = 0; i < 8; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        m_PerMaterialLayout = CreateLayout(bindings, 8);
        Msg("[Vulkan]   Set 1 (PerMaterial): 8 texture samplers");
    }

    // ========================================================================
    // Set 2: PerObject (world matrix, каждый объект)
    // ========================================================================
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        m_PerObjectLayout = CreateLayout(&binding, 1);
        Msg("[Vulkan]   Set 2 (PerObject): 1 uniform buffer");
    }

    // ========================================================================
    // Set 3: Lighting (light data)
    // ========================================================================
    {
        // Lighting set содержит:
        // - binding 0: Light data buffer (все lights в сцене)
        // - binding 1: Shadow maps (array of textures)

        VkDescriptorSetLayoutBinding bindings[2] = {};

        // Light data buffer
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Shadow maps (до 16 shadow maps)
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 16;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_LightingLayout = CreateLayout(bindings, 2);
        Msg("[Vulkan]   Set 3 (Lighting): 1 uniform buffer + 16 shadow maps");
    }

    Msg("[Vulkan] Descriptor set layouts created");
}

// Создание pool
void CVulkanDescriptorManager::CreatePool()
{
    Msg("[Vulkan] Creating descriptor pool...");

    // Подсчитываем количество каждого типа descriptor
    // Предполагаем максимум:
    // - 100 PerFrame sets (для multi-frame в полёте)
    // - 1000 PerMaterial sets (много материалов)
    // - 10000 PerObject sets (много объектов)
    // - 100 Lighting sets

    VkDescriptorPoolSize poolSizes[2] = {};

    // Uniform buffers: 100 + 10000 + 100 = 10200 (PerFrame + PerObject + Lighting)
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 10200;

    // Combined image samplers: 1000*8 + 100*16 = 9600 (PerMaterial + Lighting shadow maps)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 9600;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Позволяет vkFreeDescriptorSets
    poolInfo.maxSets = 11200;  // 100 + 1000 + 10000 + 100
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    VK_CHECK(vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_Pool));

    Msg("[Vulkan] Descriptor pool created (max sets: %u)", poolInfo.maxSets);
    Msg("[Vulkan]   - Uniform buffers: %u", poolSizes[0].descriptorCount);
    Msg("[Vulkan]   - Texture samplers: %u", poolSizes[1].descriptorCount);
}

// Helper для создания layout
VkDescriptorSetLayout CVulkanDescriptorManager::CreateLayout(
    const VkDescriptorSetLayoutBinding* bindings, u32 count)
{
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = count;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &layout));

    return layout;
}

// Allocate PerFrame set
VkDescriptorSet CVulkanDescriptorManager::AllocatePerFrame()
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_Pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_PerFrameLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &set);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate PerFrame descriptor set: %d", result);
        return VK_NULL_HANDLE;
    }

    m_AllocatedSets++;
    return set;
}

// Allocate PerMaterial set
VkDescriptorSet CVulkanDescriptorManager::AllocatePerMaterial()
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_Pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_PerMaterialLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &set);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate PerMaterial descriptor set: %d", result);
        return VK_NULL_HANDLE;
    }

    m_AllocatedSets++;
    return set;
}

// Allocate PerObject set
VkDescriptorSet CVulkanDescriptorManager::AllocatePerObject()
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_Pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_PerObjectLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &set);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate PerObject descriptor set: %d", result);
        return VK_NULL_HANDLE;
    }

    m_AllocatedSets++;
    return set;
}

// Allocate Lighting set
VkDescriptorSet CVulkanDescriptorManager::AllocateLighting()
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_Pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_LightingLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &set);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate Lighting descriptor set: %d", result);
        return VK_NULL_HANDLE;
    }

    m_AllocatedSets++;
    return set;
}

// Update buffer binding
void CVulkanDescriptorManager::UpdateBuffer(VkDescriptorSet set, u32 binding,
                                             VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset)
{
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = size;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(VulkanHW.m_Device, 1, &write, 0, nullptr);
}

// Update texture binding
void CVulkanDescriptorManager::UpdateTexture(VkDescriptorSet set, u32 binding,
                                              VkImageView view, VkSampler sampler)
{
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(VulkanHW.m_Device, 1, &write, 0, nullptr);
}

// Update multiple textures
void CVulkanDescriptorManager::UpdateTextures(VkDescriptorSet set, u32 firstBinding,
                                               const VkImageView* views, const VkSampler* samplers, u32 count)
{
    if (count == 0 || count > 16) {
        Msg("![Vulkan] Invalid texture count: %u (max 16)", count);
        return;
    }

    VkDescriptorImageInfo imageInfos[16] = {};
    VkWriteDescriptorSet writes[16] = {};

    for (u32 i = 0; i < count; ++i) {
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView = views[i];
        imageInfos[i].sampler = samplers[i];

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = firstBinding + i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(VulkanHW.m_Device, count, writes, 0, nullptr);
}

// Reset pool
void CVulkanDescriptorManager::ResetPool()
{
    VK_CHECK(vkResetDescriptorPool(VulkanHW.m_Device, m_Pool, 0));
    m_AllocatedSets = 0;
    Msg("[Vulkan] Descriptor pool reset");
}

} // namespace VK

// Глобальный экземпляр
VK::CVulkanDescriptorManager* g_DescriptorManager = nullptr;
