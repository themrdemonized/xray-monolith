#include "stdafx.h"
#include "grenade.h"
#include "../xrphysics/PhysicsShell.h"
//.#include "WeaponHUD.h"
#include "entity.h"
#include "actor.h"
#include "inventory.h"
#include "level.h"
#include "xrmessages.h"
#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "xrserver_objects_alife.h"
#include "script_game_object.h"

#ifdef EXPLOSIVE_CHANGE
#include "../xrEngine/GameMtlLib.h"
#include "../xrphysics/ExtendedGeom.h"
#include "../xrphysics/CalculateTriangle.h"
#include "../xrphysics/tri-colliderknoopc/dctriangle.h"
#endif

#define GRENADE_REMOVE_TIME		30000
const float default_grenade_detonation_threshold_hit = 100;

CGrenade::CGrenade(void)
{
	m_destroy_callback.clear();
#ifdef EXPLOSIVE_CHANGE
	m_on_grenade_explode_callback._set("");
#endif
}

CGrenade::~CGrenade(void)
{
}

void CGrenade::Load(LPCSTR section)
{
	inherited::Load(section);
	CExplosive::Load(section);

	//////////////////////////////////////
	//время убирания оружия с уровня
	if (pSettings->line_exist(section, "grenade_remove_time"))
		m_dwGrenadeRemoveTime = pSettings->r_u32(section, "grenade_remove_time");
	else
		m_dwGrenadeRemoveTime = GRENADE_REMOVE_TIME;
	m_grenade_detonation_threshold_hit = READ_IF_EXISTS(pSettings, r_float, section, "detonation_threshold_hit",
	                                                    default_grenade_detonation_threshold_hit);

#ifdef EXPLOSIVE_CHANGE
	m_on_grenade_explode_callback._set(READ_IF_EXISTS(pSettings, r_string, section, "on_grenade_explode", ""));
	m_contact.enabled = !!READ_IF_EXISTS(pSettings, r_bool, section, "explode_on_contact", FALSE);
#endif
}

void CGrenade::Hit(SHit* pHDS)
{
	if (ALife::eHitTypeExplosion == pHDS->hit_type && m_grenade_detonation_threshold_hit < pHDS->damage() && CExplosive
		::Initiator() == u16(-1))
	{
		CExplosive::SetCurrentParentID(pHDS->who->ID());
		Destroy();
	}
	inherited::Hit(pHDS);
}

BOOL CGrenade::net_Spawn(CSE_Abstract* DC)
{
	m_dwGrenadeIndependencyTime = 0;
	BOOL ret = inherited::net_Spawn(DC);
	Fvector box;
	BoundingBox().getsize(box);
	float max_size = _max(_max(box.x, box.y), box.z);
	box.set(max_size, max_size, max_size);
	box.mul(3.f);
	CExplosive::SetExplosionSize(box);
	m_thrown = false;
	return ret;
}

void CGrenade::net_Destroy()
{
	if (m_destroy_callback)
	{
		m_destroy_callback(this);
		m_destroy_callback = destroy_callback(NULL);
	}

	inherited::net_Destroy();
	CExplosive::net_Destroy();
}

void CGrenade::OnH_B_Independent(bool just_before_destroy)
{
	inherited::OnH_B_Independent(just_before_destroy);
}

void CGrenade::OnH_A_Independent()
{
	m_dwGrenadeIndependencyTime = Level().timeServer();
	inherited::OnH_A_Independent();
}

void CGrenade::OnH_A_Chield()
{
	m_dwGrenadeIndependencyTime = 0;
	m_dwDestroyTime = 0xffffffff;
	inherited::OnH_A_Chield();
}

void CGrenade::State(u32 state, u32 old_state)
{
	switch (state)
	{
	case eThrowEnd:
		{
			if (m_thrown)
			{
				if (m_pPhysicsShell)
					m_pPhysicsShell->Deactivate();
				xr_delete(m_pPhysicsShell);
				m_dwDestroyTime = 0xffffffff;
				PutNextToSlot();
				if (Local())
				{
#ifndef MASTER_GOLD
					Msg( "Destroying local grenade[%d][%d]", ID(), Device.dwFrame );
#endif // #ifndef MASTER_GOLD
					DestroyObject();
				}
			};
		}
		break;
	};
	inherited::State(state, old_state);
}

