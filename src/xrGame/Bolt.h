#pragma once
#include "missile.h"
#include "../xrphysics/DamageSource.h"

class CBolt :
	public CMissile,
	public IDamageSource
{
	typedef CMissile inherited;
	ALife::_OBJECT_ID m_thrower_id;
public:
	CBolt();
	virtual ~CBolt();

	virtual void OnH_A_Chield();

	virtual void SetInitiator(ALife::_OBJECT_ID id);
	virtual ALife::_OBJECT_ID Initiator();

	virtual void Throw();
	virtual bool Action(u16 cmd, u32 flags);
	virtual bool Useful() const;
	virtual void activate_physic_shell();

	void PutNextToSlot();
	virtual void State(u32 state, u32 old_state);

	virtual BOOL UsedAI_Locations() { return FALSE; }
	virtual IDamageSource* cast_IDamageSource() { return this; }

private:
	bool m_thrown;
};
