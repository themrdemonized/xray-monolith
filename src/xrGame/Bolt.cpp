#include "stdafx.h"
#include "bolt.h"
#include "ParticlesObject.h"
#include "../xrphysics/PhysicsShell.h"
#include "xr_level_controller.h"
// Tronex
#include "inventory.h"
#include "actor.h"

CBolt::CBolt(void)
{
	m_thrower_id = u16(-1);
}

CBolt::~CBolt(void)
{
}

void CBolt::OnH_A_Chield()
{
	inherited::OnH_A_Chield();
	CObject* o = H_Parent()->H_Parent();
	if (o)SetInitiator(o->ID());
}

void CBolt::Throw()
{
	CMissile* l_pBolt = smart_cast<CMissile*>(m_fake_missile);
	if (!l_pBolt) return;

	luabind::functor<bool> funct;
	if (m_pInventory && smart_cast<CInventoryOwner*>(H_Parent()) &&
		ai().script_engine().functor("_G.CBolt__State", funct))
	{
		CActor* pActor = smart_cast<CActor*>(m_pInventory->GetOwner());
		if (pActor)
		{
			if (!funct(pActor->ID()))
				l_pBolt->SetCanTake(FALSE);
		}
	}
	
	l_pBolt->set_destroy_time(u32(m_dwDestroyTimeMax / phTimefactor));
	inherited::Throw();
	spawn_fake_missile();

	//DestroyObject();
	m_thrown = true;
}

bool CBolt::Useful() const
{
	return CanTake();
}

bool CBolt::Action(u16 cmd, u32 flags)
{
	if (inherited::Action(cmd, flags)) return true;
	/*
		switch(cmd) 
		{
		case kDROP:
			{
				if(flags&CMD_START) 
				{
					m_throw = false;
					if(State() == MS_IDLE) State(MS_THREATEN);
				} 
				else if(State() == MS_READY || State() == MS_THREATEN) 
				{
					m_throw = true; 
					if(State() == MS_READY) State(MS_THROW);
				}
			} 
			return true;
		}
	*/
	return false;
}

void CBolt::activate_physic_shell()
{
	inherited::activate_physic_shell();
	m_pPhysicsShell->SetAirResistance(.0001f);
}

void CBolt::SetInitiator(u16 id)
{
	m_thrower_id = id;
}

u16 CBolt::Initiator()
{
	return m_thrower_id;
}

// Tronex: for limited bolts
void CBolt::PutNextToSlot()
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
		CBolt* pNext = smart_cast<CBolt*>(m_pInventory->Same(this, true));
		if (!pNext) pNext = smart_cast<CBolt*>(m_pInventory->SameSlot(BOLT_SLOT, this, true));

		VERIFY(pNext != this);

		if (pNext && m_pInventory->Slot(pNext->BaseSlot(), pNext))
		{
			pNext->u_EventGen(P, GEG_PLAYER_ITEM2SLOT, pNext->H_Parent()->ID());
			P.w_u16(pNext->ID());
			P.w_u16(pNext->BaseSlot());
			pNext->u_EventSend(P);
			m_pInventory->SetActiveSlot(pNext->BaseSlot());
		}
		else
		{
			CActor* pActor = smart_cast<CActor*>(m_pInventory->GetOwner());

			if (pActor)
				pActor->OnPrevWeaponSlot();
		}

		m_thrown = false;
	}
}

void CBolt::State(u32 state, u32 old_state)
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
				//m_dwDestroyTime			= 0xffffffff;

				luabind::functor<bool> funct;
				if (m_pInventory && smart_cast<CInventoryOwner*>(H_Parent()) && 
					ai().script_engine().functor("_G.CBolt__State", funct))
				{
					CActor* pActor = smart_cast<CActor*>(m_pInventory->GetOwner());
					if (pActor && funct(pActor->ID()))
					{
						PutNextToSlot();
						if (Local())
							DestroyObject();
					}
					else
						m_thrown = false;
				}
				else
					m_thrown = false;
			};
		}
		break;
	};
	inherited::State(state, old_state);
}