bool CGrenade::DropGrenade()
{
	EMissileStates grenade_state = static_cast<EMissileStates>(GetState());
	if (((grenade_state == eThrowStart) ||
			(grenade_state == eReady) ||
			(grenade_state == eThrow)) &&
		(!m_thrown)
	)
	{
		Throw();
		return true;
	}
	return false;
}

void CGrenade::DiscardState()
{
	if (IsGameTypeSingle())
	{
		u32 state = GetState();
		if (state == eReady || state == eThrow)
		{
			OnStateSwitch(eIdle, state);
		}
	}
}

void CGrenade::SendHiddenItem()
{
	if (GetState() == eThrow)
	{
		//		Msg("MotionMarks !!![%d][%d]", ID(), Device.dwFrame);
		Throw();
	}
	CActor* pActor = smart_cast<CActor*>(m_pInventory->GetOwner());
	if (pActor && (GetState() == eReady || GetState() == eThrow))
	{
		return;
	}

	inherited::SendHiddenItem();
}

void CGrenade::Throw()
{
	if (m_thrown)
		return;

	if (!m_fake_missile)
		return;

	CGrenade* pGrenade = smart_cast<CGrenade*>(m_fake_missile);
	VERIFY(pGrenade);

	if (pGrenade)
	{
		pGrenade->set_destroy_time(m_dwDestroyTimeMax);
		//установить ID того кто кинул гранату
		pGrenade->SetInitiator(H_Parent()->ID());
	}
	inherited::Throw();
	m_fake_missile->processing_activate(); //@sliph
	m_thrown = true;
}


void CGrenade::Destroy()
{
	//Generate Expode event
	Fvector normal;

	if (m_destroy_callback)
	{
		m_destroy_callback(this);
		m_destroy_callback = destroy_callback(NULL);
	}

	FindNormal(normal);
	CExplosive::GenExplodeEvent(Position(), normal);
}


bool CGrenade::Useful() const
{
	bool res = (/* !m_throw && */ m_dwDestroyTime == 0xffffffff && CExplosive::Useful() && TestServerFlag(
		CSE_ALifeObject::flCanSave));

	return res;
}

void CGrenade::OnEvent(NET_Packet& P, u16 type)
{
	inherited::OnEvent(P, type);
	CExplosive::OnEvent(P, type);
}

void CGrenade::PutNextToSlot()
{
	if (OnClient()) return;

	VERIFY(!getDestroy());
	//выкинуть гранату из инвентаря
	NET_Packet P;
	if (m_pInventory)
	{
		m_pInventory->Ruck(this);

		this->u_EventGen(P, GEG_PLAYER_ITEM2RUCK, this->H_Parent()->ID());
		P.w_u16(this->ID());
		this->u_EventSend(P);
	}
	else
		Msg("! PutNextToSlot : m_pInventory = NULL [%d][%d]", ID(), Device.dwFrame);

	if (smart_cast<CInventoryOwner*>(H_Parent()) && m_pInventory)
	{
		CGrenade* pNext = smart_cast<CGrenade*>(m_pInventory->Same(this, true));
		if (!pNext) pNext = smart_cast<CGrenade*>(m_pInventory->SameSlot(GRENADE_SLOT, this, true));

		VERIFY(pNext != this);

		if (pNext)
		{
			::luabind::functor<CScriptGameObject*> funct;
			if (ai().script_engine().functor("_g.CMissile__PutNextToSlot", funct))
			{
				CScriptGameObject* obj = funct(pNext->lua_game_object());
				if (!obj || !smart_cast<CGrenade*>(&obj->object())) return;

				pNext = smart_cast<CGrenade*>(&obj->object());
			}

			if (m_pInventory->Slot(pNext->BaseSlot(), pNext))
			{
				pNext->u_EventGen(P, GEG_PLAYER_ITEM2SLOT, pNext->H_Parent()->ID());
				P.w_u16(pNext->ID());
				P.w_u16(pNext->BaseSlot());
                P.w_u8(0); // do activate
				pNext->u_EventSend(P);
				m_pInventory->SetActiveSlot(pNext->BaseSlot());
			}
		}

		m_thrown = false;
	}
}

void CGrenade::OnAnimationEnd(u32 state)
{
	switch (state)
	{
	case eThrowEnd: SwitchState(eHidden);
		break;
	default: inherited::OnAnimationEnd(state);
	}
}


