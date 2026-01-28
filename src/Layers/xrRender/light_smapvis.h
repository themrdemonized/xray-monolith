#pragma once

// ============================================================================
// Shadow Map Visibility - Stub for Vulkan renderer
// ============================================================================
// This is a stub header to satisfy includes from light.h
// Full implementation exists in renderer-specific directories
// (xrRenderPC_R2, xrRenderPC_R3, xrRenderPC_R4)
//
// For Vulkan, this functionality will be implemented separately if needed
// ============================================================================

#include "r__dsgraph_structure.h"

class smapvis : public R_feedback
{
public:
	enum
	{
		state_counting = 0,
		state_working = 1,
		state_usingTC = 3,
	} state;

	xr_vector<IRenderVisual*> invisible;

	u32 frame_sleep;
	u32 test_count;
	u32 test_current;
	IRenderVisual* testQ_V;
	u32 testQ_id;
	u32 testQ_frame;

public:
	smapvis() : state(state_counting), frame_sleep(0), test_count(0),
	            test_current(0), testQ_V(nullptr), testQ_id(0), testQ_frame(0) {}
	virtual ~smapvis() {}

	void invalidate() {}
	void begin() {}
	void end() {}
	void mark() {}
	void flushoccq() {}
	void resetoccq() {}

	inline bool sleep() { return Device.dwFrame > frame_sleep; }

	virtual void rfeedback_static(dxRender_Visual* V) {}
};
