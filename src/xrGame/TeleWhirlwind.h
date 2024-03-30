#ifndef TELE_WHIRLWIND
#define TELE_WHIRLWIND
#include "ai/monsters/telekinesis.h"
#include "ai/monsters/telekinetic_object.h"
#include "../xrphysics/PHImpact.h"

class CTeleWhirlwind;
class CGameObject;

class CTeleWhirlwindObject : public CTelekineticObject
{
private:
	typedef CTelekineticObject inherited;

	CTeleWhirlwind* m_pTelekinesis;
	bool			m_bDestroyable;
	float			m_fThrowPower;

public:
	virtual ~CTeleWhirlwindObject()
	{
	};
	CTeleWhirlwindObject();
	virtual bool init(CTelekinesis* tele, CPhysicsShellHolder* obj, float s, float h, u32 ttk, bool rot = true);
	void set_throw_power(float throw_pow);
	virtual bool can_activate(CPhysicsShellHolder* obj);
	virtual void raise(float step);
	virtual void raise_update();
	virtual void keep();
	virtual void release();
	virtual void fire(const Fvector& target);
	virtual void fire(const Fvector& target, float power);
	virtual void switch_state(ETelekineticState new_state);
	virtual bool destroy_object(const Fvector dir, float val);
};

class CTeleWhirlwind : public CTelekinesis
{
private:
	typedef CTelekinesis inherited;

	// ID of the owner object
	u16 m_iOwnerID;

	Fvector	m_vCenter;
	float	m_fKeepRadius;
	float	m_fThrowPower;
	boolean	m_bHeightFixed;		// whether object will be hard-fixed at tele_heigh or can go above (true - default)

	// Arrays of impacts
	PH_IMPACT_STORAGE m_vSavedImpacts;

	// Effects of tearing
	shared_str m_sTearingParticles;
	ref_sound  m_pTearingSound;

public:
	CTeleWhirlwind();
	
	void AddImpact(const Fvector& dir, float val);
	void DrawOutImpact(Fvector& dir, float& val);
	void ClearImpacts();
	
	virtual void clear();
	virtual void clear_notrelevant();

	virtual CTelekineticObject* activate(CPhysicsShellHolder* obj, float strength, float height, u32 max_time_keep, bool rot = true);
	virtual CTelekineticObject* alloc_tele_object()
	{
		return static_cast<CTelekineticObject*>(xr_new<CTeleWhirlwindObject>());
	}

	void PlayTearingSound(CObject* object) 
	{ 
		m_pTearingSound.play_at_pos(object, m_vCenter); 
	}

public: // Getters

	u16				GetOwnerID() const { return m_iOwnerID; }
	float			GetKeepRadius() const { return m_fKeepRadius; }
	boolean			GetHeightFixed() const { return m_bHeightFixed; }
	const Fvector&	GetCenter() const { return m_vCenter; }
	LPCSTR			GetTearingParticles() const { return *m_sTearingParticles; }
	
public: // Setters

	void SetCenter(const Fvector& center) { m_vCenter.set(center); }
	void SetOwnerID(u16 owner_id) { m_iOwnerID = owner_id; }
	void SetThrowPower(float throw_pow) { m_fThrowPower = throw_pow;}
	void SetHeightFixed(boolean value) { m_bHeightFixed = value; }
	void SetTearingParticles(const shared_str& particles) { m_sTearingParticles = particles; }
	void SetTearingSound(LPCSTR name) {	m_pTearingSound.create(name, st_Effect, sg_SourceType); }

};


#endif
