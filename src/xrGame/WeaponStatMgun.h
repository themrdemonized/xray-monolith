#pragma once

#ifdef STATIONARYMGUN_NEW
#include "Inventory.h"
#include "WeaponAmmo.h"
#include "script_game_object.h"
#include "string_table.h"
#include "PHSkeleton.h"
#include "../xrphysics/PHUpdateObject.h"
#include "PhysicsSkeletonObject.h"

#include "player_hud.h"
#include "script_attachment_manager.h"
#endif

#include "holder_custom.h"
#include "shootingobject.h"
#include "physicsshellholder.h"
#include "hudsound.h"
class CCartridge;
class CCameraBase;

#define DESIRED_DIR 1

#ifdef STATIONARYMGUN_NEW
#define STM_SHOT_EFFECTOR 0x53564D /* STM ~ 53 56 4D */

class CActor;
class CInventoryOwner;
class CInventory;
class CWeaponStatMgun;

/*
If you want CWeaponStatMgun to be an item too - putting in inventory, cost, weight, icon, condition, etc,
replace CPhysicsShellHolder with CInventoryItem or CInventoryItemObject and fix all the problems coming along.
Check [jup_b32_scanner_device]
*/

struct SStmBarrel
{
	CWeaponStatMgun *m_stm;
	shared_str m_name;
	u16 m_fire_bid;
	Fmatrix m_fire_xfm;
	Fvector m_fire_pos;
	Fvector m_fire_dir;
	u16 m_drop_bid;

	float fShotTimeCounter;
	float fOneShotTime;
	int iAmmoElapsed;

	shared_str m_sShellParticles;
	shared_str m_sFlameParticles;
	CParticlesObject *m_pFlameParticles;
	shared_str m_sSmokeParticles;

	Fcolor light_base_color;
	float light_base_range;
	Fcolor light_build_color;
	float light_build_range;
	ref_light light_render;
	float light_var_color;
	float light_var_range;
	float light_lifetime;
	u32 light_frame;
	float light_time;
	bool m_bLightShotEnabled;

	SStmBarrel(CWeaponStatMgun *stm, LPCSTR name);
	~SStmBarrel();
	void reinit();
	void Load(LPCSTR section);
	BOOL net_Spawn(CSE_Abstract* DC);
	void net_Export(NET_Packet& P);
	void net_Import(NET_Packet& P);
	LPCSTR Name() { return m_name.c_str(); };
	void UpdateEx();
	void StartParticles(CParticlesObject *&pParticles, LPCSTR particles_name, const Fvector &pos, const Fvector &vel = zero_vel, bool auto_remove_flag = false);
	void StopParticles(CParticlesObject *&pParticles);
	void UpdateParticles(CParticlesObject *&pParticles, const Fvector &pos, const Fvector &vel = zero_vel);
	void LoadShellParticles(LPCSTR section, LPCSTR prefix);
	void LoadFlameParticles(LPCSTR section, LPCSTR prefix);
	void StartFlameParticles();
	void StopFlameParticles();
	void UpdateFlameParticles();
	void StartSmokeParticles(const Fvector &play_pos, const Fvector &parent_vel);
	void LoadLights(LPCSTR section, LPCSTR prefix);
	void Light_Create();
	void Light_Destroy();
	void Light_Start();
	void Light_Render(const Fvector &P);
	void RenderLight();
	void UpdateLight();
	void StopLight();
	void OnShellDrop(const Fvector &play_pos, const Fvector &parent_vel);
	void SetAmmoElapsed(int num);
};
#endif

class CWeaponStatMgun : public CPhysicsShellHolder,
                        public CHolderCustom,
#ifdef STATIONARYMGUN_NEW
						public CPHUpdateObject,
						public CPHSkeleton,