void CGrenade::UpdateCL()
{
	PROF_EVENT();
	inherited::UpdateCL();
	CExplosive::UpdateCL();

	if (!IsGameTypeSingle()) make_Interpolation();

#ifdef EXPLOSIVE_CHANGE
	ContactUpdateCL();
#endif
}


bool CGrenade::Action(u16 cmd, u32 flags)
{
	if (inherited::Action(cmd, flags)) return true;

	switch (cmd)
	{
		//переключение типа гранаты
	case kWPN_NEXT:
		{
			if (flags & CMD_START)
			{
				if (m_pInventory)
					m_pInventory->ActivateDeffered();
			}
			return true;
		};
	}
	return false;
}


bool CGrenade::NeedToDestroyObject() const
{
	if (IsGameTypeSingle()) return false;
	if (Remote()) return false;
	if (TimePassedAfterIndependant() > m_dwGrenadeRemoveTime)
		return true;

	return false;
}

ALife::_TIME_ID CGrenade::TimePassedAfterIndependant() const
{
	if (!H_Parent() && m_dwGrenadeIndependencyTime != 0)
		return Level().timeServer() - m_dwGrenadeIndependencyTime;
	else
		return 0;
}

BOOL CGrenade::UsedAI_Locations()
{
#pragma todo("Dima to Yura : It crashes, because on net_Spawn object doesn't use AI locations, but on net_Destroy it does use them")
	return inherited::UsedAI_Locations(); //m_dwDestroyTime == 0xffffffff;
}

void CGrenade::net_Relcase(CObject* O)
{
	CExplosive::net_Relcase(O);
	inherited::net_Relcase(O);
}

void CGrenade::DeactivateItem()
{
	//Drop grenade if primed
	StopCurrentAnimWithoutCallback();
	if (!GetTmpPreDestroy() && Local() && (GetState() == eThrowStart || GetState() == eReady || GetState() == eThrow))
	{
		if (m_fake_missile)
		{
			CGrenade* pGrenade = smart_cast<CGrenade*>(m_fake_missile);
			if (pGrenade)
			{
				if (m_pInventory->GetOwner())
				{
					CActor* pActor = smart_cast<CActor*>(m_pInventory->GetOwner());
					if (pActor)
					{
						if (!pActor->g_Alive())
						{
							m_constpower = false;
							m_fThrowForce = 0;
						}
					}
				}
				Throw();
			};
		};
	};

	inherited::DeactivateItem();
}

bool CGrenade::GetBriefInfo(II_BriefInfo& info)
{
	VERIFY(m_pInventory);
	info.clear();

	info.name._set(m_nameShort);
	info.icon._set(cNameSect());

	u32 ThisGrenadeCount = m_pInventory->dwfGetSameItemCount(cNameSect().c_str(), true);

	string16 stmp;
	xr_sprintf(stmp, "%d", ThisGrenadeCount);
	info.cur_ammo._set(stmp);
	return true;
}

#ifdef EXPLOSIVE_CHANGE
void CGrenade::activate_physic_shell()
{
	inherited::activate_physic_shell();
	if (PPhysicsShell())
	{
		PPhysicsShell()->add_ObjectContactCallback(GrenadeContactCallback);
	}
}

void CGrenade::Contact(const Fvector &pos, const Fvector &vel, const Fvector &nor, const LPCSTR mtl)
{
	m_contact.contact = true;
	m_contact.position.set(pos);
	m_contact.velocity.set(vel);
	m_contact.normal.set(nor);
	m_contact.material._set(mtl ? mtl : "");
}

void CGrenade::ContactUpdateCL()
{
	if (!m_contact.contact)
	{
		return;
	}
	m_contact.contact = false;
	m_contact.explode = true;

	if (PPhysicsShell())
	{
		PPhysicsShell()->set_LinearVel(zero_vel);
		PPhysicsShell()->set_AngularVel(zero_vel);
		PPhysicsShell()->remove_ObjectContactCallback(GrenadeContactCallback);
		PPhysicsShell()->Disable();
	}
	Position().set(m_contact.position);
	Destroy();
}

