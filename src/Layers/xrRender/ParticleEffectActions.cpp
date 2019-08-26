//---------------------------------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#define AnsiString std::string

#include "ParticleEffectActions.h"
//#include "../xrServerEntities/PropertiesListHelper.h"
#include "../xrParticles/particle_actions_collection.h"

using namespace PAPI;
#define PARTICLE_ACTION_VERSION		0x0001
//---------------------------------------------------------------------------
xr_token2 actions_token[] = {
	{"Avoid", "Steer particles away from a domain of space.", PAAvoidID},
	{"Bounce", "Bounce particles off a domain of space.", PABounceID},
	{"Copy VertexB", "Set the secondary position from current position.", PACopyVertexBID},
	{"Damping", "Simulate air by slowing down particle velocities.", PADampingID},
	{"Explosion", "An Explosion.", PAExplosionID},
	{"Follow", "Accelerate toward the next particle in the group.", PAFollowID},
	{"Gravitate", "Accelerate each particle toward each other particle.", PAGravitateID},
	{"Gravity", "Accelerate particles in the given direction.", PAGravityID},
	{"Jet", "Accelerate particles that are near the center of the jet.", PAJetID},
	{"Kill Old", "Remove old particles.", PAKillOldID},
	{"Match Velocity", "Modify each particle’s velocity to be similar to that of its neighbors.", PAMatchVelocityID},
	{"Move", "Move particle positions based on velocities.", PAMoveID},
	{"Orbit Line", "Accelerate toward the closest point on the given line.", PAOrbitLineID},
	{"Orbit Point", "Accelerate toward the given center point.", PAOrbitPointID},
	{"Random Accel", "Accelerate particles in random directions.", PARandomAccelID},
	{"Random Displace", "Immediately replace position with a position from the domain.", PARandomDisplaceID},
	{"Random Velocity", "Immediately replace velocity with a velocity from the domain.", PARandomVelocityID},
	{"Restore", "Over time, restore particles to their secondary positions.", PARestoreID},
	{"Scatter", "Scatter particles from center.", PAScatterID},
	{"Sink", "Kill particles with positions on wrong side of the specified domain.", PASinkID},
	{"Sink Velocity", "Kill particles with velocities on wrong side of the specified domain.", PASinkVelocityID},
	{"Source", "Add particles in the specified domain.", PASourceID},
	{"Speed Limit", "Clamp each particle’s speed to the given min and max.", PASpeedLimitID},
	{"Target Color", "Change color of all particles toward the specified color.", PATargetColorID},
	{"Target Size", "Change sizes of all particles toward the specified size.", PATargetSizeID},
	{"Target Rotate", "Change rotate of all particles toward the specified rotation.", PATargetRotateID},
	{"Target Velocity", "Change velocity of all particles toward the specified velocity.", PATargetVelocityID},
	{"Vortex", "Swirl particles around a vortex.", PAVortexID},
	{"Turbulence", "A Turbulence.", PATurbulenceID},
	{0, 0}
};


EParticleAction* pCreateEActionImpl(PAPI::PActionEnum type)
{
	EParticleAction* pa = 0;
	switch (type)
	{
	case PAPI::PAAvoidID: pa = xr_new<EPAAvoid>();
		break;
	case PAPI::PABounceID: pa = xr_new<EPABounce>();
		break;
	case PAPI::PACopyVertexBID: pa = xr_new<EPACopyVertexB>();
		break;
	case PAPI::PADampingID: pa = xr_new<EPADamping>();
		break;
	case PAPI::PAExplosionID: pa = xr_new<EPAExplosion>();
		break;
	case PAPI::PAFollowID: pa = xr_new<EPAFollow>();
		break;
	case PAPI::PAGravitateID: pa = xr_new<EPAGravitate>();
		break;
	case PAPI::PAGravityID: pa = xr_new<EPAGravity>();
		break;
	case PAPI::PAJetID: pa = xr_new<EPAJet>();
		break;
	case PAPI::PAKillOldID: pa = xr_new<EPAKillOld>();
		break;
	case PAPI::PAMatchVelocityID: pa = xr_new<EPAMatchVelocity>();
		break;
	case PAPI::PAMoveID: pa = xr_new<EPAMove>();
		break;
	case PAPI::PAOrbitLineID: pa = xr_new<EPAOrbitLine>();
		break;
	case PAPI::PAOrbitPointID: pa = xr_new<EPAOrbitPoint>();
		break;
	case PAPI::PARandomAccelID: pa = xr_new<EPARandomAccel>();
		break;
	case PAPI::PARandomDisplaceID: pa = xr_new<EPARandomDisplace>();
		break;
	case PAPI::PARandomVelocityID: pa = xr_new<EPARandomVelocity>();
		break;
	case PAPI::PARestoreID: pa = xr_new<EPARestore>();
		break;
	case PAPI::PAScatterID: pa = xr_new<EPAScatter>();
		break;
	case PAPI::PASinkID: pa = xr_new<EPASink>();
		break;
	case PAPI::PASinkVelocityID: pa = xr_new<EPASinkVelocity>();
		break;
	case PAPI::PASourceID: pa = xr_new<EPASource>();
		break;
	case PAPI::PASpeedLimitID: pa = xr_new<EPASpeedLimit>();
		break;
	case PAPI::PATargetColorID: pa = xr_new<EPATargetColor>();
		break;
	case PAPI::PATargetSizeID: pa = xr_new<EPATargetSize>();
		break;
	case PAPI::PATargetRotateID: pa = xr_new<EPATargetRotate>();
		break;
	case PAPI::PATargetRotateDID: pa = xr_new<EPATargetRotate>();
		break;
	case PAPI::PATargetVelocityID: pa = xr_new<EPATargetVelocity>();
		break;
	case PAPI::PATargetVelocityDID: pa = xr_new<EPATargetVelocity>();
		break;
	case PAPI::PAVortexID: pa = xr_new<EPAVortex>();
		break;
	case PAPI::PATurbulenceID: pa = xr_new<EPATurbulence>();
		break;
	default: NODEFAULT;
	}
	pa->type = type;
	return pa;
}