#endif
                        public CShootingObject
{
private:
	typedef CPhysicsShellHolder inheritedPH;
	typedef CHolderCustom inheritedHolder;
	typedef CShootingObject inheritedShooting;

private:
#ifdef STATIONARYMGUN_NEW
	enum EStmCamType
	{
		eCamFirst = 0,
		eCamChase,
		eCamSize,
	};
	CCameraBase *camera[eCamSize];
	CCameraBase *active_camera;
#else
	CCameraBase* camera;
#endif
	// 
	static void _BCL BoneCallbackX(CBoneInstance* B);
	static void _BCL BoneCallbackY(CBoneInstance* B);
	void SetBoneCallbacks();
	void ResetBoneCallbacks();

	HUD_SOUND_COLLECTION_LAYERED m_sounds;

	//casts
public:
	virtual CHolderCustom* cast_holder_custom() { return this; }

	//general
public:
	CWeaponStatMgun();
	virtual ~CWeaponStatMgun();

	virtual void Load(LPCSTR section);

	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void net_Destroy();
	virtual void net_Export(NET_Packet& P); // export to server
	virtual void net_Import(NET_Packet& P); // import from server

	virtual void UpdateCL();

	virtual void Hit(SHit* pHDS);

	//shooting
private:
	u16 m_rotate_x_bone, m_rotate_y_bone, m_fire_bone, m_camera_bone;
	float m_tgt_x_rot, m_tgt_y_rot, m_cur_x_rot, m_cur_y_rot, m_bind_x_rot, m_bind_y_rot;
	Fvector m_bind_x, m_bind_y;
	Fvector m_fire_dir, m_fire_pos;

	Fmatrix m_i_bind_x_xform, m_i_bind_y_xform, m_fire_bone_xform;
	Fvector2 m_lim_x_rot, m_lim_y_rot; //in bone space
	CCartridge* m_Ammo;
	float m_barrel_speed;
	Fvector2 m_dAngle;
	Fvector m_destEnemyDir;
	bool m_allow_fire;
	float camRelaxSpeed;
	float camMaxAngle;

	bool m_firing_disabled;
	bool m_overheat_enabled;
	float m_overheat_value;
	float m_overheat_time_quant;
	float m_overheat_decr_quant;
	float m_overheat_threshold;
	shared_str m_overheat_particles;
	CParticlesObject* p_overheat;
protected:
	void UpdateBarrelDir();
	virtual const Fvector& get_CurrentFirePoint();
	virtual const Fmatrix& get_ParticlesXFORM();

	virtual void FireStart();
	virtual void FireEnd();
	virtual void UpdateFire();
	virtual void OnShot();
	void AddShotEffector();
	void RemoveShotEffector();
	void SetDesiredDir(float h, float p);
	void ClampRotationHorz(float &tgt_val, const float &cur_val, const float &lim_min, const float &lim_max);
	virtual bool IsHudModeNow() { return false; };

	//HolderCustom
public:
#ifdef STATIONARYMGUN_NEW
	virtual bool Use(const Fvector &pos, const Fvector &dir, const Fvector &foot_pos);
#else
	virtual bool Use(const Fvector& pos, const Fvector& dir, const Fvector& foot_pos) { return !Owner(); };
#endif
	virtual void OnMouseMove(int x, int y);
	virtual void OnKeyboardPress(int dik);
	virtual void OnKeyboardRelease(int dik);
	virtual void OnKeyboardHold(int dik);
#ifdef STATIONARYMGUN_NEW
	CInventory *GetInventory();
#else
	virtual CInventory* GetInventory() { return NULL; };
#endif
	virtual void cam_Update(float dt, float fov = 90.0f);

	virtual void renderable_Render();

	virtual bool attach_Actor(CGameObject* actor);
	virtual void detach_Actor();
	virtual bool allowWeapon() const { return false; };
	virtual bool HUDView() const { return true; };
	virtual Fvector ExitPosition();

#ifdef STATIONARYMGUN_NEW
	virtual CCameraBase *Camera() { return active_camera; }
#else
	virtual CCameraBase* Camera() { return camera; };
#endif

	virtual void Action(u16 id, u32 flags);
	virtual void SetParam(int id, Fvector2 val);

#ifdef STATIONARYMGUN_NEW
private:
	bool m_bActive;
	Fvector2 m_desire_angle;
	bool m_desire_angle_enable;

	float m_rotate_x_speed;
	float m_rotate_y_speed;
	u16 m_drop_bone;
	u16 m_actor_bone;
	Fvector m_exit_position;
	Fvector m_actor_offsets;

	u16 m_camera_bone_def;
	u16 m_camera_bone_aim;
	float m_zoom_factor_def;
	float m_zoom_factor_aim;
	Fvector m_camera_position;
	bool m_zoom_status;

	struct SStmAnimActor
	{
		enum
		{
			eAnimBody = 0,
			eAnimLegs,
			eAnimSize,
		};
		shared_str m_idle[eAnimSize];
		SStmAnimActor();
		LPCSTR GetAnimation(u8 id);
		void SetAnimation(u8 id, LPCSTR anim);
	};
	SStmAnimActor m_animation;

	float fireDispersionOwnerScale;
	LPCSTR m_on_before_use_callback;
    shared_str m_on_range_fov_callback;

	void CreateSkeleton(CSE_Abstract *po);
	virtual void PhDataUpdate(float step) {};
	virtual void PhTune(float step) {};
	virtual CPhysicsShellHolder *PPhysicsShellHolder() { return static_cast<CPhysicsShellHolder *>(this); }
	static void IgnoreOwnerCallback(bool &do_colide, bool bo1, dContact &c, SGameMtl *mt1, SGameMtl *mt2);
	void OnCameraChange(u16 type);
	void UpdateCamera();

	xr_vector<SStmBarrel> m_barrels;
	bool BarrelAllowFire(SStmBarrel &B);
	int BarrelAmmoElapsed();
	void BarrelAmmoElapsedCorrecting();

protected:
	virtual void SpawnInitPhysics(CSE_Abstract *D);
	virtual void net_Save(NET_Packet &P);
	virtual BOOL net_SaveRelevant();

public:
	enum
	{
		eWpnActivate = 0,
		eWpnFire,
		eWpnDesiredPos,
		eWpnDesiredDir,
		eWpnDesiredAng,
		eWpnReload,
	};
	virtual void SetParam(int id, Fvector val);
	IC BOOL IsWorking() { return CShootingObject::IsWorking(); }
	virtual void UpdateEx(float fov);
	virtual void shedule_Update(u32 dt);

	virtual BOOL AlwaysTheCrow();
	virtual bool is_ai_obstacle() const;
	virtual CPhysicsShellHolder *cast_physics_shell_holder() { return this; }

	IC bool IsActive() { return m_bActive; }
	CScriptGameObject *GetOwner() { return (Owner()) ? Owner()->lua_game_object() : nullptr; }
	Fvector GetFirePos() { return m_fire_pos; }
	Fvector GetFireDir() { return m_fire_dir; }
	float FireDispersionBase() { return fireDispersionBase; }
	bool IsCameraZoom();
	void SetFeelVisionIgnore(bool enable);
	void UpdateAnimation();
	Fvector2 GetTraverseLimitHorz();
	void SetTraverseLimitHorz(Fvector2 vec);
	Fvector2 GetTraverseLimitVert();
	void SetTraverseLimitVert(Fvector2 vec);
	Fvector GetActorOffsets() { return m_actor_offsets; }
	void SetActorOffsets(Fvector vec) { m_actor_offsets.set(vec); }
	LPCSTR GetAnimation(int id) { return m_animation.GetAnimation(id); }
	void SetAnimation(int id, LPCSTR anim) { m_animation.SetAnimation(id, anim); }

    void OverrideRangeFOV(const CGameObject* npc, float& range);

	/* Barrels APIs */
	SStmBarrel *Barrel(LPCSTR name);
	float BarrelRPM(LPCSTR name);

	/*----------------------------------------------------------------------------------------------------
		Weapon animations
	----------------------------------------------------------------------------------------------------*/
	struct SStmAnimWeapon
	{
		enum EStmAnimWeapon
		{
			eStmAnimWeapon_idle = 0,
			eStmAnimWeapon_shot,
			eStmAnimWeapon_reload0,
			eStmAnimWeapon_reload1,
			eStmAnimWeapon_chamber,
			eStmAnimWeapon_size
		};
		CWeaponStatMgun *m_stm;
		xr_vector<MotionID> m_anims[eStmAnimWeapon_size];
		u8 m_current_anm;
		u8 m_current_idx;
		MotionID m_current_mid;

		SStmAnimWeapon();
		~SStmAnimWeapon();
		void Init(CWeaponStatMgun *stm);
		BOOL net_Spawn(CSE_Abstract *DC);
		void Play(u8 anim);
		void OnAnimationEnd();
		static void AnimationCallback(CBlend *B);

		const LPCSTR m_hand_atm = "stm_hand_atm";
		u16 m_hand_bid;
		Fvector m_hand_pos;
		shared_str m_hand_vis;
		shared_str m_hand_anims[eStmAnimWeapon_size];

		bool HandGetVisualName();
		void HandCreate();
		void HandRemove();
		void HandPlay(u8 anim);

		u16 m_magazine_hide_bid;
		xr_vector<MotionID> m_magazine_hide_anm;
		void UpdateMagazineVisibility();
#if 0
		/*
		Discarded magazine. Unfinished.
		Do something like CCustomRocket and CPhysicsSkeletonObject but create a new class instead of reusing them.
		*/
		u8 m_reload0_mag_idx;
		u8 m_reload1_mag_idx;
		shared_str m_magazine_sec;
		Fvector m_magazine_dir;
		float m_magazine_vel;
		xr_vector<CCustomRocket *> m_magazine_object;
		xr_vector<CCustomRocket *> m_magazine_launch;
		bool IsMagazine(CObject *O);
		void CreateMagazine();
		void LaunchMagazine(CObject *O);
		void AttachMagazine(u16 id, CGameObject *parent);
		void DetachMagazine(u16 id);
#endif
	} m_anim_weapon;
	virtual void OnEvent(NET_Packet &P, u16 type);

	/*----------------------------------------------------------------------------------------------------
		Sounds
	----------------------------------------------------------------------------------------------------*/
	struct SStmSound
	{
		CWeaponStatMgun *m_stm;
		float m_volume;

		ref_sound m_rotate_snd;
		u16 m_rotate_bid;
		bool m_rotating_x;
		bool m_rotating_y;

		void Init(CWeaponStatMgun *stm);
		BOOL net_Spawn(CSE_Abstract *DC);
		void Update();
		void RotatePlay(bool state);
		void RotateVolume(float vol);

		SStmSound();
		~SStmSound();
	} m_sound_mgr;
	void UpdateSound();

	/*----------------------------------------------------------------------------------------------------
		Ammo
	----------------------------------------------------------------------------------------------------*/
private:
	bool m_unlimited_ammo;
	LPCSTR m_reload_consume_callback;
	bool IsUnlimitedAmmo() { return m_unlimited_ammo; }
	bool IsReloadConsume();

	u16 m_state_index;
	float m_state_delay;

	xr_vector<u16> m_bullet_bones;
	u16 m_bullet_count;
	void UpdateBulletVisibility(u16 num);

	shared_str m_shot_effector;

public:
	enum eState
	{
		eStateIdle = 0,
		eStateFire,
		eStateReload,
	};

protected:
	virtual void OnShot(SStmBarrel &B);
	IC u16 GetState() const { return m_state_index; }
	IC float GetStateDelay() const { return m_state_delay; }
	virtual void SwitchState(u16 new_tate);
	virtual void switch2_Idle();
	virtual void switch2_Fire();
	virtual void switch2_Reload();

	virtual void UpdateState();
	virtual void UpdateReload();

public:
	struct SStmNextAmmoTypeOnReload
	{
		enum
		{
			undefined = u8(-1),
		};
		u8 m_ammo_type;
		u8 get() { return m_ammo_type; }
		void set(u8 ammo_type) { m_ammo_type = ammo_type; }
		void reset() { m_ammo_type = undefined; }
		bool valid() { return m_ammo_type != undefined; }
	} m_next_ammoType_on_reload;

	u8 m_ammoType;
	xr_vector<shared_str> m_ammoTypes;

	xr_vector<CCartridge> m_magazine;
	CCartridge m_DefaultCartridge;
	CCartridge m_lastCartridge;
	float m_fCurrentCartirdgeDisp;

	int iAmmoElapsed;
	int iMagazineSize;

	virtual void ReloadMagazine();
	virtual void UnloadMagazine(bool spawn_ammo = true);
	virtual bool GetBriefInfo(II_BriefInfo &info);
	virtual void SetNextAmmoTypeOnReload(u8 ammo_type = SStmNextAmmoTypeOnReload::undefined);

protected:
	int m_iShotNum;
	CWeaponAmmo *m_pCurrentAmmo;
	bool m_bLockType;
	float m_reload_delay;
	bool m_single_shot_wpn;

	int GetAmmoCount(u8 ammo_type);
	int GetAmmoCount_allType();
	virtual u16 AddCartridge(u16 cnt);
	void SpawnAmmo(u32 boxCurr = 0xffffffff, LPCSTR ammoSect = NULL, u32 ParentID = 0xffffffff);

public:
	IC int GetAmmoMagSize() const
	{
		return iMagazineSize;
	}

	IC int GetAmmoElapsed() const
	{
		return iAmmoElapsed;
	}
	void SetAmmoElapsed(int ammo_count);

	IC u8 HasAmmoType(u8 type) const
	{
		return type < m_ammoTypes.size();
	}
	IC u8 GetAmmoType() const
	{
		return m_ammoType;
	}
	void SetAmmoType(u8 type);

	int GetAmmoCount_forType(shared_str const &ammo_type);

	float GetBaseDispersion(float cartridge_k);
	float GetFireDispersion(bool with_cartridge, bool for_crosshair = false);
	virtual float GetFireDispersion(float cartridge_k, bool for_crosshair = false);
	float GetFireDispersionScript(bool wc = true, bool fc = false);

	float GetReloadDelay() { return m_reload_delay; }

public:
DECLARE_SCRIPT_REGISTER_FUNCTION
#endif

};
