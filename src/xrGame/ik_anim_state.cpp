#include	"stdafx.h"

#include	"ik_anim_state.h"

//#include	"../Include/xrRender/KinematicsAnimated.h"
#include	"../Include/xrRender/KinematicsAnimated.h"
#include	"../xrEngine/motion.h"

BOOL print_ik_warnings = FALSE;

namespace
{
const CMotionDef* motion_def(IKinematicsAnimated* K, MotionID id)
{
	if (!id.valid() || id.slot >= K->LL_MotionsSlotCount())
		return nullptr;
	const auto& definitions = *K->LL_MotionsSlot(id.slot).motion_defs();
	return id.idx < definitions.size() ? &definitions[id.idx] : nullptr;
}

void describe_error(IKinematicsAnimated* K, const CBlend& blend, u16 limb, const char* reason,
	ik_anim_error& error)
{
	error.reason = reason;
	error.blend_in_pool = true;
	error.limb = limb;
	if (!print_ik_warnings)
		return;
	if (blend.motionID.slot < K->LL_MotionsSlotCount())
	{
		const auto& motions = K->LL_MotionsSlot(blend.motionID.slot);
		error.animation_set = motions.id().c_str();
		if (motion_def(K, blend.motionID))
		{
			for (const auto& motion : *motions.motion_map())
				if (motion.second == blend.motionID.idx)
				{
					error.animation = motion.first.c_str();
					break;
				}
		}
	}
}

const CMotionDef* validate_blend(IKinematicsAnimated* K, const CBlend& blend, u16 limb, ik_anim_error& error)
{
	const char* reason = nullptr;
	const CMotionDef* motion = motion_def(K, blend.motionID);
	if (blend.blend_state() != CBlend::eAccrue && blend.blend_state() != CBlend::eFalloff)
		reason = "invalid blend state";
	else if (!motion)
		reason = "invalid motion ID";
	else if (!_valid(blend.timeTotal) || blend.timeTotal <= 0.f || fis_zero(blend.timeTotal) ||
		!_valid(blend.timeCurrent) || blend.timeCurrent < 0.f)
		reason = "invalid blend timing";
	else if (!_valid(blend.blendAmount) || blend.blendAmount < 0.f ||
		!_valid(blend.blendPower) || blend.blendPower < 0.f || !_valid(blend.speed))
		reason = "invalid blend weight or speed";
	else if (limb < motion->marks.size())
	{
		for (const auto& interval : motion->marks[limb].intervals)
			// Finite endpoints are safe to evaluate, including negative or reversed intervals.
			if (!_valid(interval.first) || !_valid(interval.second))
			{
				reason = "invalid foot-contact interval";
				break;
			}
	}

	if (reason)
	{
		describe_error(K, blend, limb, reason, error);
		return nullptr;
	}
	return motion;
}
}

IC bool is_in(const motion_marks::interval& i, float v)
{
	if (i.first < i.second)
		return i.first < v && i.second > v;
	else
		return i.first < v || i.second > v;
}

float blend_time(const CBlend& b)
{
	VERIFY(b.timeTotal > 0.f);
	VERIFY(b.timeCurrent >= 0.f);
	VERIFY(!fis_zero( b.timeTotal ));

	// Double precision avoids overflowing timeCurrent / timeTotal for finite inputs.
	float t = float(fmod(double(b.timeCurrent), double(b.timeTotal)));

	VERIFY(t <= b.timeTotal);
	VERIFY(t >= 0.f);

	return t;
}

float time_to_next_mark(const CBlend& b, const motion_marks& marks)
{
	VERIFY(!marks.is_empty());
	const float l_blend_time = blend_time(b);
	float time = marks.time_to_next_mark(l_blend_time);
	if (time < FLT_MAX)
		return time;
	time = marks.time_to_next_mark(EPS_S);
	if (time < FLT_MAX)
		return time + b.timeTotal - l_blend_time;
	return b.timeTotal - l_blend_time;
}

