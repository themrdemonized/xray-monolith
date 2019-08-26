#pragma once

#include "inventory_item_object.h"

struct SBoneProtections;

class CHelmet : public CInventoryItemObject
{
private:
	typedef CInventoryItemObject inherited;
public:
	CHelmet();
	virtual ~CHelmet();

	virtual void Load(LPCSTR section);

	virtual void Hit(float P, ALife::EHitType hit_type);

	shared_str m_BonesProtectionSect;
	shared_str m_NightVisionSect;

	virtual void OnMoveToSlot(const SInvItemPlace& previous_place);
	virtual void OnMoveToRuck(const SInvItemPlace& previous_place);
	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void net_Export(NET_Packet& P);
	virtual void net_Import(NET_Packet& P);
	virtual void OnH_A_Chield();

	float GetDefHitTypeProtection(ALife::EHitType hit_type);
	float GetHitTypeProtection(ALife::EHitType hit_type, s16 element);
	float GetBoneArmor(s16 element);

	float HitThroughArmor(float hit_power, s16 element, float ap, bool& add_wound, ALife::EHitType hit_type);

protected:
	HitImmunity::HitTypeSVec m_HitTypeProtection;
	SBoneProtections* m_boneProtection;
	
public:
	float m_fPowerLoss;
	float m_fHealthRestoreSpeed;
	float m_fRadiationRestoreSpeed;
	float m_fSatietyRestoreSpeed;
	float m_fPowerRestoreSpeed;
	float m_fBleedingRestoreSpeed;

	float m_fShowNearestEnemiesDistance;

	float get_HitFracActor() const;
	void ReloadBonesProtection();
	void AddBonesProtection(LPCSTR bones_section);

protected:
	virtual bool install_upgrade_impl(LPCSTR section, bool test);
	
	DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CHelmet)
#undef script_type_list
#define script_type_list save_type_list(CHelmet)