_CreateEAction pCreateEAction = pCreateEActionImpl;

//---------------------------------------------------------------------------
bool EParticleAction::Load(IReader& F)
{
	volatile u32 vers = F.r_u32();
	if (vers != PARTICLE_ACTION_VERSION)
		return false;
	R_ASSERT(vers == PARTICLE_ACTION_VERSION);
	F.r_stringZ(actionName);
	flags.assign(F.r_u32());
	for (PFloatMapIt f_it = floats.begin(); f_it != floats.end(); f_it++) f_it->second.val = F.r_float();
	for (PVectorMapIt v_it = vectors.begin(); v_it != vectors.end(); v_it++) F.r_fvector3(v_it->second.val);
	for (PDomainMapIt d_it = domains.begin(); d_it != domains.end(); d_it++) d_it->second.Load(F);
	for (PBoolMapIt b_it = bools.begin(); b_it != bools.end(); b_it++) b_it->second.val = F.r_u8();
	for (PIntMapIt i_it = ints.begin(); i_it != ints.end(); i_it++) i_it->second.val = F.r_s32();
	return true;
}

void EParticleAction::Load2(CInifile& ini, const shared_str& sect)
{
	u32 ver = ini.r_u32(sect.c_str(), "version");
	actionName = ini.r_string(sect.c_str(), "action_name");
	flags.assign(ini.r_u32(sect.c_str(), "flags"));

	u32 counter = 0;
	string256 buff;
	for (PFloatMapIt f_it = floats.begin(); f_it != floats.end(); ++f_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "flt_%04d", counter);
		if (ver == 0)
		{
			if (ini.line_exist(sect.c_str(), buff))
				f_it->second.val = ini.r_float(sect.c_str(), buff);
		}
		else
			f_it->second.val = ini.r_float(sect.c_str(), buff);
	}
	counter = 0;
	for (PVectorMapIt v_it = vectors.begin(); v_it != vectors.end(); ++v_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "vec_%04d", counter);
		v_it->second.val = ini.r_fvector3(sect.c_str(), buff);
	}

	counter = 0;
	for (PDomainMapIt d_it = domains.begin(); d_it != domains.end(); ++d_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "domain_%s_%04d", sect.c_str(), counter);
		d_it->second.Load2(ini, buff);
	}

	counter = 0;
	for (PBoolMapIt b_it = bools.begin(); b_it != bools.end(); ++b_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "bool_%04d", counter);
		b_it->second.val = ini.r_bool(sect.c_str(), buff);
	}

	counter = 0;
	for (PIntMapIt i_it = ints.begin(); i_it != ints.end(); ++i_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "int_%04d", counter);
		i_it->second.val = ini.r_s32(sect.c_str(), buff);
	}
}

void EParticleAction::Save(IWriter& F)
{
	F.w_u32(PARTICLE_ACTION_VERSION);
	F.w_stringZ(actionName);
	F.w_u32(flags.get());
	for (PFloatMapIt f_it = floats.begin(); f_it != floats.end(); f_it++) F.w_float(f_it->second.val);
	for (PVectorMapIt v_it = vectors.begin(); v_it != vectors.end(); v_it++) F.w_fvector3(v_it->second.val);
	for (PDomainMapIt d_it = domains.begin(); d_it != domains.end(); d_it++) d_it->second.Save(F);
	for (PBoolMapIt b_it = bools.begin(); b_it != bools.end(); b_it++) F.w_u8((u8)b_it->second.val);
	for (PIntMapIt i_it = ints.begin(); i_it != ints.end(); i_it++) F.w_s32(i_it->second.val);
}

void EParticleAction::Save2(CInifile& ini, const shared_str& sect)
{
	ini.w_u32(sect.c_str(), "version", PARTICLE_ACTION_VERSION);
	ini.w_string(sect.c_str(), "action_name", actionName.c_str());
	ini.w_u32(sect.c_str(), "flags", flags.get());

	u32 counter = 0;
	string256 buff;
	for (PFloatMapIt f_it = floats.begin(); f_it != floats.end(); ++f_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "flt_%04d", counter);
		ini.w_float(sect.c_str(), buff, f_it->second.val);
	}
	counter = 0;
	for (PVectorMapIt v_it = vectors.begin(); v_it != vectors.end(); ++v_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "vec_%04d", counter);
		ini.w_fvector3(sect.c_str(), buff, v_it->second.val);
	}

	counter = 0;
	for (PDomainMapIt d_it = domains.begin(); d_it != domains.end(); ++d_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "domain_%s_%04d", sect.c_str(), counter);
		d_it->second.Save2(ini, buff);
	}

	counter = 0;
	for (PBoolMapIt b_it = bools.begin(); b_it != bools.end(); ++b_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "bool_%04d", counter);
		ini.w_bool(sect.c_str(), buff, b_it->second.val);
	}

	counter = 0;
	for (PIntMapIt i_it = ints.begin(); i_it != ints.end(); ++i_it, ++counter)
	{
		xr_sprintf(buff, sizeof(buff), "int_%04d", counter);
		ini.w_s32(sect.c_str(), buff, i_it->second.val);
	}
}

