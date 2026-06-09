////////////////////////////////////////////////////////////////////////////
//	Module 		: sight_manager_target.cpp
//	Created 	: 27.12.2003
//  Modified 	: 08.04.2008
//	Author		: Dmitriy Iassenev
//	Description : sight manager target functions
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "sight_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "stalker_movement_manager_smart_cover.h"
#include "level_graph.h"
#include "ai_space.h"
#include "ai/stalker/ai_stalker_space.h"
#include "detail_path_manager.h"
#include "actor.h"
#include "weapon.h"
#include "CharacterPhysicsSupport.h"
#include "../xrEngine/CameraBase.h"

namespace
{
bool get_object_velocity(Fvector& velocity, const CGameObject* object)
{
	velocity.set(0.f, 0.f, 0.f);

	if (CActor* actor = smart_cast<CActor*>(const_cast<CGameObject*>(object)))
	{
		if (actor->character_physics_support() && actor->character_physics_support()->movement())
		{
			velocity = actor->character_physics_support()->movement()->GetVelocity();
			return !fis_zero(velocity.square_magnitude());
		}
	}
	else if (CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(const_cast<CGameObject*>(object)))
	{
		if (stalker->character_physics_support() && stalker->character_physics_support()->movement())
		{
			velocity = stalker->character_physics_support()->movement()->GetVelocity();
			return !fis_zero(velocity.square_magnitude());
		}
	}
	else if (object->ps_Size() >= 2)
	{
		const Fvector& prev = object->ps_Element(object->ps_Size() - 2).vPosition;
		const Fvector& curr = object->ps_Element(object->ps_Size() - 1).vPosition;
		velocity.sub(curr, prev);
		if (Device.fTimeDelta > EPS_L)
			velocity.div(Device.fTimeDelta);
		return !fis_zero(velocity.square_magnitude());
	}

	return false;
}

float compute_lead_time(Fvector const& to_target, Fvector const& target_velocity, float bullet_speed)
{
	const float a = target_velocity.square_magnitude() - bullet_speed * bullet_speed;
	const float b = 2.f * to_target.dotproduct(target_velocity);
	const float c = to_target.square_magnitude();

	if (c < EPS_L)
		return 0.f;

	if (fis_zero(a, EPS_L))
	{
		if (fis_zero(b, EPS_L))
			return _sqrt(c) / bullet_speed;

		const float t = -c / b;
		return t > 0.f ? t : _sqrt(c) / bullet_speed;
	}

	const float discriminant = b * b - 4.f * a * c;
	if (discriminant < 0.f)
		return _sqrt(c) / bullet_speed;

	const float sqrt_d = _sqrt(discriminant);
	const float inv_2a = 0.5f / a;
	float lead_time = flt_max;

	const float t1 = (-b - sqrt_d) * inv_2a;
	const float t2 = (-b + sqrt_d) * inv_2a;

	if (t1 > EPS_L)
		lead_time = t1;
	if (t2 > EPS_L && t2 < lead_time)
		lead_time = t2;

	if (lead_time == flt_max || lead_time <= 0.f)
		lead_time = _sqrt(c) / bullet_speed;

	return lead_time;
}

void apply_velocity_leading(Fvector& target_pos, Fvector const& my_position, const CGameObject* target,
                            CAI_Stalker& shooter)
{
	Fvector target_velocity;
	if (!get_object_velocity(target_velocity, target))
		return;

	float bullet_speed = 1000.f;
	if (CWeapon* weapon = smart_cast<CWeapon*>(shooter.inventory().ActiveItem()))
	{
		LPCSTR const sect = *weapon->cNameSect();
		if (pSettings->line_exist(sect, "bullet_speed"))
			bullet_speed = pSettings->r_float(sect, "bullet_speed");
	}

	if (bullet_speed < EPS_L)
		return;

	Fvector to_target;
	to_target.sub(target_pos, my_position);

	const float lead_time = compute_lead_time(to_target, target_velocity, bullet_speed);
	if (lead_time <= EPS_L)
		return;

	Fvector lead_offset;
	lead_offset.mul(target_velocity, lead_time);
	target_pos.add(lead_offset);
}
} // namespace