void CGrenade::GrenadeContactCallback(bool &do_colide, bool bo1, dContact &c, SGameMtl *material_1, SGameMtl *material_2)
{
	dxGeomUserData *gd1 = PHRetrieveGeomUserData(c.geom.g1);
	dxGeomUserData *gd2 = PHRetrieveGeomUserData(c.geom.g2);

	SGameMtl *mtl = nullptr;
	Fvector pos;
	Fvector nor;
	pos.set(*(Fvector *)&c.geom.pos);
	CGrenade *gnd = gd1 ? smart_cast<CGrenade *>(gd1->ph_ref_object) : nullptr;
	if (!gnd)
	{
		gnd = gd2 ? smart_cast<CGrenade *>(gd2->ph_ref_object) : nullptr;
		nor.invert(*(Fvector *)&c.geom.normal);
		mtl = material_1;
	}
	else
	{
		nor.set(*(Fvector *)&c.geom.normal);
		mtl = material_2;
	}
	VERIFY(mtl);

	if (mtl->Flags.is(SGameMtl::flPassable))
		return;

	if (gnd == nullptr || gnd->m_contact.enabled != true || gnd->m_contact.contact == true)
		return;

	CGameObject *who = gd1 ? smart_cast<CGameObject *>(gd1->ph_ref_object) : NULL;
	if (!who || who == (CGameObject *)gnd)
	{
		who = gd2 ? smart_cast<CGameObject *>(gd2->ph_ref_object) : NULL;
	}

	if (!who || who->ID() != gnd->CurrentParentID())
	{
#if 0 /* Copy from CCustomRocket. No idea what these do. Don't seem to work. */
		dxGeomUserData *l_pMYU = bo1 ? gd1 : gd2;
		VERIFY(l_pMYU);
		if (l_pMYU->last_pos[0] != -dInfinity)
		{
			pos = cast_fv(l_pMYU->last_pos);
		}
		if (!gd1 || !gd2)
		{
			dGeomID GID = (gd1) ? c.geom.g1 : c.geom.g2;
			dxGeomUserData *&GUD = gd1 ? gd1 : gd2;

			if (GUD->pushing_neg)
			{
				Fvector velocity;
				gnd->PHGetLinearVell(velocity);
				if (velocity.square_magnitude() > EPS)
				{
					velocity.normalize();
					Triangle neg_tri;
					CalculateTriangle(GUD->neg_tri, GID, neg_tri, Level().ObjectSpace.GetStaticVerts());
					float cosinus = velocity.dotproduct(*((Fvector *)neg_tri.norm));
					VERIFY(_valid(neg_tri.dist));
					float dist = neg_tri.dist / cosinus;
					velocity.mul(dist * 1.1f);
					pos.sub(velocity);
				}
			}
		}
#endif
		Fvector vel;
		gnd->PHGetLinearVell(vel);
		gnd->Contact(pos, vel, nor, mtl->m_Name.c_str());
		R_ASSERT(gnd->m_pPhysicsShell);
		gnd->m_pPhysicsShell->DisableCollision();
		gnd->m_pPhysicsShell->set_LinearVel(zero_vel);
		gnd->m_pPhysicsShell->set_AngularVel(zero_vel);
		gnd->m_pPhysicsShell->setForce(zero_vel);
		gnd->m_pPhysicsShell->setTorque(zero_vel);
		gnd->m_pPhysicsShell->set_ApplyByGravity(false);
		gnd->setEnabled(FALSE);

		do_colide = false;
	}
}

void CGrenade::OnBeforeExplosion()
{
	CExplosive::OnBeforeExplosion();
	if (m_on_grenade_explode_callback.size())
	{
		::luabind::functor<void> lua_function;
		if (ai().script_engine().functor(m_on_grenade_explode_callback.c_str(), lua_function))
		{
			Fvector pos;
			Fvector vel;
			Fvector nor;
			LPCSTR mtl = "";
			if (m_contact.explode)
			{
				pos.set(m_contact.position);
				vel.set(m_contact.velocity);
				nor.set(m_contact.normal);
				mtl = m_contact.material.c_str();
			}
			else
			{
				pos.set(Position());
				PHGetLinearVell(vel);
				nor.set(0.0F, 1.0F, 0.0F);
				mtl = "";
			}
			::luabind::object lua_table = ::luabind::newtable(ai().script_engine().lua());
			lua_table["flag"] = m_contact.explode;
			lua_table["position"] = pos;
			lua_table["velocity"] = vel;
			lua_table["normal"] = nor;
			lua_table["material"] = mtl;
			lua_function(lua_game_object(), lua_table);
		}
	}
}
#endif