/*void 	EParticleAction::FillProp(PropItemVec& items, LPCSTR pref, u32 clr)
{
	PropValue* V = 0;
	for (OrderVecIt o_it = orders.begin(); o_it != orders.end(); o_it++)
	{
		LPCSTR name = o_it->name.c_str();
		switch (o_it->type) {
		case tpDomain:
			domains[o_it->name].FillProp(items, PrepareKey(pref, name).c_str(), clr);
			break;
		case tpVector: {
			PVector& vect = vectors[o_it->name];
			switch (vect.type) {
			case PVector::vNum:
				V = PHelper().CreateVector(items, PrepareKey(pref, name).c_str(), &vect.val, vect.mn, vect.mx, 0.001f, 3);
				break;
			case PVector::vAngle:
				V = PHelper().CreateAngle3(items, PrepareKey(pref, name).c_str(), &vect.val, vect.mn, vect.mx, 0.001f, 3);
				break;
			case PVector::vColor:
				V = PHelper().CreateVColor(items, PrepareKey(pref, name).c_str(), &vect.val);
				break;
			}
		}break;
		case tpFloat: {
			PFloat& flt = floats[o_it->name];
			V = PHelper().CreateFloat(items, PrepareKey(pref, name).c_str(), &flt.val, flt.mn, flt.mx, 0.001f, 3);
		}break;
		case tpInt: {
			PInt& el = ints[o_it->name];
			V = PHelper().CreateS32(items, PrepareKey(pref, name).c_str(), &el.val, el.mn, el.mx);
		}break;
		case tpBool:
			V = PHelper().CreateBOOL(items, PrepareKey(pref, name).c_str(), &bools[o_it->name].val);
			break;
		}
		if (V) V->Owner()->prop_color = clr;
	}
	V = PHelper().CreateFlag32(items, PrepareKey(pref, "Draw").c_str(), &flags, flDraw);
	V->Owner()->prop_color = clr;
	V = PHelper().CreateFlag32(items, PrepareKey(pref, "Enabled").c_str(), &flags, flEnabled);
	V->Owner()->prop_color = clr;
} //*/
void EParticleAction::appendFloat(LPCSTR name, float v, float mn, float mx)
{
	orders.push_back(SOrder(tpFloat, name));
	floats[name] = PFloat(v, mn, mx);
}

void EParticleAction::appendInt(LPCSTR name, int v, int mn, int mx)
{
	orders.push_back(SOrder(tpInt, name));
	ints[name] = PInt(v, mn, mx);
}

void EParticleAction::appendVector(LPCSTR name, PVector::EType type, float vx, float vy, float vz, float mn, float mx)
{
	orders.push_back(SOrder(tpVector, name));
	vectors[name] = PVector(type, Fvector().set(vx, vy, vz), mn, mx);
}

void EParticleAction::appendDomain(LPCSTR name, PDomain v)
{
	orders.push_back(SOrder(tpDomain, name));
	domains[name] = v;
}

void EParticleAction::appendBool(LPCSTR name, BOOL v)
{
	orders.push_back(SOrder(tpBool, name));
	bools[name] = PBool(v);
}

