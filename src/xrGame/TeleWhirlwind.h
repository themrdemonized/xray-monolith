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

class CTeleTrampolinObject : public CTelekineticObject
{
private:
	typedef CTelekineticObject inherited;

	CTeleWhirlwind* m_pTelekinesis;
	bool			m_bDestroyable;
	float			m_fThrowPower;

public:
	virtual ~CTeleTrampolinObject()
	{
	};
	CTeleTrampolinObject();
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

	// Type of telekinesis: 0 - default (whirlwind), 1 - trampolin
	u16 m_iTelekinesisType;

	Fvector	m_vCenter;
	float	m_fKeepRadius;
	float	m_fThrowPower;
	boolean	m_bHeightFixed;		// whether object will be hard-fixed at tele_heigh or can go above (true - default)
	boolean m_bSpawnSkeleton;
	float   m_fTeleTime;
	u16		m_fTeleRotateSpeed;

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
		if (m_iTelekinesisType == 1) 
		{
			return static_cast<CTelekineticObject*>(xr_new<CTeleTrampolinObject>());
		}
		else 
		{
			return static_cast<CTelekineticObject*>(xr_new<CTeleWhirlwindObject>());
		}
	}

	void PlayTearingSound(CObject* object) 
	{ 
		m_pTearingSound.play_at_pos(object, m_vCenter); 
	}

	void Load(LPCSTR section);

public: // Getters

	u16				GetOwnerID() const { return m_iOwnerID; }
	u16				GetTelekinesisType() const { return m_iTelekinesisType; }
	float			GetKeepRadius() const { return m_fKeepRadius; }
	float			GetTeleTime() const { return m_fTeleTime; }
	u16				GetTeleRotateSpeed() const { return m_fTeleRotateSpeed; }
	boolean			GetSpawnSkeleton() const { return m_bSpawnSkeleton; }
	boolean			GetHeightFixed() const { return m_bHeightFixed; }
	const Fvector&	GetCenter() const { return m_vCenter; }
	LPCSTR			GetTearingParticles() const { return *m_sTearingParticles; }
	
public: // Setters

	void SetCenter(const Fvector& center) { m_vCenter.set(center); }
	void SetOwnerID(u16 owner_id) { m_iOwnerID = owner_id; }

};


#endif
