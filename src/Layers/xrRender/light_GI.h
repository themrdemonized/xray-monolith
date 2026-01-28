#pragma once

// ============================================================================
// Global Illumination - Stub for Vulkan renderer
// ============================================================================
// This is a stub header to satisfy includes from light.h
// Full implementation exists in renderer-specific directories
// (xrRenderPC_R2, xrRenderPC_R3, xrRenderPC_R4)
//
// For Vulkan, this functionality will be implemented separately if needed
// ============================================================================

class light_indirect
{
public:
	Fvector position;
	Fvector direction;
	Fcolor color;
	float range;

public:
	light_indirect() : range(10.0f)
	{
		position.set(0, 0, 0);
		direction.set(0, 1, 0);
		color.set(1.0f, 1.0f, 1.0f, 1.0f);
	}
	~light_indirect() {}
};