//---------------------------------------------------------------------------
void pAvoid(IWriter& F, float magnitude, float epsilon, float look_ahead, pDomain D, BOOL allow_rotate)
{
	PAAvoid S;
	S.type = PAAvoidID;

	S.positionL = D;
	S.position = S.positionL;
	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.look_ahead = look_ahead;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pBounce(IWriter& F, float friction, float resilience, float cutoff, pDomain D, BOOL allow_rotate)
{
	PABounce S;
	S.type = PABounceID;

	S.positionL = D;
	S.position = S.positionL;
	S.oneMinusFriction = 1.0f - friction;
	S.resilience = resilience;
	S.cutoffSqr = _sqr(cutoff);
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pCopyVertexB(IWriter& F, BOOL copy_pos)
{
	PACopyVertexB S;
	S.type = PACopyVertexBID;

	S.copy_pos = copy_pos;

	F.w_u32(S.type);
	S.Save(F);
}

void pDamping(IWriter& F, const Fvector& damping,
              float vlow, float vhigh)
{
	PADamping S;
	S.type = PADampingID;

	S.damping = pVector(damping.x, damping.y, damping.z);
	S.vlowSqr = _sqr(vlow);
	S.vhighSqr = _sqr(vhigh);

	F.w_u32(S.type);
	S.Save(F);
}

void pExplosion(IWriter& F, const Fvector& center, float velocity,
                float magnitude, float stdev, float epsilon, float age, BOOL allow_rotate)
{
	PAExplosion S;
	S.type = PAExplosionID;

	S.centerL = pVector(center.x, center.y, center.z);
	S.center = S.centerL;
	S.velocity = velocity;
	S.magnitude = magnitude;
	S.stdev = stdev;
	S.epsilon = epsilon;
	S.age = age;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	if (S.epsilon < 0.0f)
		S.epsilon = EPS_L;

	F.w_u32(S.type);
	S.Save(F);
}

void pFollow(IWriter& F, float magnitude, float epsilon, float max_radius)
{
	PAFollow S;
	S.type = PAFollowID;

	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.max_radius = max_radius;

	F.w_u32(S.type);
	S.Save(F);
}

void pGravitate(IWriter& F, float magnitude, float epsilon, float max_radius)
{
	PAGravitate S;
	S.type = PAGravitateID;

	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.max_radius = max_radius;

	F.w_u32(S.type);
	S.Save(F);
}

void pGravity(IWriter& F, const Fvector& dir, BOOL allow_rotate)
{
	PAGravity S;
	S.type = PAGravityID;

	S.directionL = pVector(dir.x, dir.y, dir.z);
	S.direction = S.directionL;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pJet(IWriter& F, pDomain acc, const Fvector& center,
          float magnitude, float epsilon, float max_radius, BOOL allow_rotate)
{
	PAJet S;
	S.type = PAJetID;

	S.centerL = pVector(center.x, center.y, center.z);
	S.center = S.centerL;
	S.accL = acc;
	S.acc = S.accL;
	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.max_radius = max_radius;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pKillOld(IWriter& F, float age_limit, BOOL kill_less_than)
{
	PAKillOld S;
	S.type = PAKillOldID;

	S.age_limit = age_limit;
	S.kill_less_than = kill_less_than;

	F.w_u32(S.type);
	S.Save(F);
}

void pMove(IWriter& F)
{
	PAMove S;
	S.type = PAMoveID;

	F.w_u32(S.type);
	S.Save(F);
}

void pMatchVelocity(IWriter& F, float magnitude, float epsilon, float max_radius)
{
	PAMatchVelocity S;
	S.type = PAMatchVelocityID;

	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.max_radius = max_radius;

	F.w_u32(S.type);
	S.Save(F);
}

void pOrbitLine(IWriter& F, const Fvector& p, const Fvector& axis,
                float magnitude, float epsilon, float max_radius, BOOL allow_rotate)
{
	PAOrbitLine S;
	S.type = PAOrbitLineID;

	S.pL = pVector(p.x, p.y, p.z);
	S.p = S.pL;
	S.axisL = pVector(axis.x, axis.y, axis.z);
	S.axisL.normalize_safe();
	S.axis = S.axisL;
	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.max_radius = max_radius;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pOrbitPoint(IWriter& F, const Fvector& center,
                 float magnitude, float epsilon, float max_radius, BOOL allow_rotate)
{
	PAOrbitPoint S;
	S.type = PAOrbitPointID;

	S.centerL = pVector(center.x, center.y, center.z);
	S.center = S.centerL;
	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.max_radius = max_radius;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pRandomAccel(IWriter& F, pDomain D, BOOL allow_rotate)
{
	PARandomAccel S;
	S.type = PARandomAccelID;

	S.gen_accL = D;
	S.gen_acc = S.gen_accL;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pRandomDisplace(IWriter& F, pDomain D, BOOL allow_rotate)
{
	PARandomDisplace S;
	S.type = PARandomDisplaceID;

	S.gen_dispL = D;
	S.gen_disp = S.gen_dispL;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pRandomVelocity(IWriter& F, pDomain D, BOOL allow_rotate)
{
	PARandomVelocity S;
	S.type = PARandomVelocityID;

	S.gen_velL = D;
	S.gen_vel = S.gen_velL;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pRestore(IWriter& F, float time_left)
{
	PARestore S;
	S.type = PARestoreID;

	S.time_left = time_left;

	F.w_u32(S.type);
	S.Save(F);
}

void pScatter(IWriter& F, const Fvector& center, float magnitude, float epsilon, float max_radius, BOOL allow_rotate)
{
	PAScatter S;
	S.type = PAScatterID;

	S.centerL = pVector(center.x, center.y, center.z);
	S.center = S.centerL;
	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.max_radius = max_radius;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pSink(IWriter& F, BOOL kill_inside, pDomain D, BOOL allow_rotate)
{
	PASink S;
	S.type = PASinkID;

	S.kill_inside = kill_inside;
	S.positionL = D;
	S.position = S.positionL;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pSinkVelocity(IWriter& F, BOOL kill_inside, pDomain D, BOOL allow_rotate)
{
	PASinkVelocity S;
	S.type = PASinkVelocityID;

	S.kill_inside = kill_inside;
	S.velocityL = D;
	S.velocity = S.velocityL;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pSource(IWriter& F, float particle_rate, pDomain pos, pDomain vel, pDomain rot, pDomain size, BOOL single_size,
             pDomain color, float alpha, float age, float age_sigma, float parent_motion,
             BOOL allow_rotate)
{
	PASource S;
	S.type = PASourceID;

	S.particle_rate = particle_rate;
	S.positionL = pos;
	S.position = S.positionL;
	S.velocityL = vel;
	S.velocity = S.velocityL;
	S.size = size;
	S.rot = rot;
	S.color = color;
	S.alpha = alpha;
	S.age = age;
	S.age_sigma = age_sigma;
	S.m_Flags.assign((single_size ? PASource::flSingleSize : 0) | PASource::flVertexB_tracks);
	S.parent_vel = pVector(0, 0, 0);
	S.parent_motion = parent_motion;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pSpeedLimit(IWriter& F, float min_speed, float max_speed)
{
	PASpeedLimit S;
	S.type = PASpeedLimitID;

	S.min_speed = min_speed;
	S.max_speed = max_speed;

	F.w_u32(S.type);
	S.Save(F);
}

void pTargetColor(IWriter& F, const Fvector& color, float alpha, float scale, float time_from, float time_to)
{
	PATargetColor S;
	S.type = PATargetColorID;

	S.color = pVector(color.x, color.y, color.z);
	S.alpha = alpha;
	S.scale = scale;
	S.timeFrom = time_from;
	S.timeTo = time_to;

	F.w_u32(S.type);
	S.Save(F);
}

void pTargetSize(IWriter& F, const Fvector& size, const Fvector& scale)
{
	PATargetSize S;
	S.type = PATargetSizeID;

	S.size = pVector(size.x, size.y, size.z);
	S.scale = pVector(scale.x, scale.y, scale.z);

	F.w_u32(S.type);
	S.Save(F);
}

void pTargetRotate(IWriter& F, const Fvector& rot, float scale)
{
	PATargetRotate S;
	S.type = PATargetRotateID;

	S.rot = pVector(rot.x, rot.y, rot.z);
	S.scale = scale;

	F.w_u32(S.type);
	S.Save(F);
}

void pTargetVelocity(IWriter& F, const Fvector& vel, float scale, BOOL allow_rotate)
{
	PATargetVelocity S;
	S.type = PATargetVelocityID;

	S.velocityL = pVector(vel.x, vel.y, vel.z);
	S.velocity = S.velocityL;
	S.scale = scale;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pVortex(IWriter& F, const Fvector& center, const Fvector& axis,
             float magnitude, float epsilon, float max_radius, BOOL allow_rotate)
{
	PAVortex S;
	S.type = PAVortexID;

	S.centerL = pVector(center.x, center.y, center.z);
	S.center = S.centerL;
	S.axisL = pVector(axis.x, axis.y, axis.z);
	S.axisL.normalize_safe();
	S.axis = S.axisL;
	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.max_radius = max_radius;
	S.m_Flags.set(ParticleAction::ALLOW_ROTATE, allow_rotate);

	F.w_u32(S.type);
	S.Save(F);
}

void pTurbulence(IWriter& F, float freq, int octaves, float magnitude, float epsilon, const Fvector& offs)
{
	PATurbulence S;
	S.type = PATurbulenceID;

	S.frequency = freq;
	S.octaves = octaves;
	S.magnitude = magnitude;
	S.epsilon = epsilon;
	S.offset.set(offs);

	S.age = 0.f;

	F.w_u32(S.type);
	S.Save(F);
}

//------------------------------------------------------------------------------
#define EXPAND_DOMAIN(D)			D.type,\
									D.f[0], D.f[1], D.f[2],\
									D.f[3], D.f[4], D.f[5],\
						            D.f[6], D.f[7], D.f[8]

EPAAvoid::EPAAvoid() : EParticleAction(PAPI::PAAvoidID)
{
	actionType = "Avoid";
	actionName = actionType;
	appendDomain("Position", PDomain(PDomain::vNum, TRUE, 0x6096FF96));
	appendFloat("Magnitude", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Look Ahead", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPAAvoid::Compile(IWriter& F)
{
	pAvoid(F, _float("Magnitude").val, _float("Epsilon").val, _float("Look Ahead").val,
	       pDomain(EXPAND_DOMAIN(_domain("Position"))), _bool("Allow Rotate").val);
}

EPABounce::EPABounce() : EParticleAction(PAPI::PABounceID)
{
	actionType = "Bounce";
	actionName = actionType;
	appendDomain("Position", PDomain(PDomain::vNum, TRUE, 0x6096FEEC));
	appendFloat("Friction", 0.5f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Resilience", 0.1f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Cutoff", 1.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPABounce::Compile(IWriter& F)
{
	pBounce(F, _float("Friction").val, _float("Resilience").val, _float("Cutoff").val,
	        pDomain(EXPAND_DOMAIN(_domain("Position"))), _bool("Allow Rotate").val);
}

EPACopyVertexB::EPACopyVertexB() : EParticleAction(PAPI::PACopyVertexBID)
{
	actionType = "CopyVertexB";
	actionName = actionType;
	appendBool("Copy Position", TRUE);
}

void EPACopyVertexB::Compile(IWriter& F)
{
	pCopyVertexB(F, _bool("Copy Position").val);
}

EPADamping::EPADamping() : EParticleAction(PAPI::PADampingID)
{
	actionType = "Damping";
	actionName = actionType;
	appendVector("Damping", PVector::vNum, 0.f, 0.f, 0.f);
	appendFloat("V Low", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("V High", P_MAXFLOAT, -P_MAXFLOAT, P_MAXFLOAT);
}

void EPADamping::Compile(IWriter& F)
{
	pDamping(F, _vector("Damping").val, _float("V Low").val, _float("V High").val);
}

EPAExplosion::EPAExplosion() : EParticleAction(PAPI::PAExplosionID)
{
	actionType = "Explosion";
	actionName = actionType;
	appendVector("Center", PVector::vNum, 0.f, 0.f, 0.f);
	appendFloat("Velocity", 1.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Magnitude", 2.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Standart Dev", 3.f, EPS, P_MAXFLOAT);
	appendFloat("Epsilon", EPS_L, EPS, P_MAXFLOAT);
	appendFloat("Age", 0.f, 0.f, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPAExplosion::Compile(IWriter& F)
{
	pExplosion(F, _vector("Center").val, _float("Velocity").val, _float("Magnitude").val, _float("Standart Dev").val,
	           _float("Epsilon").val, _float("Age").val, _bool("Allow Rotate").val);
}

EPAFollow::EPAFollow() : EParticleAction(PAPI::PAFollowID)
{
	actionType = "Follow";
	actionName = actionType;
	appendFloat("Magnitude", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", EPS_L, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Radius", P_MAXFLOAT, -P_MAXFLOAT, P_MAXFLOAT);
}

void EPAFollow::Compile(IWriter& F)
{
	pFollow(F, _float("Magnitude").val, _float("Epsilon").val, _float("Max Radius").val);
}

EPAGravitate::EPAGravitate() : EParticleAction(PAPI::PAGravitateID)
{
	actionType = "Gravitate";
	actionName = actionType;
	appendFloat("Magnitude", 1.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", 0.001f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Radius", 10.0f, -P_MAXFLOAT, P_MAXFLOAT);
}

void EPAGravitate::Compile(IWriter& F)
{
	pGravitate(F, _float("Magnitude").val, _float("Epsilon").val, _float("Max Radius").val);
}

EPAGravity::EPAGravity() : EParticleAction(PAPI::PAGravityID)
{
	actionType = "Gravity";
	actionName = actionType;
	appendVector("Direction", PVector::vNum, 0.f, -9.8f, 0.f);
	appendBool("Allow Rotate", TRUE);
}

void EPAGravity::Compile(IWriter& F)
{
	pGravity(F, _vector("Direction").val, _bool("Allow Rotate").val);
}

EPAJet::EPAJet() : EParticleAction(PAPI::PAJetID)
{
	actionType = "Jet";
	actionName = actionType;
	appendDomain("Accelerate", PDomain(PDomain::vNum, FALSE));
	appendVector("Center", PVector::vNum, 0.f, 0.f, 0.f);
	appendFloat("Magnitude", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", EPS_L, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Radius", P_MAXFLOAT, -P_MAXFLOAT, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPAJet::Compile(IWriter& F)
{
	pJet(F, pDomain(EXPAND_DOMAIN(_domain("Accelerate"))), _vector("Center").val, _float("Magnitude").val,
	     _float("Epsilon").val, _float("Max Radius").val, _bool("Allow Rotate").val);
}

EPAKillOld::EPAKillOld() : EParticleAction(PAPI::PAKillOldID)
{
	actionType = "KillOld";
	actionName = actionType;
	appendFloat("Age Limit", 5.f, 0.0f, P_MAXFLOAT);
	appendBool("Kill Less Than", FALSE);
}

void EPAKillOld::Compile(IWriter& F)
{
	pKillOld(F, _float("Age Limit").val, _bool("Kill Less Than").val);
}

EPAMatchVelocity::EPAMatchVelocity() : EParticleAction(PAPI::PAMatchVelocityID)
{
	actionType = "MatchVelocity";
	actionName = actionType;
	appendFloat("Magnitude", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", EPS_L, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Radius", P_MAXFLOAT, -P_MAXFLOAT, P_MAXFLOAT);
}

void EPAMatchVelocity::Compile(IWriter& F)
{
	pMatchVelocity(F, _float("Magnitude").val, _float("Epsilon").val, _float("Max Radius").val);
}

EPAMove::EPAMove() : EParticleAction(PAPI::PAMoveID)
{
	actionType = "Move";
	actionName = actionType;
}

void EPAMove::Compile(IWriter& F)
{
	pMove(F);
}

EPAOrbitLine::EPAOrbitLine() : EParticleAction(PAPI::PAOrbitLineID)
{
	actionType = "OrbitLine";
	actionName = actionType;
	appendVector("Position", PVector::vNum, 0.f, 0.f, 0.f);
	appendVector("Axis", PVector::vNum, 0.f, 0.f, 0.f);
	appendFloat("Magnitude", 1.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", EPS_L, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Radius", P_MAXFLOAT, -P_MAXFLOAT, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPAOrbitLine::Compile(IWriter& F)
{
	pOrbitLine(F, _vector("Position").val, _vector("Axis").val, _float("Magnitude").val, _float("Epsilon").val,
	           _float("Max Radius").val, _bool("Allow Rotate").val);
}

EPAOrbitPoint::EPAOrbitPoint() : EParticleAction(PAPI::PAOrbitPointID)
{
	actionType = "OrbitPoint";
	actionName = actionType;
	appendVector("Center", PVector::vNum, 0.f, 0.f, 0.f);
	appendFloat("Magnitude", 400.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", 0.1f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Radius", 100.0f, -P_MAXFLOAT, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPAOrbitPoint::Compile(IWriter& F)
{
	pOrbitPoint(F, _vector("Center").val, _float("Magnitude").val, _float("Epsilon").val, _float("Max Radius").val,
	            _bool("Allow Rotate").val);
}

EPARandomAccel::EPARandomAccel() : EParticleAction(PAPI::PARandomAccelID)
{
	actionType = "RandomAccel";
	actionName = actionType;
	appendDomain("Accelerate", PDomain(PDomain::vNum, FALSE));
	appendBool("Allow Rotate", TRUE);
}

void EPARandomAccel::Compile(IWriter& F)
{
	pRandomAccel(F, pDomain(EXPAND_DOMAIN(_domain("Accelerate"))), _bool("Allow Rotate").val);
}

EPARandomDisplace::EPARandomDisplace() : EParticleAction(PAPI::PARandomDisplaceID)
{
	actionType = "RandomDisplace";
	actionName = actionType;
	appendDomain("Displace", PDomain(PDomain::vNum, FALSE));
	appendBool("Allow Rotate", TRUE);
}

void EPARandomDisplace::Compile(IWriter& F)
{
	pRandomDisplace(F, pDomain(EXPAND_DOMAIN(_domain("Displace"))), _bool("Allow Rotate").val);
}

EPARandomVelocity::EPARandomVelocity() : EParticleAction(PAPI::PARandomVelocityID)
{
	actionType = "RandomVelocity";
	actionName = actionType;
	appendDomain("Velocity", PDomain(PDomain::vNum, FALSE));
	appendBool("Allow Rotate", TRUE);
}

void EPARandomVelocity::Compile(IWriter& F)
{
	pRandomVelocity(F, pDomain(EXPAND_DOMAIN(_domain("Velocity"))), _bool("Allow Rotate").val);
}

EPARestore::EPARestore() : EParticleAction(PAPI::PARestoreID)
{
	actionType = "Restore";
	actionName = actionType;
	appendFloat("Time", 0.f, 0.0f, P_MAXFLOAT);
}

void EPARestore::Compile(IWriter& F)
{
	pRestore(F, _float("Time").val);
}

EPAScatter::EPAScatter() : EParticleAction(PAPI::PAScatterID)
{
	actionType = "Scatter";
	actionName = actionType;
	appendVector("Center", PVector::vNum, 0.f, 0.f, 0.f);
	appendFloat("Magnitude", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", EPS_L, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Radius", P_MAXFLOAT, -P_MAXFLOAT, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPAScatter::Compile(IWriter& F)
{
	pScatter(F, _vector("Center").val, _float("Magnitude").val, _float("Epsilon").val, _float("Max Radius").val,
	         _bool("Allow Rotate").val);
}

EPASink::EPASink() : EParticleAction(PAPI::PASinkID)
{
	actionType = "Sink";
	actionName = actionType;
	appendBool("Kill Inside", TRUE);
	appendDomain("Domain", PDomain(PDomain::vNum, TRUE, 0x60ff0000));
	appendBool("Allow Rotate", TRUE);
}

void EPASink::Compile(IWriter& F)
{
	pSink(F, _bool("Kill Inside").val, pDomain(EXPAND_DOMAIN(_domain("Domain"))), _bool("Allow Rotate").val);
}

EPASinkVelocity::EPASinkVelocity() : EParticleAction(PAPI::PASinkVelocityID)
{
	actionType = "SinkVelocity";
	actionName = actionType;
	appendBool("Kill Inside", TRUE);
	appendDomain("Domain", PDomain(PDomain::vNum, FALSE));
	appendBool("Allow Rotate", TRUE);
}

void EPASinkVelocity::Compile(IWriter& F)
{
	pSinkVelocity(F, _bool("Kill Inside").val, pDomain(EXPAND_DOMAIN(_domain("Domain"))), _bool("Allow Rotate").val);
}

EPASource::EPASource() : EParticleAction(PAPI::PASourceID)
{
	actionType = "Source";
	actionName = actionType;
	appendFloat("Rate", 100.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendDomain("Domain", PDomain(PDomain::vNum, TRUE, 0x60FFEBAA));
	appendDomain("Velocity", PDomain(PDomain::vNum, FALSE));
	appendDomain("Rotation", PDomain(PDomain::vAngle, FALSE));
	appendDomain("Size", PDomain(PDomain::vNum, FALSE));
	appendBool("Single Size", FALSE);
	appendDomain("Color", PDomain(PDomain::vColor, FALSE, 0x00000000, PAPI::PDPoint, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
	                              1.f, 1.f));
	appendFloat("Color\\Alpha", 0.f, 0.f, 1.f);
	appendFloat("Starting Age", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Age Sigma", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Parent Motion", 0.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendBool("Allow Rotate", FALSE);
}

void EPASource::Compile(IWriter& F)
{
	pSource(F, _float("Rate").val, pDomain(EXPAND_DOMAIN(_domain("Domain"))),
	        pDomain(EXPAND_DOMAIN(_domain("Velocity"))),
	        pDomain(EXPAND_DOMAIN(_domain("Rotation"))),
	        pDomain(EXPAND_DOMAIN(_domain("Size"))), _bool("Single Size").val,
	        pDomain(EXPAND_DOMAIN(_domain("Color"))), _float("Color\\Alpha").val,
	        _float("Starting Age").val, _float("Age Sigma").val, _float("Parent Motion").val,
	        _bool("Allow Rotate").val);
}

EPASpeedLimit::EPASpeedLimit() : EParticleAction(PAPI::PASpeedLimitID)
{
	actionType = "SpeedLimit";
	actionName = actionType;
	appendFloat("Min Speed", -1.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Speed", 15.0f, -P_MAXFLOAT, P_MAXFLOAT);
}

void EPASpeedLimit::Compile(IWriter& F)
{
	pSpeedLimit(F, _float("Min Speed").val, _float("Max Speed").val);
}

EPATargetColor::EPATargetColor() : EParticleAction(PAPI::PATargetColorID)
{
	actionType = "TargetColor";
	actionName = actionType;
	appendVector("Color", PVector::vColor, 1.f, 1.f, 1.f, 0.f, 1.f);
	appendFloat("Alpha", 1.f, 0.0f, 1.0f);
	appendFloat("Scale", 1.f, 0.01f, P_MAXFLOAT);
	appendFloat("TimeFrom", 0.0f, 0.0f, 1.0f);
	appendFloat("TimeTo", 1.0f, 0.0f, 1.0f);
}

void EPATargetColor::Compile(IWriter& F)
{
	pTargetColor(F, _vector("Color").val, _float("Alpha").val, _float("Scale").val, _float("TimeFrom").val,
	             _float("TimeTo").val);
}

EPATargetSize::EPATargetSize() : EParticleAction(PAPI::PATargetSizeID)
{
	actionType = "TargetSize";
	actionName = actionType;
	appendVector("Size", PVector::vNum, 2.f, 2.f, 0.001f, EPS_L);
	appendVector("Scale", PVector::vNum, 1.f, 1.f, 0.f);
}

void EPATargetSize::Compile(IWriter& F)
{
	pTargetSize(F, _vector("Size").val, _vector("Scale").val);
}

EPATargetRotate::EPATargetRotate() : EParticleAction(PAPI::PATargetRotateID)
{
	actionType = "TargetRotate";
	actionName = actionType;
	appendVector("Rotation", PVector::vAngle, 0.f, 0.f, 0.f);
	appendFloat("Scale", 1.f, 0.0f, P_MAXFLOAT);
}

void EPATargetRotate::Compile(IWriter& F)
{
	pTargetRotate(F, _vector("Rotation").val, _float("Scale").val);
}

EPATargetVelocity::EPATargetVelocity() : EParticleAction(PAPI::PATargetVelocityID)
{
	actionType = "TargetVelocity";
	actionName = actionType;
	appendVector("Velocity", PVector::vNum, 0.f, 0.f, 0.f);
	appendFloat("Scale", 1.f, 0.0f, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPATargetVelocity::Compile(IWriter& F)
{
	pTargetVelocity(F, _vector("Velocity").val, _float("Scale").val, _bool("Allow Rotate").val);
}

EPAVortex::EPAVortex() : EParticleAction(PAPI::PAVortexID)
{
	actionType = "Vortex";
	actionName = actionType;
	appendVector("Center", PVector::vNum, 0.f, 0.f, 0.f);
	appendVector("Axis", PVector::vNum, 0.f, 1.f, 0.f);
	appendFloat("Magnitude", 1.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Epsilon", EPS_L, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Max Radius", 1.0f, -P_MAXFLOAT, P_MAXFLOAT);
	appendBool("Allow Rotate", TRUE);
}

void EPAVortex::Compile(IWriter& F)
{
	pVortex(F, _vector("Center").val, _vector("Axis").val, _float("Magnitude").val, _float("Epsilon").val,
	        _float("Max Radius").val, _bool("Allow Rotate").val);
}

EPATurbulence::EPATurbulence() : EParticleAction(PAPI::PATurbulenceID)
{
	actionType = "Turbulence";
	actionName = actionType;
	appendFloat("Frequency", 2.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendInt("Octaves", 1, 1);
	appendFloat("Magnitude", 10.f, -P_MAXFLOAT, P_MAXFLOAT);
	appendFloat("Delta", 0.01f, -P_MAXFLOAT, P_MAXFLOAT);
	appendVector("Movement", PVector::vNum, 1, 1, 1);
	// -
	nval = 0;
	age = 0.f;
}

static const int detail = 16;

void EPATurbulence::Compile(IWriter& F)
{
	pTurbulence(F, _float("Frequency").val, _int("Octaves").val, _float("Magnitude").val, _float("Delta").val,
	            _vector("Movement").val);
	if (nval != 0)
	{
		for (int i = 0; i < detail; i++)
		{
			for (int j = 0; j < detail; j++)
				delete[] nval[i][j];
		}
		for (i = 0; i < detail; i++)
			delete[] nval[i];
		delete[] nval;
		nval = 0;
	}
}

//---------------------------------------------------------------------------

PDomain::PDomain(EType et, BOOL ra, u32 color, PDomainEnum t,
                 float inA0, float inA1, float inA2,
                 float inA3, float inA4, float inA5,
                 float inA6, float inA7, float inA8)
{
	flags.set(flRenderable, ra);
	e_type = et;
	type = t;
	clr = color;
	f[0] = inA0;
	f[1] = inA1;
	f[2] = inA2;
	f[3] = inA3;
	f[4] = inA4;
	f[5] = inA5;
	f[6] = inA6;
	f[7] = inA7;
	f[8] = inA8;
}

PDomain::PDomain(const PDomain& inDomain)
{
	e_type = inDomain.e_type;
	flags = inDomain.flags;
	type = inDomain.type;
	clr = inDomain.clr;
	f[0] = inDomain.f[0];
	f[1] = inDomain.f[1];
	f[2] = inDomain.f[2];
	f[3] = inDomain.f[3];
	f[4] = inDomain.f[4];
	f[5] = inDomain.f[5];
	f[6] = inDomain.f[6];
	f[7] = inDomain.f[7];
	f[8] = inDomain.f[8];
}

PDomain::~PDomain()
{
}

void PDomain::Load(IReader& F)
{
	type = PDomainEnum(F.r_u32());
	F.r_fvector3(v[0]);
	F.r_fvector3(v[1]);
	F.r_fvector3(v[2]);
}

void PDomain::Load2(CInifile& ini, const shared_str& sect)
{
	type = PDomainEnum(ini.r_u32(sect, "type"));
	v[0] = ini.r_fvector3(sect, "v0");
	v[1] = ini.r_fvector3(sect, "v1");
	v[2] = ini.r_fvector3(sect, "v2");
}

void PDomain::Save(IWriter& F)
{
	F.w_u32(type);
	F.w_fvector3(v[0]);
	F.w_fvector3(v[1]);
	F.w_fvector3(v[2]);
}

void PDomain::Save2(CInifile& ini, const shared_str& sect)
{
	ini.w_u32(sect.c_str(), "type", type);
	ini.w_fvector3(sect.c_str(), "v0", v[0]);
	ini.w_fvector3(sect.c_str(), "v1", v[1]);
	ini.w_fvector3(sect.c_str(), "v2", v[2]);
}

//--------------------------------------------------------------------

xr_token domain_token[] = {
	{"Point", PDPoint},
	{"Line", PDLine},
	{"Triangle", PDTriangle},
	{"Plane", PDPlane},
	{"Box", PDBox},
	{"Sphere", PDSphere},
	{"Cylinder", PDCylinder},
	{"Cone", PDCone},
	{"Blob", PDBlob},
	{"Disc", PDDisc},
	{"Rectangle", PDRectangle},
	{0, 0}
};

/* #define TColor u32

void 	PDomain::FillProp(PropItemVec& items, LPCSTR pref, u32 clr)
{
	PropValue* V;
	V = PHelper().CreateToken32(items, pref, (u32*)&type, domain_token);
	//	V->OnChangeEvent.bind(this, &PDomain::OnTypeChange);
	V->Owner()->prop_color = TColor(clr);

	switch (e_type) {
	case vNum:
		switch (type) {
		case PDPoint:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Center"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDLine:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Point 1"), &v[0], flt_min, flt_max, 0.001f, 3); 	V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Point 2"), &v[1], flt_min, flt_max, 0.001f, 3);	V->Owner()->prop_color = TColor(clr);
			break;
		case PDTriangle:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Vertex 1"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Vertex 2"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Vertex 3"), &v[2], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDPlane:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Origin"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Normal"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDBox:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Min"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Max"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDSphere:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Center"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[3], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[4], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDCylinder:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Point 1"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Point 2"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDCone:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Apex"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "End Point"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDBlob:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Center"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[3], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDDisc:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Center"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Normal"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDRectangle:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Origin"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Basis U"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Basis V"), &v[2], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		default:
			V = PHelper().CreateVector(items, PrepareKey(pref, "Translate"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Rotate"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVector(items, PrepareKey(pref, "Scale"), &v[2], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		}
		break;
	case vAngle:
		switch (type) {
		case PDPoint:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Center"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDLine:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Point 1"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Point 2"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDTriangle:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Vertex 1"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Vertex 2"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Vertex 3"), &v[2], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDPlane:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Origin"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Normal"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDBox:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Min"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Max"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDSphere:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Center"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[3], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[4], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDCylinder:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Point 1"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Point 2"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDCone:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Apex"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "End Point"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDBlob:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Center"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[3], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDDisc:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Center"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Normal"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		case PDRectangle:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Origin"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Basis U"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Basis V"), &v[2], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		default:
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Translate"), &v[0], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Rotate"), &v[1], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateAngle3(items, PrepareKey(pref, "Scale"), &v[2], flt_min, flt_max, 0.001f, 3);V->Owner()->prop_color = TColor(clr);
			break;
		}
		break;
	case vColor:
		switch (type) {
		case PDPoint:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Center"), &v[0]);	V->Owner()->prop_color = TColor(clr);
			break;
		case PDLine:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Point 1"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Point 2"), &v[1]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDTriangle:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Vertex 1"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Vertex 2"), &v[1]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Vertex 3"), &v[2]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDPlane:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Origin"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Normal"), &v[1]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDBox:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Min"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Max"), &v[1]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDSphere:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Center"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[3]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[4]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDCylinder:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Point 1"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Point 2"), &v[1]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDCone:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Apex"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "End Point"), &v[1]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDBlob:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Center"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[3]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDDisc:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Center"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Normal"), &v[1]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Inner"), &f[6]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateFloat(items, PrepareKey(pref, "Radius Outer"), &f[7]); V->Owner()->prop_color = TColor(clr);
			break;
		case PDRectangle:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Origin"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Basis U"), &v[1]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Basis V"), &v[2]); V->Owner()->prop_color = TColor(clr);
			break;
		default:
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Translate"), &v[0]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Rotate"), &v[1]); V->Owner()->prop_color = TColor(clr);
			V = PHelper().CreateVColor(items, PrepareKey(pref, "Scale"), &v[2]); V->Owner()->prop_color = TColor(clr);
			break;
		}
		break;
	}
} //*/