void CSightManager::SetPointLookAngles(const Fvector& tPosition, float& yaw, float& pitch, Fvector const& look_position,
                                       const CGameObject* object)
{
	Fvector my_position = look_position;
	Fvector target = tPosition;
	if (!aim_target(my_position, target, object))
	{
		target = tPosition;
		my_position = look_position;
	}

	target.sub(my_position);
	target.getHP(yaw, pitch);

	VERIFY(_valid(yaw));
	yaw *= -1;

	VERIFY(_valid(pitch));
	pitch *= -1;
}


void aim_target(shared_str const& aim_bone_id, Fvector& result, const CGameObject* object);


bool CSightManager::aim_target(Fvector& my_position, Fvector& aim_target, const CGameObject* object) const
{
	if (!object)
		return (false);

	if (m_object->aim_bone_id().size())
	{
		m_object->aim_target(aim_target, object);
		return (true);
	}

	CGameObject* GO = const_cast<CGameObject*>(object);

	if (GO && GO->cast_entity() && GO->cast_entity()->g_Alive() && (GO->cast_actor() || GO->cast_stalker()))
	{
		if (GO->cast_actor() && GO->cast_actor()->HUDview())
			aim_target = GO->cast_actor()->cam_Active()->vPosition;
		else
			::aim_target("bip01_spine1", aim_target, object);
		return (true);
	}

	if (!object->use_center_to_aim())
		return (false);

	if (GO->cast_actor())
		m_object->Visual()->dcast_PKinematics()->CalculateBBox(FALSE);

	m_object->Center(my_position);
#if 1
	//. hack is here, just because our actor model is animated with 20cm shift
	m_object->XFORM().transform_tiny(
		my_position,
		Fvector().set(
			.2f,
			my_position.y - m_object->Position().y,
			0.f
		)
	);
#else
	const CEntityAlive			*entity_alive = smart_cast<const CEntityAlive*>(object);
	if (!entity_alive || entity_alive->g_Alive()) {
		aim_target.x			= m_object->Position().x;
		aim_target.z			= m_object->Position().z;
	}
#endif

	return (true);
}

void CSightManager::SetFirePointLookAngles(const Fvector& tPosition, float& yaw, float& pitch,
                                           Fvector const& look_position, const CGameObject* object)
{
	Fvector my_position = look_position;
	Fvector target = tPosition;
	if (!aim_target(my_position, target, object))
	{
		target = tPosition;
		my_position = look_position;
	}
	else if (object)
	{
		apply_velocity_leading(target, my_position, object, this->object());
	}

	target.sub(my_position);
	if (fis_zero(target.square_magnitude()))
		target.set(0.f, 0.f, 1.f);

	target.getHP(yaw, pitch);
	VERIFY(_valid(yaw));
	VERIFY(_valid(pitch));
	yaw *= -1;
	pitch *= -1;
}

void CSightManager::SetDirectionLook()
{
	//	MonsterSpace::SBoneRotation				orientation = object().movement().m_head, body_orientation = object().movement().body_orientation();
	//	orientation.target						= orientation.current;
	//	body_orientation.target					= body_orientation.current;
	if (GetDirectionAngles(object().movement().m_head.target.yaw, object().movement().m_head.target.pitch))
	{
		object().movement().m_head.target.yaw *= -1;
		object().movement().m_head.target.pitch *= 0; //-1;
	}
	else
		object().movement().m_head.target = object().movement().m_head.current;
	object().movement().m_body.target = object().movement().m_head.target;
}

void CSightManager::SetLessCoverLook(const CLevelGraph::CVertex* tpNode, bool bDifferenceLook)
{
	SetDirectionLook();

	if (m_object->movement().detail().path().empty())
		return;

	SetLessCoverLook(tpNode, MAX_HEAD_TURN_ANGLE, bDifferenceLook);
}