bool blend_in(const CBlend& b, const motion_marks& marks)
{
	return NULL != marks.pick_mark(blend_time(b));
}

IC bool b_is_blending(const CBlend* current_blend, const CBlend* b)
{
	return current_blend &&
		current_blend->blend_state() != CBlend::eFREE_SLOT &&
		current_blend != b &&
		b->blendAmount < b->blendPower - EPS_L;
}

bool ik_anim_state::update(IKinematicsAnimated* K, const CBlend* b, u16 i, ik_anim_error& error)
{
	//Andy	is_step = m && b && blend_in( *b, m->get_interval( i ) );
	error = ik_anim_error();
	is_step = false;
	is_idle = false;
	do_glue = false;
	is_blending = false;
	if (!K)
	{
		error.reason = "missing animated skeleton";
		error.limb = i;
		current_blend = nullptr;
		return false;
	}
	if (!b)
	{
		current_blend = 0;
		return true;
	}
	// Pool membership must be checked before dereferencing a cached IK pointer.
	if (!K->LL_IsBlendInPool(b))
	{
		error.reason = "leg blend does not belong to this skeleton";
		error.limb = i;
		current_blend = nullptr;
		return false;
	}
	// Previous blends normally expire; do not dereference an expired cached pointer.
	if (current_blend && (!K->LL_IsBlendInPool(current_blend) ||
		current_blend->blend_state() == CBlend::eFREE_SLOT ||
		current_blend->motionID.val != current_blend_motion))
		current_blend = nullptr;

	const CMotionDef* next = validate_blend(K, *b, i, error);
	if (!next)
		return false;
	const CMotionDef& m_def_new = *next;

	if (m_def_new.marks.size() <= i)
	{
		current_blend = nullptr;
		return true;
	}

	if (b_is_blending(current_blend, b))
	{
		is_blending = true;
		const CMotionDef* previous = validate_blend(K, *current_blend, i, error);
		if (!previous)
			return false;
		const CMotionDef& m_def_cur = *previous;
		bool is_cur_step = (m_def_cur.marks.size() > i) && blend_in(*current_blend, m_def_cur.marks[i]);
		is_idle = !!(m_def_new.flags & esmIdle) && !!(m_def_cur.flags & esmIdle);
		bool any_idle = (m_def_cur.flags & esmIdle) ||
			(m_def_new.flags & esmIdle);
		bool is_new_step = blend_in(*b, m_def_new.marks[i]);
		bool any_step = is_cur_step || is_new_step;
		bool step_all = is_cur_step && is_new_step;

		//is_step = step_all || any_idle && is_cur_step || is_new_step;

		do_glue = step_all || any_idle && is_new_step;
		is_step = (!any_idle && any_step) || any_idle && do_glue;
		//do_glue =true;
	}
	else
	{
		is_step = blend_in(*b, m_def_new.marks[i]);
		current_blend = b;
		current_blend_motion = b->motionID.val;
		is_idle = !!(m_def_new.flags & esmIdle);
		do_glue = true;
	}
	return true;
}

bool ik_anim_state::time_step_begin(IKinematicsAnimated* K, const CBlend& B, u16 limb_id, float& time)
{
	time = 0;
	if (!K)
		return false;
	ik_anim_error error;
	const CMotionDef* motion = validate_blend(K, B, limb_id, error);
	if (!motion)
		return false;
	const CMotionDef& m_def_cur = *motion;
	if (m_def_cur.marks.size() <= limb_id || !!(m_def_cur.flags & esmIdle))
		return false;
	const motion_marks& marks = m_def_cur.marks[limb_id];
	if (marks.is_empty())
		return false;
	//if( blend_in( *current_blend, marks ) )
	//	time = 0;
	time = time_to_next_mark(B, marks);
	return _valid(time) && time >= 0.f && time < FLT_MAX;
}
