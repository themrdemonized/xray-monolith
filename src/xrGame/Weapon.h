#pragma once

#include "../xrphysics/PhysicsShell.h"
#include "weaponammo.h"
#include "PHShellCreator.h"

#include "ShootingObject.h"
#include "hud_item_object.h"
#include "Actor_Flags.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "firedeps.h"
#include "game_cl_single.h"
#include "first_bullet_controller.h"

#include "CameraRecoil.h"

#include "NewZoomFlag.h"

class CEntity;
class ENGINE_API CMotionDef;
class CSE_ALifeItemWeapon;
class CSE_ALifeItemWeaponAmmo;
class CWeaponMagazined;
class CParticlesObject;
class CUIWindow;
class CBinocularsVision;
class CNightVisionEffector;

extern float f_weapon_deterioration;

extern xr_map<shared_str, float> listScopeRadii;

extern float scope_scrollpower;
extern float sens_multiple;

struct PickParam
{
	collide::rq_result RQ;
	float power;
	u32 pass;
};

struct SafemodeAnm
{
	LPCSTR name;
	float power, speed;
};

class CWeapon : public CHudItemObject,
                public CShootingObject
{
private:
	typedef CHudItemObject inherited;

public:
	CWeapon();
	virtual ~CWeapon();

	// Generic
	virtual void Load(LPCSTR section);

	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void net_Destroy();
	virtual void net_Export(NET_Packet& P);
	virtual void net_Import(NET_Packet& P);
	virtual void net_Relcase(CObject* object) override;

	virtual CWeapon* cast_weapon()
	{
		return this;
	}

	virtual CWeaponMagazined* cast_weapon_magazined()
	{
		return 0;
	}

	//serialization
	virtual void save(NET_Packet& output_packet);
	virtual void load(IReader& input_packet);

	virtual BOOL net_SaveRelevant()
	{
		return inherited::net_SaveRelevant();
	}

	float CWeapon::GetSecondVPFov() const;
	IC float GetZRotatingFactor()    const { return m_zoom_params.m_fZoomRotationFactor; }
	IC float GetSecondVPZoomFactor() const { return m_zoom_params.m_fSecondVPFovFactor; }
	IC float IsSecondVPZoomPresent() const { return GetSecondVPZoomFactor() > 0.005f; }

	// Up
	// Magazine system & etc
	xr_vector<shared_str> bullets_bones;
	int bullet_cnt;
	int last_hide_bullet;
	bool bHasBulletsToHide;

	//--DSR-- SilencerOverheat_start
	float temperature;

	virtual void FireBullet(const Fvector& pos,
		const Fvector& shot_dir,
		float fire_disp,
		const CCartridge& cartridge,
		u16 parent_id,
		u16 weapon_id,
		bool send_hit, int iShotNum);

	virtual float GetGlowing();
	//--DSR-- SilencerOverheat_end

	virtual void HUD_VisualBulletUpdate(bool force = false, int force_idx = -1);

	void UpdateSecondVP();

	virtual void UpdateCL();
	virtual void shedule_Update(u32 dt);

	virtual void renderable_Render();
	virtual void render_hud_mode();
	virtual bool need_renderable();

	virtual void render_item_ui();
	virtual bool render_item_ui_query();

	virtual void OnH_B_Chield();
	virtual void OnH_A_Chield();
	virtual void OnH_B_Independent(bool just_before_destroy);
	virtual void OnH_A_Independent();
	virtual void OnEvent(NET_Packet& P, u16 type); // {inherited::OnEvent(P,type);}

	virtual void Hit(SHit* pHDS);

	virtual void reinit();
	virtual void reload(LPCSTR section);

	// demonized: World model on stalkers adjustments
	void set_mOffset(Fvector position, Fvector orientation);
	void set_mStrapOffset(Fvector position, Fvector orientation);
	void set_mFirePoint(Fvector &fire_point);
	void set_mFirePoint2(Fvector &fire_point);
	void set_mShellPoint(Fvector &fire_point);
	Fmatrix get_mOffset() { return m_Offset; };
	Fmatrix get_mStrapOffset() { return m_StrapOffset; };

	virtual void create_physic_shell();
	virtual void activate_physic_shell();
	virtual void setup_physic_shell();

	virtual void SwitchState(u32 S);

	virtual void OnActiveItem();
	virtual void OnHiddenItem();
	virtual void SendHiddenItem(); //same as OnHiddenItem but for client... (sends message to a server)...

	virtual bool NeedBlendAnm();
	virtual void OnEmptyClick() {};

public:
	virtual bool can_kill() const;
	virtual CInventoryItem* can_kill(CInventory* inventory) const;
	virtual const CInventoryItem* can_kill(const xr_vector<const CGameObject*>& items) const;
	virtual bool ready_to_kill() const;
	virtual bool NeedToDestroyObject() const;
	virtual ALife::_TIME_ID TimePassedAfterIndependant() const;
protected:
	//âðåìÿ óäàëåíèÿ îðóæèÿ
	ALife::_TIME_ID m_dwWeaponRemoveTime;
	ALife::_TIME_ID m_dwWeaponIndependencyTime;

	virtual bool IsHudModeNow();
	virtual bool SOParentIsActor() { return ParentIsActor(); }
	u8 last_idx;

	CAnonHudItem* m_scopeItem = NULL;
public:
	void signal_HideComplete();
	virtual bool Action(u16 cmd, u32 flags);

	enum EWeaponStates
	{
		eFire = eLastBaseState + 1,
		eFire2,
		eReload,
		eMisfire,
		eSwitch,
		eSwitchMode,
		eAimStart,
		eAimEnd,
	};

	enum EWeaponSubStates
	{
		eSubstateReloadBegin = 0,
		eSubstateReloadInProcess,
		eSubstateReloadEnd,
		eSubstateReloadInProcessEmptyEnd,
	};

	enum
	{
		undefined_ammo_type = u8(-1)
	};

	IC BOOL IsValid() const
	{
		return iAmmoElapsed;
	}

	// Does weapon need's update?
	BOOL IsUpdating();

	bool IsMisfire() const;
	void SetMisfireScript(bool b);
	BOOL CheckForMisfire();

	BOOL AutoSpawnAmmo() const
	{
		return m_bAutoSpawnAmmo;
	};

	bool IsTriStateReload() const
	{
		return m_bTriStateReload;
	}

	EWeaponSubStates GetReloadState() const
	{
		return (EWeaponSubStates)m_sub_state;
	}

protected:
	bool m_bTriStateReload;

	// a misfire happens, you'll need to rearm weapon
	bool bMisfire;
	bool bClearJamOnly; //used for "reload" misfire animation

	bool m_playFullShotAnim;

	BOOL m_bAutoSpawnAmmo;
	virtual bool AllowBore();
public:
	u8 m_sub_state;

	bool IsCustomReloadAvaible;

	bool IsGrenadeLauncherAttached() const;
	bool IsScopeAttached() const;
	bool IsSilencerAttached() const;

	virtual bool GrenadeLauncherAttachable();
	virtual bool ScopeAttachable();
	virtual bool SilencerAttachable();

	ALife::EWeaponAddonStatus get_GrenadeLauncherStatus() const
	{
		return m_eGrenadeLauncherStatus;
	}

	ALife::EWeaponAddonStatus get_ScopeStatus() const
	{
		return m_eScopeStatus;
	}

	ALife::EWeaponAddonStatus get_SilencerStatus() const
	{
		return m_eSilencerStatus;
	}

	virtual bool UseScopeTexture()
	{
		return true;
	};

	//îáíîâëåíèå âèäèìîñòè äëÿ êîñòî÷åê àääîíîâ
	void UpdateAddonsVisibility();
	void UpdateHUDAddonsVisibility();
	//èíèöèàëèçàöèÿ ñâîéñòâ ïðèñîåäèíåííûõ àääîíîâ
	virtual void InitAddons();

	//äëÿ îòîáðîàæåíèÿ èêîíîê àïãðåéäîâ â èíòåðôåéñå
	int GetScopeX()
	{
		return pSettings->r_s32(m_scopes[m_cur_scope], "scope_x");
	}

	int GetScopeY()
	{
		return pSettings->r_s32(m_scopes[m_cur_scope], "scope_y");
	}

	int GetSilencerX()
	{
		return m_iSilencerX;
	}

	int GetSilencerY()
	{
		return m_iSilencerY;
	}

	int GetGrenadeLauncherX()
	{
		return m_iGrenadeLauncherX;
	}

	int GetGrenadeLauncherY()
	{
		return m_iGrenadeLauncherY;
	}

	const shared_str& GetGrenadeLauncherName() const
	{
		return m_sGrenadeLauncherName;
	}

	const shared_str GetScopeName() const
	{
		if (m_scopes.size() < 1)
		{
			return {};
		}
		if (m_modular_attachments) {
			return m_scopes[m_cur_scope];
		}
		return pSettings->r_string(m_scopes[m_cur_scope], "scope_name");
	}

	const shared_str& GetSilencerName() const
	{
		return m_sSilencerName;
	}

	IC void ForceUpdateAmmo()
	{
		m_BriefInfo_CalcFrame = 0;
	}

	u8 GetAddonsState() const
	{
		return m_flagsAddOnState;
	};

	void SetAddonsState(u8 st)
	{
		m_flagsAddOnState = st;
	} //dont use!!! for buy menu only!!!
	
protected:
	//ñîñòîÿíèå ïîäêëþ÷åííûõ àääîíîâ
	u8 m_flagsAddOnState;

	//âîçìîæíîñòü ïîäêëþ÷åíèÿ ðàçëè÷íûõ àääîíîâ
	ALife::EWeaponAddonStatus m_eScopeStatus;
	ALife::EWeaponAddonStatus m_eSilencerStatus;
	ALife::EWeaponAddonStatus m_eGrenadeLauncherStatus;


	shared_str m_sScopeName;
	shared_str m_sSilencerName;
	shared_str m_sGrenadeLauncherName;

	//ñìåùåíèå èêîíîâ àïãðåéäîâ â èíâåíòàðå
	int m_iScopeX, m_iScopeY;
	int m_iSilencerX, m_iSilencerY;
	int m_iGrenadeLauncherX, m_iGrenadeLauncherY;

protected:

	struct SZoomParams
	{
        float m_fMinBaseZoomFactor;
		float m_fZoomStepCount;
		bool m_bZoomEnabled;
		bool m_bHideCrosshairInZoom;
		bool m_bZoomDofEnabled;
		bool m_bIsZoomModeNow;
		float m_fCurrentZoomFactor;
		float m_fZoomRotateTime;
		float m_fBaseZoomFactor;
		float m_fScopeZoomFactor;
		float m_fZoomRotationFactor;
		float m_fSecondVPFovFactor;
		Fvector m_ZoomDof;
		Fvector4 m_ReloadDof;
		Fvector4 m_ReloadEmptyDof; //Swartz: reload when empty mag. DOF
		BOOL m_bUseDynamicZoom;
		BOOL m_bUseDynamicZoom_Primary;
		BOOL m_bUseDynamicZoom_Alt;
		BOOL m_bUseDynamicZoom_GL;
		shared_str m_sUseZoomPostprocess;
		shared_str m_sUseBinocularVision;
		CBinocularsVision* m_pVision;
		CNightVisionEffector* m_pNight_vision;
	} m_zoom_params;

	float m_fRTZoomFactor; //run-time zoom factor
	CUIWindow* m_UIScope;

private:
	bool firstZoomDone;

public:

	IC bool IsZoomEnabled() const
	{
		return m_zoom_params.m_bZoomEnabled;
	}

	virtual float GetMinScopeZoomFactor() const;
	virtual void ZoomInc();
	virtual void ZoomDec();
	virtual void OnZoomIn();
	virtual void OnZoomOut();
	IC bool IsZoomed() const
	{
		return m_zoom_params.m_bIsZoomModeNow;
	};
	CUIWindow* ZoomTexture();

	bool ZoomHideCrosshair();

	IC float GetZoomFactor() const
	{
		return m_zoom_params.m_fCurrentZoomFactor;
	}

	IC void SetZoomFactor(float f)
	{
		m_zoom_params.m_fCurrentZoomFactor = f;
	}

	virtual float CurrentZoomFactor();
	//ïîêàçûâàåò, ÷òî îðóæèå íàõîäèòñÿ â ñîîñòîÿíèè ïîâîðîòà äëÿ ïðèáëèæåííîãî ïðèöåëèâàíèÿ
	bool IsRotatingToZoom() const
	{
		return (m_zoom_params.m_fZoomRotationFactor < 1.f);
	}

	virtual u8 GetCurrentHudOffsetIdx();

	// Tronex script exports
	void AmmoTypeForEach(const ::luabind::functor<bool>& funct);
	float GetMagazineWeightScript() const { return GetMagazineWeight(m_magazine); }
	int GetAmmoCount_forType_Script(LPCSTR type) const { return GetAmmoCount_forType(type); }
	LPCSTR GetGrenadeLauncherNameScript() const { return *GetGrenadeLauncherName(); }
	LPCSTR GetSilencerNameScript() const { return *GetSilencerName(); }
	LPCSTR GetScopeNameScript() const { return *GetScopeName(); }
	float GetFireDispersionScript() const { return fireDispersionBase; }
	float RPMScript() const { return fOneShotTime; }
	float RealRPMScript() const { return 60.0f / fOneShotTime; } // Return actual RPM like in configs
	float ModeRPMScript() const { return fModeShotTime; }
	float ModeRealRPMScript() const { return 60.0f / fModeShotTime; }

	//Setters
	void SetFireDispersionScript(float val) { fireDispersionBase = val; }
	void SetRPM(float newOneShotTime) { fOneShotTime = newOneShotTime; } // Input - time between shots like received from getter
	void SetRealRPM(float rpm) { fOneShotTime = 60.0f / rpm; } // Input - actual RPM like in configs
	void SetModeRPM(float newOneShotTime) { fModeShotTime = newOneShotTime; } // Input - time between shots like received from getter
	void SetModeRealRPM(float rpm) { fModeShotTime = 60.0f / rpm; } // Input - actual RPM like in configs

	virtual float Weight() const;
	virtual u32 Cost() const;
public:
	virtual EHandDependence HandDependence() const
	{
		return eHandDependence;
	}

	bool IsSingleHanded() const
	{
		return m_bIsSingleHanded;
	}

public:
	IC LPCSTR strap_bone0() const
	{
		return m_strap_bone0;
	}

	IC LPCSTR strap_bone1() const
	{
		return m_strap_bone1;
	}

	IC void strapped_mode(bool value)
	{
		m_strapped_mode = value;
	}

	IC bool strapped_mode() const
	{
		return m_strapped_mode;
	}

protected:
	LPCSTR m_strap_bone0;
	LPCSTR m_strap_bone1;
	Fmatrix m_StrapOffset;
	bool m_strapped_mode;
	bool m_can_be_strapped;
	bool isGrenadeLauncherActive = false;
	u8 zoomTypeBeforeLauncher = 0;
	float m_fSafeModeRotateTime;
	SafemodeAnm m_safemode_anm[2];

	Fmatrix m_Offset;
	Fvector m_hud_offset[2];
	Fvector m_hud_aim_rot;
	// 0-èñïîëüçóåòñÿ áåç ó÷àñòèÿ ðóê, 1-îäíà ðóêà, 2-äâå ðóêè
	EHandDependence eHandDependence;
	bool m_bIsSingleHanded;

public:
	//çàãðóæàåìûå ïàðàìåòðû
	Fvector vLoadedFirePoint;
	Fvector vLoadedFirePoint2;
	Fvector vLoadedFirePointSilencer;
	bool m_bCanBeLowered;
	bool m_modular_attachments;

private:
	firedeps m_current_firedeps;

public:
	Fmatrix m_shoot_shake_mat;
	void UpdateZoomParams();

	void SetUIScope(LPCSTR scope_texture);
	shared_str m_scope_tex_name;
	shared_str m_primary_scope_tex_name;
	shared_str m_secondary_scope_tex_name;

private:
	float m_nearwall_zoomed_range;
	bool m_firepos;
	bool m_aimpos;

public:
	bool GetFirepos() { return m_firepos; }
	bool GetAimpos() { return m_aimpos; }
	float GetTargetNearWallOffset();
	float GetTargetHudFov();

public:
	Fmatrix RayTransform();

protected:
	virtual void UpdateFireDependencies_internal();
	void UpdateUIScope();
	void SwitchZoomType();
	void ToggleGrenadeLauncher();
    void SetZoomType(u8 new_zoom_type);
	void SetZoomTypeAndParams(u8 zoomType);
	virtual void UpdatePosition(const Fmatrix& transform); //.
	virtual void UpdateXForm();
	void InterpolateOffset(Fvector& current, const Fvector& target, const float factor) const;
	virtual void UpdateHudAdditional(Fmatrix& trans);
	IC void UpdateFireDependencies()
	{
		if (dwFP_Frame == Device.dwFrame) return;
		UpdateFireDependencies_internal();
	};

	virtual void LoadFireParams(LPCSTR section);
public:
	IC const Fvector& get_LastFP()
	{
		UpdateFireDependencies();
		return m_current_firedeps.vLastFP;
	}

	IC const Fvector& get_LastFP2()
	{
		UpdateFireDependencies();
		return m_current_firedeps.vLastFP2;
	}

	IC const Fvector& get_LastFPSilencer()
	{
		UpdateFireDependencies();
		return m_current_firedeps.vLastFPSilencer;
	}

	IC const Fvector& get_LastFD()
	{
		UpdateFireDependencies();
		return m_current_firedeps.vLastFD;
	}

	IC const Fvector& get_LastSP()
	{
		UpdateFireDependencies();
		return m_current_firedeps.vLastSP;
	}

	virtual const Fvector& get_CurrentFirePoint()
	{
		if (SilencerAttachable() && IsSilencerAttached()) {
			return get_LastFPSilencer();
		}
		return get_LastFP();
	}

	virtual const Fvector& get_CurrentFirePoint2()
	{
		return get_LastFP2();
	}

	virtual const Fvector& get_CurrentFirePointSilencer()
	{
		return get_LastFPSilencer();
	}

	virtual const Fmatrix& get_ParticlesXFORM()
	{
		UpdateFireDependencies();
		return m_current_firedeps.m_FireParticlesXForm;
	}

	virtual void ForceUpdateFireParticles();
	virtual void debug_draw_firedeps();

protected:
	virtual void SetDefaults();

	virtual bool MovingAnimAllowedNow();
	virtual void OnStateSwitch(u32 S, u32 oldState);
	virtual void OnAnimationEnd(u32 state);

	//òðàññèðîâàíèå ïîëåòà ïóëè
	virtual void FireTrace(const Fvector& P, const Fvector& D);
	virtual float GetWeaponDeterioration();

	virtual void FireStart();

	virtual void FireEnd();

	virtual void Reload();
	void StopShooting();

	// îáðàáîòêà âèçóàëèçàöèè âûñòðåëà
	virtual void OnShot()
	{
	};
	virtual void AddShotEffector();
	virtual void RemoveShotEffector();
	virtual void ClearShotEffector();
	virtual void StopShotEffector();

public:
	float GetBaseDispersion(float cartridge_k);
	float GetFireDispersion(bool with_cartridge, bool for_crosshair = false);
	virtual float GetFireDispersion(float cartridge_k, bool for_crosshair = false);

	virtual int ShotsFired()
	{
		return 0;
	}

	virtual int GetCurrentFireMode()
	{
		return 1;
	}

	//ïàðàìåòû îðóæèÿ â çàâèñèìîòè îò åãî ñîñòîÿíèÿ èñïðàâíîñòè
	float GetConditionDispersionFactor() const;
	float GetConditionMisfireProbability() const;
	virtual float GetConditionToShow() const;

public:
	CameraRecoil cam_recoil; // simple mode (walk, run)
	CameraRecoil zoom_cam_recoil; // using zoom =(ironsight or scope)

	// Getters
	float GetCamRelaxSpeed() { return cam_recoil.RelaxSpeed; };
	float GetCamRelaxSpeed_AI() { return cam_recoil.RelaxSpeed_AI; };
	float GetCamDispersion() { return cam_recoil.Dispersion; };
	float GetCamDispersionInc() { return cam_recoil.DispersionInc; };
	float GetCamDispersionFrac() { return cam_recoil.DispersionFrac; };
	float GetCamMaxAngleVert() { return cam_recoil.MaxAngleVert; };
	float GetCamMaxAngleHorz() { return cam_recoil.MaxAngleHorz; };
	float GetCamStepAngleHorz() { return cam_recoil.StepAngleHorz; };
	float GetZoomCamRelaxSpeed() { return zoom_cam_recoil.RelaxSpeed; };
	float GetZoomCamRelaxSpeed_AI() { return zoom_cam_recoil.RelaxSpeed_AI; };
	float GetZoomCamDispersion() { return zoom_cam_recoil.Dispersion; };
	float GetZoomCamDispersionInc() { return zoom_cam_recoil.DispersionInc; };
	float GetZoomCamDispersionFrac() { return zoom_cam_recoil.DispersionFrac; };
	float GetZoomCamMaxAngleVert() { return zoom_cam_recoil.MaxAngleVert; };
	float GetZoomCamMaxAngleHorz() { return zoom_cam_recoil.MaxAngleHorz; };
	float GetZoomCamStepAngleHorz() { return zoom_cam_recoil.StepAngleHorz; };

	// Setters
	void SetCamRelaxSpeed(float val) { cam_recoil.RelaxSpeed = val; };
	void SetCamRelaxSpeed_AI(float val) { cam_recoil.RelaxSpeed_AI = val; };
	void SetCamDispersion(float val) { cam_recoil.Dispersion = val; };
	void SetCamDispersionInc(float val) { cam_recoil.DispersionInc = val; };
	void SetCamDispersionFrac(float val) { cam_recoil.DispersionFrac = val; };
	void SetCamMaxAngleVert(float val) { cam_recoil.MaxAngleVert = val; };
	void SetCamMaxAngleHorz(float val) { cam_recoil.MaxAngleHorz = val; };
	void SetCamStepAngleHorz(float val) { cam_recoil.StepAngleHorz = val; };
	void SetZoomCamRelaxSpeed(float val) { zoom_cam_recoil.RelaxSpeed = val; };
	void SetZoomCamRelaxSpeed_AI(float val) { zoom_cam_recoil.RelaxSpeed_AI = val; };
	void SetZoomCamDispersion(float val) { zoom_cam_recoil.Dispersion = val; };
	void SetZoomCamDispersionInc(float val) { zoom_cam_recoil.DispersionInc = val; };
	void SetZoomCamDispersionFrac(float val) { zoom_cam_recoil.DispersionFrac = val; };
	void SetZoomCamMaxAngleVert(float val) { zoom_cam_recoil.MaxAngleVert = val; };
	void SetZoomCamMaxAngleHorz(float val) { zoom_cam_recoil.MaxAngleHorz = val; };
	void SetZoomCamStepAngleHorz(float val) { zoom_cam_recoil.StepAngleHorz = val; };

protected:
	//ôàêòîð óâåëè÷åíèÿ äèñïåðñèè ïðè ìàêñèìàëüíîé èçíîøåíîñòè
	//(íà ñêîëüêî ïðîöåíòîâ óâåëè÷èòñÿ äèñïåðñèÿ)
	float fireDispersionConditionFactor;
	//âåðîÿòíîñòü îñå÷êè ïðè ìàêñèìàëüíîé èçíîøåíîñòè

	// modified by Peacemaker [17.10.08]
	//	float					misfireProbability;
	//	float					misfireConditionK;
	float misfireStartCondition; //èçíîøåííîñòü, ïðè êîòîðîé ïîÿâëÿåòñÿ øàíñ îñå÷êè
	float misfireEndCondition; //èçíîøåíîñòü ïðè êîòîðîé øàíñ îñå÷êè ñòàíîâèòñÿ êîíñòàíòíûì
	float misfireStartProbability; //øàíñ îñå÷êè ïðè èçíîøåíîñòè áîëüøå ÷åì misfireStartCondition
	float misfireEndProbability; //øàíñ îñå÷êè ïðè èçíîøåíîñòè áîëüøå ÷åì misfireEndCondition
	float conditionDecreasePerQueueShot; //óâåëè÷åíèå èçíîøåíîñòè ïðè âûñòðåëå î÷åðåäüþ
	float conditionDecreasePerShot; //óâåëè÷åíèå èçíîøåíîñòè ïðè îäèíî÷íîì âûñòðåëå

public:
	float GetMisfireStartCondition() const
	{
		return misfireStartCondition;
	};

	float GetMisfireEndCondition() const
	{
		return misfireEndCondition;
	};

	// Setters
	void SetMisfireStartCondition(float val)
	{
		misfireStartCondition = val;
	};

	void SetMisfireEndCondition(float val)
	{
		misfireEndCondition = val;
	};

protected:
	struct SPDM
	{
		float m_fPDM_disp_base;
		float m_fPDM_disp_vel_factor;
		float m_fPDM_disp_accel_factor;
		float m_fPDM_disp_crouch;
		float m_fPDM_disp_crouch_no_acc;
		float m_fPDM_disp_buckShot;
	};

	SPDM m_pdm;

	float m_crosshair_inertion;
	first_bullet_controller m_first_bullet_controller;
protected:
	//äëÿ îòäà÷è îðóæèÿ
	Fvector m_vRecoilDeltaAngle;

	//äëÿ ñòàëêåðîâ, ÷òîá îíè çíàëè ýôôåêòèâíûå ãðàíèöû èñïîëüçîâàíèÿ
	//îðóæèÿ
	float m_fMinRadius;
	float m_fMaxRadius;
	float m_fZoomRotateModifier;

protected:
	//äëÿ âòîðîãî ñòâîëà
	void StartFlameParticles2();
	void StopFlameParticles2();
	void UpdateFlameParticles2();
protected:
	shared_str m_sFlameParticles2;
	//îáúåêò ïàðòèêëîâ äëÿ ñòðåëüáû èç 2-ãî ñòâîëà
	CParticlesObject* m_pFlameParticles2;

protected:
	int GetAmmoCount(u8 ammo_type) const;

public:
	IC int GetAmmoElapsed() const
	{
		return iAmmoElapsed;
	}

	IC int GetAmmoMagSize() const
	{
		return iMagazineSize;
	}

	int GetSuitableAmmoTotal(bool use_item_to_spawn = false) const;

	void SetAmmoElapsed(int ammo_count);

	virtual void OnMagazineEmpty();
	void SpawnAmmo(u32 boxCurr = 0xffffffff,
	               LPCSTR ammoSect = NULL,
	               u32 ParentID = 0xffffffff);
	bool SwitchAmmoType(u32 flags);

	float m_APk;

	virtual float Get_PDM_Base() const
	{
		return m_pdm.m_fPDM_disp_base;
	};

	virtual float Get_Silencer_PDM_Base() const
	{
		return cur_silencer_koef.pdm_base;
	};

	virtual float Get_Scope_PDM_Base() const
	{
		return cur_scope_koef.pdm_base;
	};

	virtual float Get_Launcher_PDM_Base() const
	{
		return cur_launcher_koef.pdm_base;
	};

	virtual float Get_PDM_BuckShot() const
	{
		return m_pdm.m_fPDM_disp_buckShot;
	};

	virtual float Get_PDM_Vel_F() const
	{
		return m_pdm.m_fPDM_disp_vel_factor;
	};

	virtual float Get_Silencer_PDM_Vel() const
	{
		return cur_silencer_koef.pdm_vel;
	};

	virtual float Get_Scope_PDM_Vel() const
	{
		return cur_scope_koef.pdm_vel;
	};

	virtual float Get_Launcher_PDM_Vel() const
	{
		return cur_launcher_koef.pdm_vel;
	};

	virtual float Get_PDM_Accel_F() const
	{
		return m_pdm.m_fPDM_disp_accel_factor;
	};

	virtual float Get_Silencer_PDM_Accel() const
	{
		return cur_silencer_koef.pdm_accel;
	};

	virtual float Get_Scope_PDM_Accel() const
	{
		return cur_scope_koef.pdm_accel;
	};

	virtual float Get_Launcher_PDM_Accel() const
	{
		return cur_launcher_koef.pdm_accel;
	};

	virtual float Get_PDM_Crouch() const
	{
		return m_pdm.m_fPDM_disp_crouch;
	};

	virtual float Get_PDM_Crouch_NA() const
	{
		return m_pdm.m_fPDM_disp_crouch_no_acc;
	};

	virtual float GetCrosshairInertion() const
	{
		return m_crosshair_inertion;
	};

	virtual float Get_Silencer_CrosshairInertion() const
	{
		return cur_silencer_koef.crosshair_inertion;
	};

	virtual float Get_Scope_CrosshairInertion() const
	{
		return cur_scope_koef.crosshair_inertion;
	};

	virtual float Get_Launcher_CrosshairInertion() const
	{
		return cur_launcher_koef.crosshair_inertion;
	};

	float GetFirstBulletDisp() const
	{
		return m_first_bullet_controller.get_fire_dispertion();
	};

	// Setters
	virtual void Set_PDM_Base(float val) 
	{
		m_pdm.m_fPDM_disp_base = val;
	};

	virtual void Set_Silencer_PDM_Base(float val) 
	{
		cur_silencer_koef.pdm_base = val;
	};

	virtual void Set_Scope_PDM_Base(float val) 
	{
		cur_scope_koef.pdm_base = val;
	};

	virtual void Set_Launcher_PDM_Base(float val) 
	{
		cur_launcher_koef.pdm_base = val;
	};

	virtual void Set_PDM_BuckShot(float val) 
	{
		m_pdm.m_fPDM_disp_buckShot = val;
	};

	virtual void Set_PDM_Vel_F(float val) 
	{
		m_pdm.m_fPDM_disp_vel_factor = val;
	};

	virtual void Set_Silencer_PDM_Vel(float val) 
	{
		cur_silencer_koef.pdm_vel = val;
	};

	virtual void Set_Scope_PDM_Vel(float val) 
	{
		cur_scope_koef.pdm_vel = val;
	};

	virtual void Set_Launcher_PDM_Vel(float val) 
	{
		cur_launcher_koef.pdm_vel = val;
	};

	virtual void Set_PDM_Accel_F(float val) 
	{
		m_pdm.m_fPDM_disp_accel_factor = val;
	};

	virtual void Set_Silencer_PDM_Accel(float val) 
	{
		cur_silencer_koef.pdm_accel = val;
	};

	virtual void Set_Scope_PDM_Accel(float val) 
	{
		cur_scope_koef.pdm_accel = val;
	};

	virtual void Set_Launcher_PDM_Accel(float val) 
	{
		cur_launcher_koef.pdm_accel = val;
	};

	virtual void Set_PDM_Crouch(float val) 
	{
		m_pdm.m_fPDM_disp_crouch = val;
	};

	virtual void Set_PDM_Crouch_NA(float val) 
	{
		m_pdm.m_fPDM_disp_crouch_no_acc = val;
	};

	virtual void SetCrosshairInertion(float val) 
	{
		m_crosshair_inertion = val;
	};

	virtual void Set_Silencer_CrosshairInertion(float val) 
	{
		cur_silencer_koef.crosshair_inertion = val;
	};

	virtual void Set_Scope_CrosshairInertion(float val) 
	{
		cur_scope_koef.crosshair_inertion = val;
	};

	virtual void Set_Launcher_CrosshairInertion(float val)
	{
		cur_launcher_koef.crosshair_inertion = val;
	};

	void SetFirstBulletDisp(float val)
	{
		m_first_bullet_controller.set_fire_dispertion(val);
	};

	// momopate
	float GetZoomRotateTime() { return m_zoom_params.m_fZoomRotateTime; }
	virtual void SetZoomRotateTime(float val) { m_zoom_params.m_fZoomRotateTime = val; }

    // verdatim
    virtual void ForceSetZoomType(float val) { m_zoomtype = val; }

protected:
	int iAmmoElapsed; // ammo in magazine, currently
	int iMagazineSize; // size (in bullets) of magazine

	//äëÿ ïîäñ÷åòà â GetSuitableAmmoTotal
	mutable int m_iAmmoCurrentTotal;
	mutable u32 m_BriefInfo_CalcFrame; //êàäð íà êîòîðîì ïðîñ÷èòàëè êîë-âî ïàòðîíîâ
	bool m_bAmmoWasSpawned;

	virtual bool IsNecessaryItem(const shared_str& item_sect);

public:
	xr_vector<shared_str> m_ammoTypes;

	DEFINE_VECTOR(shared_str, SCOPES_VECTOR, SCOPES_VECTOR_IT);
	SCOPES_VECTOR m_scopes;
	u8 m_cur_scope;

	bool m_altAimPos;
	u8 m_zoomtype;

	CWeaponAmmo* m_pCurrentAmmo;
	u8 m_ammoType;
	bool m_bHasTracers;
	bool m_bSilencedTracers;
	u8 m_u8TracerColorID;
	u8 m_set_next_ammoType_on_reload;
	// Multitype ammo support
	xr_vector<CCartridge> m_magazine;
	CCartridge m_DefaultCartridge;
	CCartridge m_lastCartridge;
	float m_fCurrentCartirdgeDisp;

	virtual bool GetSilencedTracers() { return m_bSilencedTracers; }

	bool unlimited_ammo();
	IC bool can_be_strapped() const
	{
		return m_can_be_strapped;
	};

	const decltype(m_magazine)& GetMagazine() { return m_magazine; };
	float GetMagazineWeight(const decltype(m_magazine)& mag) const;

	virtual float GetHitPower() { return fvHitPower[g_SingleGameDifficulty]; };
	virtual float GetHitPowerCritical() { return fvHitPowerCritical[g_SingleGameDifficulty]; };
	virtual float GetHitImpulse() { return fHitImpulse; };
	virtual float GetFireDistance() { return fireDistance; };

	// Setters
	virtual void SetHitPower(float val) {
		for (int i = ESingleGameDifficulty::egdNovice; i < ESingleGameDifficulty::egdCount; i++) {
			fvHitPower[i] = val;
		}
	};
	virtual void SetHitPowerCritical(float val) {
		for (int i = ESingleGameDifficulty::egdNovice; i < ESingleGameDifficulty::egdCount; i++) {
			fvHitPowerCritical[i] = val;
		}
	};
	virtual void SetHitImpulse(float val) { fHitImpulse = val; };
	virtual void SetFireDistance(float val) { fireDistance = val; };
	
	IC u8 GetZoomType() const
	{
		return m_zoomtype;
	}
	
protected:
	u32 m_ef_main_weapon_type;
	u32 m_ef_weapon_type;

public:
	virtual u32 ef_main_weapon_type() const;
	virtual u32 ef_weapon_type() const;

	//Alundaio
	int GetAmmoCount_forType(shared_str const& ammo_type) const;
	virtual void set_ef_main_weapon_type(u32 type) { m_ef_main_weapon_type = type; };
	virtual void set_ef_weapon_type(u32 type) { m_ef_weapon_type = type; };
	virtual void SetAmmoType(u8 type) { m_ammoType = type; };
	u8 GetAmmoType() { return m_ammoType; };
	//-Alundaio

protected:
	// This is because when scope is attached we can't ask scope for these params
	// therefore we should hold them by ourself :-((
	float m_addon_holder_range_modifier;
	float m_addon_holder_fov_modifier;

public:
	virtual void modify_holder_params(float& range, float& fov) const;

	virtual bool use_crosshair() const
	{
		return true;
	}

	bool show_crosshair();
	bool show_indicators();
	virtual BOOL ParentMayHaveAimBullet();

private:
	virtual bool install_upgrade_ammo_class(LPCSTR section, bool test);
	bool install_upgrade_disp(LPCSTR section, bool test);
	bool install_upgrade_hit(LPCSTR section, bool test);
	bool install_upgrade_hud(LPCSTR section, bool test);
	bool install_upgrade_addon(LPCSTR section, bool test);
protected:
	virtual bool install_upgrade_impl(LPCSTR section, bool test);

private:
	float m_hit_probability[egdCount];

public:
	const float& hit_probability() const;

private:
	Fvector m_overriden_activation_speed;
	bool m_activation_speed_is_overriden;
	virtual bool ActivationSpeedOverriden(Fvector& dest, bool clear_override);

	bool m_bRememberActorNVisnStatus;
public:
	float m_fLR_ShootingFactor; // Фактор горизонтального сдвига худа при стрельбе [-1; +1]
	float m_fUD_ShootingFactor; // Фактор вертикального сдвига худа при стрельбе [-1; +1]
	float m_fBACKW_ShootingFactor; // Фактор сдвига худа в сторону лица при стрельбе [0; +1]
public:
	virtual void SetActivationSpeedOverride(Fvector const& speed);
	void AddHUDShootingEffect();

	bool GetRememberActorNVisnStatus()
	{
		return m_bRememberActorNVisnStatus;
	};
	virtual void EnableActorNVisnAfterZoom();
	virtual float GetInertionAimFactor() { return 1.f - m_zoom_params.m_fZoomRotationFactor; };
	//--> [От 1.0 - Инерция от бедра, до 0.0 - Инерция при зумме] Какую инерцию использовать

	virtual void DumpActiveParams(shared_str const& section_name, CInifile& dst_ini) const;

	virtual shared_str const GetAnticheatSectionName() const
	{
		return cNameSect();
	};
    
    float SDS_Radius(bool alt = false);
};
