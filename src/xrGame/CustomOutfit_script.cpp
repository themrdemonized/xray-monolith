#include "pch_script.h"
#include "CustomOutfit.h"
#include "ActorHelmet.h"

using namespace luabind;

float GetDefHitTypeProtection_CurrentOutfit(CCustomOutfit* outfit, int hit_type)
{
	return outfit->GetDefHitTypeProtection(ALife::EHitType(hit_type));
}

float GetHitTypeProtection_CurrentOutfit(CCustomOutfit* outfit, int hit_type, LPCSTR element)
{
	return outfit->GetHitTypeProtection(ALife::EHitType(hit_type), s16(element));
}

float GetDefHitTypeProtection_CurrentHelmet(CHelmet* helm, int hit_type)
{
	return helm->GetDefHitTypeProtection(ALife::EHitType(hit_type));
}

float GetHitTypeProtection_CurrentHelmet(CHelmet* helm, int hit_type, LPCSTR element)
{
	return helm->GetHitTypeProtection(ALife::EHitType(hit_type), s16(element));
}

#pragma optimize("s",on)
void CCustomOutfit::script_register(lua_State *L)
{
	module(L)
		[
			class_<CCustomOutfit, CGameObject>("CCustomOutfit")
			.def(constructor<>())
			.def_readwrite("m_fPowerLoss", &CCustomOutfit::m_fPowerLoss)
			.def_readwrite("m_additional_weight", &CCustomOutfit::m_additional_weight)
			.def_readwrite("m_additional_weight2", &CCustomOutfit::m_additional_weight2)
			.def_readwrite("m_fHealthRestoreSpeed", &CCustomOutfit::m_fHealthRestoreSpeed)
			.def_readwrite("m_fRadiationRestoreSpeed", &CCustomOutfit::m_fRadiationRestoreSpeed)
			.def_readwrite("m_fSatietyRestoreSpeed", &CCustomOutfit::m_fSatietyRestoreSpeed)
			.def_readwrite("m_fPowerRestoreSpeed", &CCustomOutfit::m_fPowerRestoreSpeed)
			.def_readwrite("m_fBleedingRestoreSpeed", &CCustomOutfit::m_fBleedingRestoreSpeed)
			.def_readonly("bIsHelmetAvaliable", &CCustomOutfit::bIsHelmetAvaliable)
			.def_readonly("bIsBackpackAvaliable", &CCustomOutfit::bIsBackpackAvaliable)
			.def("BonePassBullet", &CCustomOutfit::BonePassBullet)
			.def("get_HitFracActor", &CCustomOutfit::get_HitFracActor)
			.def("get_artefact_count", &CCustomOutfit::get_artefact_count)
			.def("GetDefHitTypeProtection", &GetDefHitTypeProtection_CurrentOutfit)
			.def("GetHitTypeProtection", &GetHitTypeProtection_CurrentOutfit)
			.def("GetBoneArmor", &CCustomOutfit::GetBoneArmor)
		];
}

void CHelmet::script_register(lua_State *L)
{
	module(L)
		[
			class_<CHelmet, CGameObject>("CHelmet")
			.def(constructor<>())
			.def_readwrite("m_fPowerLoss", &CHelmet::m_fPowerLoss)
			.def_readwrite("m_fHealthRestoreSpeed", &CHelmet::m_fHealthRestoreSpeed)
			.def_readwrite("m_fRadiationRestoreSpeed", &CHelmet::m_fRadiationRestoreSpeed)
			.def_readwrite("m_fSatietyRestoreSpeed", &CHelmet::m_fSatietyRestoreSpeed)
			.def_readwrite("m_fPowerRestoreSpeed", &CHelmet::m_fPowerRestoreSpeed)
			.def_readwrite("m_fBleedingRestoreSpeed", &CHelmet::m_fBleedingRestoreSpeed)
			.def("get_HitFracActor", &CHelmet::get_HitFracActor)
			.def("GetDefHitTypeProtection", &GetDefHitTypeProtection_CurrentHelmet)
			.def("GetHitTypeProtection", &GetHitTypeProtection_CurrentHelmet)
			.def("GetBoneArmor", &CHelmet::GetBoneArmor)
		];
}
