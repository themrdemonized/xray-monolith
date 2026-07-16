#pragma once
#include "missile.h"
#include "explosive.h"
#include "../xrEngine/feel_touch.h"

#ifdef EXPLOSIVE_CHANGE
struct SGrenadeContact
{
	bool enabled; /* Enable explode by contact. */
	bool contact; /* Set true on grenade contact callback, used for ContactUpdateCL. */
	bool explode; /* Grenade explode by contact. (not by time out) */
	Fvector position;
	Fvector velocity;
	Fvector normal;
	shared_str material;
	SGrenadeContact()
	{
		enabled = false;
		contact = false;
		explode = false;
		position.set(0.0F, 0.0F, 0.0F);
		velocity.set(0.0F, 0.0F, 0.0F);
		normal.set(0.0F, 1.0F, 0.0F);
		material._set("");
	}
};
#endif

class CGrenade :
	public CMissile,
	public CExplosive
{
	typedef CMissile inherited;
public:
	CGrenade();
	virtual ~CGrenade();


	virtual void Load(LPCSTR section);

	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void net_Destroy();
	virtual void net_Relcase(CObject* O);
	virtual bool net_RelcaseNeeded() const override { return true; }

	virtual void OnH_B_Independent(bool just_before_destroy);
	virtual void OnH_A_Independent();
	virtual void OnH_A_Chield();
	virtual void DiscardState();

	virtual void OnEvent(NET_Packet& P, u16 type);
	virtual bool DropGrenade(); //in this case if grenade state is eReady, it should Throw

	virtual void OnAnimationEnd(u32 state);
	virtual void UpdateCL();

	virtual void Throw();
	virtual void Destroy();


	virtual bool Action(u16 cmd, u32 flags);
	virtual bool Useful() const;
	virtual void State(u32 state, u32 old_state);

	virtual void OnH_B_Chield() { inherited::OnH_B_Chield(); }

	virtual void Hit(SHit* pHDS);

	virtual bool NeedToDestroyObject() const;
	virtual ALife::_TIME_ID TimePassedAfterIndependant() const;

	void PutNextToSlot();

	virtual void DeactivateItem();
	virtual bool GetBriefInfo(II_BriefInfo& info);

	virtual void SendHiddenItem(); //same as OnHiddenItem but for client... (sends message to a server)...
protected:
	ALife::_TIME_ID m_dwGrenadeRemoveTime;
	ALife::_TIME_ID m_dwGrenadeIndependencyTime;
private:
	float m_grenade_detonation_threshold_hit;
	bool m_thrown;

protected:
	virtual void UpdateXForm() { CMissile::UpdateXForm(); };
public:

	virtual BOOL UsedAI_Locations();
	virtual CExplosive* cast_explosive() { return this; }
	virtual CMissile* cast_missile() { return this; }
	virtual CHudItem* cast_hud_item() { return this; }
	virtual CGameObject* cast_game_object() { return this; }
	virtual CGrenade* cast_grenade() { return this; }
	virtual IDamageSource* cast_IDamageSource() { return CExplosive::cast_IDamageSource(); }

	typedef xr_delegate<void (CGrenade*)> destroy_callback;

	void set_destroy_callback(destroy_callback callback)
	{
		m_destroy_callback = callback;
	}

#ifdef EXPLOSIVE_CHANGE
	SGrenadeContact m_contact;
	virtual void activate_physic_shell();
	void Contact(const Fvector &pos, const Fvector &vel, const Fvector &nor, const LPCSTR mtl);
	void ContactUpdateCL();
	static void GrenadeContactCallback(bool &do_colide, bool bo1, dContact &c, SGameMtl *mtl_1, SGameMtl *mtl_2);

	shared_str m_on_grenade_explode_callback;
	void OnBeforeExplosion();
#endif

private:
	destroy_callback m_destroy_callback;
};
