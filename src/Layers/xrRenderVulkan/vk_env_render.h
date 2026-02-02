// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT
//
// Vulkan environment descriptor render classes.
// Extracted from vk_RenderFactory.cpp so that phase_sky/phase_clouds
// can access mixer texture pointers without duplicating texture loading.

#pragma once

#include "../../Include/xrRender/EnvironmentRender.h"
#include "vk_texture.h"

// ============================================================================
// vkEnvDescriptorRender - per-weather-descriptor texture holder
// ============================================================================
class vkEnvDescriptorRender : public IEnvDescriptorRender
{
public:
	VK::CVulkanTexture* sky_texture     = nullptr;  // cubemap
	VK::CVulkanTexture* sky_texture_env = nullptr;  // cubemap #small
	VK::CVulkanTexture* clouds_texture  = nullptr;  // 2D

	void Copy(IEnvDescriptorRender& _in) override
	{
		sky_texture     = nullptr;
		sky_texture_env = nullptr;
		clouds_texture  = nullptr;
	}

	void OnDeviceCreate(CEnvDescriptor& owner) override;
	void OnDeviceDestroy() override;
};

// ============================================================================
// vkEnvDescriptorMixerRender - assembles texture pairs for blending
// ============================================================================
class vkEnvDescriptorMixerRender : public IEnvDescriptorMixerRender
{
public:
	// Texture pairs for blending: slot 0 = from (A), slot 1 = to (B)
	// These are non-owning pointers -- owned by vkEnvDescriptorRender instances
	VK::CVulkanTexture* sky_tex[2]     = { nullptr, nullptr };
	VK::CVulkanTexture* sky_env_tex[2] = { nullptr, nullptr };
	VK::CVulkanTexture* clouds_tex[2]  = { nullptr, nullptr };

	void Copy(IEnvDescriptorMixerRender& _in) override
	{
		vkEnvDescriptorMixerRender& src = *static_cast<vkEnvDescriptorMixerRender*>(&_in);
		for (int i = 0; i < 2; i++) {
			sky_tex[i]     = src.sky_tex[i];
			sky_env_tex[i] = src.sky_env_tex[i];
			clouds_tex[i]  = src.clouds_tex[i];
		}
	}

	void Destroy() override
	{
		Clear();
	}

	void Clear() override
	{
		for (int i = 0; i < 2; i++) {
			sky_tex[i]     = nullptr;
			sky_env_tex[i] = nullptr;
			clouds_tex[i]  = nullptr;
		}
	}

	void lerp(IEnvDescriptorRender* inA, IEnvDescriptorRender* inB) override
	{
		vkEnvDescriptorRender* A = static_cast<vkEnvDescriptorRender*>(inA);
		vkEnvDescriptorRender* B = static_cast<vkEnvDescriptorRender*>(inB);

		sky_tex[0]     = A->sky_texture;
		sky_tex[1]     = B->sky_texture;

		sky_env_tex[0] = A->sky_texture_env;
		sky_env_tex[1] = B->sky_texture_env;

		clouds_tex[0]  = A->clouds_texture;
		clouds_tex[1]  = B->clouds_texture;
	}
};
