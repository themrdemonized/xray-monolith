#pragma once
class CBlend;
class ENGINE_API motion_marks;
class IKinematicsAnimated;

extern BOOL print_ik_warnings;

struct ik_anim_error
{
	const char* reason = nullptr;
	bool blend_in_pool = false;
	const char* animation = "<unavailable>";
	const char* animation_set = "<unavailable>";
	u16 limb = u16(-1);
};

class ik_anim_state
{
	bool is_step;
	bool do_glue;
	bool is_idle;
	bool is_blending;
	const CBlend* current_blend;
	u32 current_blend_motion = u32(-1);
public:
	ik_anim_state(): is_step(false), do_glue(true), is_idle(false), is_blending(false), current_blend(0)
	{
	};
	bool update(IKinematicsAnimated* K, const CBlend* b, u16 interval, ik_anim_error& error);
	IC bool step() const { return is_step; }
	IC bool glue() const { return do_glue; }
	IC bool idle() const { return is_idle; }
	IC bool blending() const { return is_blending; }
	IC bool auto_unstuck() const { return true /*|| idle() || blending()*/; }
	static bool time_step_begin(IKinematicsAnimated* K, const CBlend& B, u16 limb_id, float& time);
};
