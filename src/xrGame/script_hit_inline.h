////////////////////////////////////////////////////////////////////////////
//	Module 		: script_hit_inline.h
//	Created 	: 06.02.2004
//  Modified 	: 24.06.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script hit class inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once
#include "Hit.h"
#include "GameObject.h"

IC CScriptHit::CScriptHit()
{
	m_fPower = 100;
	m_tDirection.set(1, 0, 0);
	m_caBoneName = "";
	m_tpDraftsman = 0;
	m_fImpulse = 100;
	m_tpWeaponID = 0;
	m_tHitType = ALife::eHitTypeWound;
}

IC CScriptHit::CScriptHit(const CScriptHit* tpLuaHit)
{
	*this = *tpLuaHit;
}

IC CScriptHit::CScriptHit(const SHit* tpHit)
{
	m_fPower = tpHit->power;
	m_tDirection = tpHit->direction();
	m_tpDraftsman = smart_cast<const CGameObject*>(tpHit->who)->lua_game_object();
	m_fImpulse = tpHit->impulse;
	m_tpWeaponID = tpHit->weaponID;
	m_tHitType = tpHit->hit_type;
}

IC void CScriptHit::set_bone_name(LPCSTR bone_name)
{
	m_caBoneName = bone_name;
}
