///////////////////////////////////////////////////////////////
// BottleItem.h
// BottleItem - бутылка с напитком, которую можно разбить
///////////////////////////////////////////////////////////////


#pragma once

#include "fooditem.h"
#include "script_export_space.h"

class CBottleItem : public CFoodItem
{
private:
	typedef CFoodItem inherited;
public:
	CBottleItem();
	virtual ~CBottleItem();


	virtual void Load(LPCSTR section);


	void OnEvent(NET_Packet& P, u16 type);


	virtual void Hit(SHit* pHDS);


	void BreakToPieces();
protected:
	//партиклы разбивания бутылки
	shared_str m_sBreakParticles;
	ref_sound sndBreaking;

	DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CBottleItem)
#undef script_type_list
#define script_type_list save_type_list(CBottleItem)