void CSightManager::SetLessCoverLook(const CLevelGraph::CVertex* tpNode, float fMaxHeadTurnAngle, bool bDifferenceLook)
{
	float fAngleOfView, range, fMaxSquare = -1.f, fBestAngle = object().movement().m_head.target.yaw;
	m_object->update_range_fov(range, fAngleOfView, m_object->eye_range, m_object->eye_fov);
	fAngleOfView = (fAngleOfView / 180.f * PI) / 2.f;

	CLevelGraph::CVertex* tpNextNode = 0;
	u32 node_id;
	bool bOk = false;
	if (bDifferenceLook && !m_object->movement().detail().path().empty() && (m_object->movement().detail().path().size()
		- 1 > m_object->movement().detail().curr_travel_point_index()))
	{
		CLevelGraph::const_iterator i, e;
		ai().level_graph().begin(tpNode, i, e);
		for (; i != e; ++i)
		{
			node_id = ai().level_graph().value(tpNode, i);
			if (!ai().level_graph().valid_vertex_id(node_id))
				continue;
			tpNextNode = ai().level_graph().vertex(node_id);
			if (ai().level_graph().inside(
				tpNextNode,
				m_object->movement().detail().path()[m_object->movement().detail().curr_travel_point_index() +
					1].position))
			{
				bOk = true;
				break;
			}
		}
	}

	if (!bDifferenceLook || !bOk)
		for (float fIncrement = object().movement().m_body.target.yaw - fMaxHeadTurnAngle; fIncrement <= object()
		                                                                                                 .movement().
		                                                                                                 m_body.target.
		                                                                                                 yaw +
		     fMaxHeadTurnAngle; fIncrement += fMaxHeadTurnAngle / 18.f)
		{
			float fSquare = ai().level_graph().compute_high_square(-fIncrement, fAngleOfView, tpNode);
			if (fSquare > fMaxSquare)
			{
				fMaxSquare = fSquare;
				fBestAngle = fIncrement;
			}
		}
	else
	{
		float fMaxSquareSingle = -1.f, fSingleIncrement = object().movement().m_head.target.yaw;
		for (float fIncrement = object().movement().m_body.target.yaw - fMaxHeadTurnAngle; fIncrement <= object()
		                                                                                                 .movement().
		                                                                                                 m_body.target.
		                                                                                                 yaw +
		     fMaxHeadTurnAngle; fIncrement += 2 * fMaxHeadTurnAngle / 60.f)
		{
			float fSquare0 = ai().level_graph().compute_high_square(-fIncrement, fAngleOfView, tpNode);
			float fSquare1 = ai().level_graph().compute_high_square(-fIncrement, fAngleOfView, tpNextNode);
			if (
				(fSquare1 - fSquare0 > fMaxSquare) ||
				(
					fsimilar(fSquare1 - fSquare0, fMaxSquare, EPS_L) &&
					(_abs(fIncrement - object().movement().m_body.target.yaw) < _abs(
						fBestAngle - object().movement().m_body.target.yaw))
				)
			)
			{
				fMaxSquare = fSquare1 - fSquare0;
				fBestAngle = fIncrement;
			}

			if (fSquare0 > fMaxSquareSingle)
			{
				fMaxSquareSingle = fSquare0;
				fSingleIncrement = fIncrement;
			}
		}
		if (_sqrt(fMaxSquare) < 0 * PI_DIV_6)
			fBestAngle = fSingleIncrement;
	}

	object().movement().m_head.target.yaw = angle_normalize_signed(fBestAngle);
	object().movement().m_head.target.pitch = 0;
	VERIFY(_valid(object().movement().m_head.target.yaw));
}


bool CSightManager::GetDirectionAngles(float& yaw, float& pitch)
{
	if (!object().movement().path().empty() && (m_object->movement().detail().curr_travel_point_index() + 1 < m_object
	                                                                                                          ->
	                                                                                                          movement()
	                                                                                                          .detail().
	                                                                                                          path().
	                                                                                                          size()))
	{
		Fvector t;
		t.sub(
			object().movement().path()[m_object->movement().detail().curr_travel_point_index() + 1].position,
			object().movement().path()[m_object->movement().detail().curr_travel_point_index()].position
		);
		t.getHP(yaw, pitch);
		return (true);
	}
	return (GetDirectionAnglesByPrevPositions(yaw, pitch));
};

bool CSightManager::GetDirectionAnglesByPrevPositions(float& yaw, float& pitch)
{
	Fvector tDirection;
	int i = m_object->ps_Size();

	if (i < 2)
		return (false);

	CObject::SavedPosition tPreviousPosition = m_object->ps_Element(i - 2), tCurrentPosition = m_object->
		                       ps_Element(i - 1);
	VERIFY(_valid(tPreviousPosition.vPosition));
	VERIFY(_valid(tCurrentPosition.vPosition));
	tDirection.sub(tCurrentPosition.vPosition, tPreviousPosition.vPosition);
	if (tDirection.magnitude() < EPS_L) return (false);
	tDirection.getHP(yaw, pitch);
	VERIFY(_valid(yaw));
	VERIFY(_valid(pitch));

	return (true);
}
