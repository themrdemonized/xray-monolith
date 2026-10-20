#pragma once

//#if 0

#include "entity.h"
//#include "../xrphysics/PHDynamicData.h"
#include "../xrphysics/PhysicsShell.h"
#include "../xrphysics/phupdateobject.h"
#include "script_entity.h"
#include "CarLights.h"
//#include "phobject.h"
#include "holder_custom.h"
#include "PHSkeleton.h"
#include "DamagableItem.h"
#include "phcollisiondamagereceiver.h"
#include "CarDamageParticles.h"
#include "xrserver_objects_alife.h"
#include "CarDamageParticles.h"
#include "hit_immunity.h"
#include "Explosive.h"
#include "PHDestroyable.h"
#include "DelayedActionFuse.h"
// refs
class ENGINE_API CBoneInstance;
class CActor;
class CInventory;
class CSE_PHSkeleton;
class CCarWeapon;
struct dxGeomUserData;
struct dSurfaceParameters;
// defs

#ifdef DEBUG
	#include "../xrEngine/StatGraph.h"
	#include "PHDebug.h"
#endif

#ifdef CAR_NEW
#include "CarDrone.h"
#endif

class CScriptEntityAction;
class car_memory;
class CEntityAlive;

// Vehicle Handling Overhaul parameters: name, default. Units match the LTX keys (rpm, degrees, m/s, Hz).
// See docs/vehicles/03-vehicle-handling-overhaul.md. A value of 0 for the engine/gear overrides keeps the model value.
#define VHO_PARAM_LIST(X) \
	X(engine_power, 0.f) X(max_rpm, 0.f) X(idle_rpm, 0.f) X(max_power_rpm, 0.f) X(max_torque_rpm, 0.f) \
	X(power_scale, 1.f) X(engine_inertia, 0.25f) X(rev_up_rate, 5000.f) X(rev_down_rate, 3000.f) X(launch_rpm, 2200.f) X(engine_brake, 0.15f) \
	X(overrev_rpm, 0.f) X(overrev_damage, 0.02f) X(shift_damage, 0.04f) \
	X(low_range, 0.f) X(diff_lock, 0.f) X(diff_center, 0.f) X(diff_axle_bias, 0.f) X(diff_lock_axles, 1.f) X(lug_stall, 1.f) X(launch_gears, 2.f) X(clutch_time, 0.8f) X(stall_rpm, 0.f) X(all_wheel_drive, 0.f) X(rear_drive_only, 0.f) X(brake_all_wheels, 0.f) \
	X(automatic, 1.f) X(final_drive, 0.f) X(reverse_ratio, 3.5f) X(shift_up_rpm, 0.f) X(shift_down_rpm, 0.f) \
	X(shift_time, 0.3f) X(shift_cooldown, 1.5f) X(driveline_efficiency, 0.9f) X(engine_shift_keys, 1.f) \
	X(throttle_rise, 2.5f) X(throttle_fall, 5.f) X(brake_rise, 3.f) X(brake_scale, 1.f) X(handbrake_scale, 1.f) \
	X(handbrake_rear_only, 1.f) X(handbrake_grip, 0.35f) X(handbrake_recover, 3.f) X(handbrake_declutch, 1.f) X(handbrake_double_tap, 0.f) \
	X(reverse_switch_speed, 1.f) \
	X(drag, 0.6f) X(rolling_resistance, 0.015f) X(tyre_friction, 1.f) X(offroad, 1.f) X(grip_scale, 1.f) X(cornering_stiffness, 50000.f) \
	X(tyre_spring, 0.02f) X(tyre_damping, 0.01f) \
	X(steer_rate, 2.2f) X(steer_return_rate, 3.f) X(steer_low_speed, 4.f) X(steer_high_speed, 25.f) \
	X(steer_high_factor, 0.35f) X(steer_max_angle, 0.f) X(steer_servo_speed, 3.f) X(ackermann, 1.f) \
	X(handbrake_steer_rate, 2.f) X(handbrake_steer_lock, 0.5f) X(rear_steer, 0.7f) X(rear_steer_speed, 8.f) \
	X(susp_frequency, 1.6f) X(susp_damping, 0.55f) X(susp_preload, 0.5f) \
	X(bump_start, 0.07f) X(bump_stiffness, 5.f) X(bump_damping, 1.5f) X(anti_roll, 0.5f) \
	X(damper_bump, 1.f) X(damper_rebound, 1.f) X(damper_knee, 0.f) X(damper_soft, 1.f) X(damper_firm, 1.f) \
	X(susp_level, 0.f) X(ride_height, 0.f) X(bump_ramp, 0.04f) X(cog_height, 0.f) X(spawn_lift, 0.f) \
	X(slide_grip, 1.f) X(slide_speed, 2.5f) X(weather_grip, 1.f) X(handbrake_slip, 4.6f) X(flip_speed, 0.7f) \
	X(throttle_calm, 1.f) X(clutch_pedal, 0.f) \
	X(temp_thermostat, 88.f) X(temp_derate, 108.f) X(temp_damage, 115.f) X(temp_seize, 128.f) X(temp_restart, 100.f) \
	X(drivetrain_inertia, 0.f) X(inertia_cap, 40.f) X(slip_rpm, 0.f) X(tc_stall_rpm, 0.f) X(tc_multiply, 1.f) \
	X(auto_up_reserve, 0.25f) X(hill_shift, 0.f) X(handbrake_rise, 0.f) X(yaw_inertia, 0.f) X(park_release, 0.f) X(calm_hill, 0.f) \
	X(contact_max_vel, 0.f) X(body_friction, 0.f) X(wheel_climb, 0.f) X(run_over_mass, 0.f) X(see_through, 0.f) \
	X(dodge, 0.f) X(dodge_see, 35.f) X(dodge_hear, 18.f) X(dodge_time, 1.f) \
	X(disable_health, 0.05f) X(min_health, 0.01f) X(damage_scale, 0.8f) X(collision_damage_scale, 0.7f) \
	X(explosion_damage_scale, 1.f) X(explosive_only_death, 1.f) X(low_health_start, 0.3f) X(low_health_power, 0.6f) \
	X(wade_depth, 0.5f) X(flood_time, 20.f) X(crank_time, 0.8f) X(start_fail, 0.5f) X(fuel_scale, 1.f) X(fuel_per_kwh, 0.42f) X(fuel_idle, 0.f) \
	X(overheat, 0.f) X(heat_scale, 1.f) X(cool_scale, 1.f) \
	X(stall_health, 0.4f) X(stall_chance, 0.05f) X(crash_stall_speed, 8.f) \
	X(snd_pitch_idle, 0.6f) X(snd_pitch_max, 1.6f) X(snd_volume_idle, 0.75f) X(snd_volume_load, 1.f) \
	X(run_over_damage, 0.006f) X(run_over_impulse, 40.f) X(run_over_speed, 3.f) \
	X(ram_mass, 150.f) X(ram_damage, 0.005f) X(ram_player_damage, 0.008f) \
	X(shock_recharge, 15.f) X(anomaly_pull, 20.f) X(anomaly_lift, 1.6f) X(anomaly_lift_height, 1.5f) \
	X(anomaly_kick, 8.f) X(anomaly_fire_damage, 0.8f) X(anomaly_push_damage, 0.15f) \
	X(anomaly_push, 0.2f) X(creature_push, 0.f) X(heavy_push, 1.f) X(real_mass, 0.f) X(easy_enter, 0.f) X(camera_raise, 0.f) \
	X(brake_g, 0.f) X(brake_front, 0.f) X(handbrake_lock, 0.f) X(slide_peak, 0.f) X(slide_full, 0.f) \
	X(cornering_load, 0.f) X(steer_slip, 0.f) X(brake_calm, 1.f) \
	X(brake_build, 0.f) X(brake_bite, 0.3f) X(stall_time, 0.4f) X(idle_hold, 0.f) X(clutch_assist, 0.f) X(temp_warmup, 1.f) \
	X(debug_trace, 0.f)

class CCar :
	public CEntity,
	public CScriptEntity,
	public CPHUpdateObject,
	public CHolderCustom,
	public CPHSkeleton,
	public CDamagableItem,
	public CPHDestroyable,
	public CPHCollisionDamageReceiver,
	public CHitImmunity,
	public CExplosive,
	public CDelayedActionFuse
{
private:
	collide::rq_results RQR;

#ifdef DEBUG
	CFunctionGraph 					m_dbg_power_rpm			;
	CFunctionGraph 					m_dbg_torque_rpm		;
	CStatGraph	   					*m_dbg_dynamic_plot		;
	bool							b_plots					;
	float _stdcall			TorqueRpmFun		(float rpm)		{return Parabola(rpm)/rpm;}
	void 					InitDebug			()				;
	void 					DbgSheduleUpdate	()				;
	void 					DbgUbdateCl			()				;
	void 					DbgCreatePlots		()				;
	void 					DBgClearPlots		()				;
#endif
	////////////////////////////////////////////////////////////////////	
	Flags16 async_calls;
	static const u16 cAsCallsnum = 3;

	enum EAsyncCalls
	{
		ascSndTransmission = 1 << 0,
		ascSndStall = 1 << 1,
		ascExhoustStop = 1 << 2,
		ascLast = 1 << cAsCallsnum
	};

	void ASCUpdate();
	void ASCUpdate(EAsyncCalls c);
	void AscCall(EAsyncCalls c);
	////////////////////////////////////////////////////////////////////////////////////////
	virtual bool CanRemoveObject();
	////////////////////////////////////////////////////////////////////////
	static BONE_P_MAP bone_map; //interface for PhysicsShell
	static void ActorObstacleCallback(bool& do_colide, bool bo1, dContact& c, SGameMtl* material_1,
	                                  SGameMtl* material_2);
	virtual void PhDataUpdate(float step);
	virtual void PhTune(float step);
	/////////////////////////////////////////////////////////////////////////
	virtual void ApplyDamage(u16 level);
	virtual float Health() { return GetfHealth(); }
	virtual void ChangeCondition(float fDeltaCondition);

	virtual void StartTimerEffects()
	{
	};
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////
	virtual CPhysicsShellHolder* PPhysicsShellHolder() { return static_cast<CPhysicsShellHolder*>(this); }
	virtual ICollisionDamageReceiver* PHCollisionDamageReceiver() { return this; }

	////////////////////////////////////////////////////////////////////////
	CCarDamageParticles m_damage_particles;
	///////////////////////////////////////////////////////////////////////
protected:
	enum ECarCamType
	{
		ectFirst = 0,
		ectChase,
		ectFree
	};

public:


	bool rsp, lsp, fwp, bkp, brp;
	Fmatrix m_root_transform;
	Fvector m_exit_position;

	enum eStateDrive
	{
		drive,
		neutral
	};

	eStateDrive e_state_drive;

	enum eStateSteer
	{
		right,
		idle,
		left
	};

	eStateSteer e_state_steer;

	bool b_wheels_limited;
	bool b_engine_on;
	bool b_clutch;
	bool b_starting;
	bool b_stalling;
	bool b_breaks;
	bool b_transmission_switching;

	u32 m_dwStartTime;
	float m_fuel;
	float m_fuel_tank;
	float m_fuel_consumption;
	u16 m_driver_anim_type;

	float m_break_start;
	float m_break_time;
	float m_breaks_to_back_rate;
	float m_power_neutral_factor; //multiplier for power when accelerator is not pressed (0-1,0.25)
	bool b_exploded;

	struct SWheel :
		public CDamagableHealthItem
	{
		typedef CDamagableHealthItem inherited;
		u16 bone_id;
		bool inited;
		float radius;
		CPhysicsJoint* joint;
		CCar* car;

		struct SWheelCollisionParams
		{
			float spring_factor;
			float damping_factor;
			float mu_factor;
			SWheel* wheel;
			SWheelCollisionParams();
		} collision_params;

		// Cached at Init: the shared static bone_map is cleared whenever another car spawns.
		CPhysicsElement* element;
		Fvector pos; // bind position in object space
		float stock_spring, stock_damping; // hinge2 suspension factors from the model
		float vho_spring_mul, vho_damping_mul; // suspension multipliers currently applied by the handling overhaul
		float vho_drive, vho_resist; // per-step torque accumulators of the handling overhaul
		Fvector rest_local; // bind position in the chassis element frame (suspension travel reference)
		float vho_comp; // suspension compression last step, m (positive = wheel pushed into the body)
		float vho_steer_dir; // +1 or -1: steer joint axis up (stock convention) or down (mirrored in the model)
		SWheel* vho_arb; // the other wheel on this axle, for the anti-roll bar
		SWheel* vho_diff_mate; // the other driven wheel on this axle (its differential partner), or NULL
		int vho_diff_group; // 0 = front, 1 = rear side of the centre / inter-axle differential
		float vho_diff_k; // share of its drive torque the differentials pass to this wheel, smoothed
		float vho_rim; // forward rim speed this step, m/s
		float vho_comp_now; // suspension compression this step
		Fvector vho_axis; // suspension axis this step (points up out of the wheel)
		Fvector vho_axis_stock; // the model's suspension axis in the chassis frame, while replaced
		bool vho_axis_fixed; // suspension axis replaced with the car's up axis (it lies flat in the model)
		float vho_bump_s, vho_bump_d; // bump-stop multipliers currently applied to the joint spring and damping
		float vho_preload; // N pushing the wheel down out of the body (static load the spring does not carry)
		float vho_inertia; // drivetrain inertia currently added about the axle (kg m2, physics scale)
		float vho_brake_full, vho_hand_full; // brake_g: N m at full pedal and full lever (physics scale)
		SGameMtl* contact_mtl;
		u32 contact_step;
		// handling trace (debug_trace): last tyre contact and joint motor command, read by HandlingTraceRecord
		float vho_dbg_mu, vho_dbg_slip2, vho_dbg_patch; // friction, sideways slip, contact-patch slip speed m/s
		u32 vho_dbg_contacts; // tyre contacts generated in the last collision pass
		bool vho_dbg_neg; // a contact pushed out of a triangle's back side (the solver gives it unlimited friction)
		float vho_dbg_mvel, vho_dbg_mfmax; // drive joint motor: target speed rad/s, force cap N m
		float vho_dbg_steer; // steer joint target angle, rad

		IC static void applywheelCollisionParams(const dxGeomUserData* ud, bool& do_colide, dContact& c,
		                                         SGameMtl* material_1, SGameMtl* material_2);
		static void WheellCollisionCallback(bool& do_colide, bool bo1, dContact& c, SGameMtl* material_1,
		                                    SGameMtl* material_2);

		void Init(); //asumptions: bone_map is 1. ini parsed 2. filled in 3. bone_id is set 
		void Load(LPCSTR section);
		void RestoreNetState(const CSE_ALifeCar::SWheelState& a_state);
		void SaveNetState(NET_Packet& P);
		void ApplyDriveAxisVel(float vel);
		void ApplyDriveAxisTorque(float torque);
		void ApplyDriveAxisVelTorque(float vel, float torque);
		void ApplySteerAxisVel(float vel);
		void ApplySteerAxisTorque(float torque);
		void ApplySteerAxisVelTorque(float vel, float torque);
		void SetSteerLoLimit(float lo);
		void SetSteerHiLimit(float hi);
		void SetSteerLimits(float hi, float lo);

		virtual void ApplyDamage(u16 level);

		SWheel(CCar* acar)
		{
			radius = 0.0f;
			bone_id = BI_NONE;
			car = acar;
			joint = NULL;
			inited = false;
			element = NULL;
			pos.set(0.f, 0.f, 0.f);
			stock_spring = stock_damping = 1.f;
			vho_spring_mul = vho_damping_mul = 1.f;
			vho_drive = vho_resist = 0.f;
			rest_local.set(0.f, 0.f, 0.f);
			vho_comp = 0.f;
			vho_steer_dir = 1.f;
			vho_axis_stock.set(0.f, 1.f, 0.f);
			vho_axis_fixed = false;
			vho_arb = NULL;
			vho_diff_mate = NULL;
			vho_diff_group = 0;
			vho_diff_k = 1.f;
			vho_rim = 0.f;
			vho_comp_now = 0.f;
			vho_axis.set(0.f, 1.f, 0.f);
			vho_bump_s = vho_bump_d = 1.f;
			vho_preload = 0.f;
			vho_inertia = 0.f;
			vho_brake_full = vho_hand_full = 0.f;
			contact_mtl = NULL;
			contact_step = 0;
			vho_dbg_mu = vho_dbg_slip2 = vho_dbg_patch = 0.f;
			vho_dbg_contacts = 0;
			vho_dbg_neg = false;
			vho_dbg_mvel = vho_dbg_mfmax = vho_dbg_steer = 0.f;
		}
	};

	struct SWheelDrive
	{
		SWheel* pwheel;
		float pos_fvd;
		float gear_factor;
		void Init();
		void Drive();
		void Neutral();
		void UpdatePower();
		float ASpeed();

		void Load(LPCSTR /*section*/)
		{
		} ;
	};

	struct SWheelSteer
	{
		SWheel* pwheel;
		float pos_right;
		float lo_limit;
		float hi_limit;
		float steering_velocity;
		float steering_torque;
		bool limited; //zero limited for idle steering drive
		float GetSteerAngle();

		void Init();
		void SteerRight();
		void SteerLeft();
		void SteerIdle();
		void SteerTo(float k);
		void Limit();

		void Load(LPCSTR /*section*/)
		{
		} ;
	};

	struct SWheelBreak
	{
		SWheel* pwheel;
		float break_torque;
		float hand_break_torque;
		void Init();
		void Break(float k);
		void HandBreak();
		void Neutral();
		void Load(LPCSTR section);
	};

	struct SExhaust
	{
		u16 bone_id;
		Fmatrix transform;
		//Fvector				velocity;
		CParticlesObject* p_pgobject;
		CPhysicsElement* pelement;
		CCar* pcar;
		void Init();
		void Play();
		void Stop();
		void Update();
		void Clear();

		SExhaust(CCar* acar)
		{
			bone_id = BI_NONE;
			pcar = acar;
			p_pgobject = NULL;
			pelement = NULL;
		}

		~SExhaust();
	};

	struct SDoor;

	struct SDoor :
		public CDamagableHealthItem
	{
		typedef CDamagableHealthItem inherited;
		u16 bone_id;
		CCar* pcar;
		bool update;
		CPhysicsJoint* joint;
		float torque;
		float a_vel;
		float pos_open;
		float opened_angle;
		float closed_angle;
		u32 open_time;

		struct SDoorway
		{
			Fvector2 door_plane_ext;
			_vector2<int> door_plane_axes;
			SDoor* door;
			SDoorway();
			void SPass();
			void Init(SDoor* adoor);
			void Trace(const Fvector& point, const Fvector& dir);
		};

		Fvector2 door_plane_ext;
		_vector2<int> door_plane_axes;
		Fvector door_dir_in_door;
		Fmatrix closed_door_form_in_object;
		void Use();
		void Switch();
		void Init();
		void Open();
		void Close();
		void Break();
		virtual void ApplyDamage(u16 level);
		void Update();
		float GetAngle();
		bool CanEnter(const Fvector& pos, const Fvector& dir, const Fvector& foot_pos);
		bool IsInArea(const Fvector& pos, const Fvector& dir);
		bool IsFront(const Fvector& pos, const Fvector& dir);
		bool CanExit(const Fvector& pos, const Fvector& dir);
		bool TestPass(const Fvector& pos, const Fvector& dir);
		//bool TestPass1(const Fvector& pos,const Fvector& dir);
		void GetExitPosition(Fvector& pos);
		void ApplyOpenTorque();
		void ApplyTorque(float atorque, float aa_vel);
		void ApplyCloseTorque();
		void NeutralTorque(float atorque);
		void ClosingToClosed();
		void ClosedToOpening();
		void PlaceInUpdate();
		void RemoveFromUpdate();
		void SaveNetState(NET_Packet& P);
		void RestoreNetState(const CSE_ALifeCar::SDoorState& a_state);
		void SetDefaultNetState();

		enum eState
		{
			opening,
			closing,
			opened,
			closed,
			broken
		};

		eState state;

		SDoor(CCar* acar)
		{
			update = false;
			pos_open = 0.0f;
			opened_angle = 0.0f;
			closed_angle = 0.0f;
			open_time = 0;
			bone_id = BI_NONE;
			pcar = acar;
			joint = NULL;
			state = closed;
			torque = 500.f;
			a_vel = M_PI;
		}
	};

	struct SCarSound
	{
		ref_sound snd_engine;
		ref_sound snd_engine_start;
		ref_sound snd_engine_stop;
		ref_sound snd_transmission;

		enum ESoundState
		{
			sndOff,
			sndStalling,
			sndStoping,
			sndStarting,
			sndDrive
		} eCarSound;

		void Update();
		void UpdateStarting();
		void UpdateStoping();
		void UpdateStalling();
		void UpdateDrive();
		void SwitchState(ESoundState new_state);
		void SetSoundPosition(ref_sound& snd);
		void SwitchOff();
		void SwitchOn();
		void Init();
		void Destroy();
		void Start();
		void Stop();
		void Stall();
		void Drive();
		void TransmissionSwitch();

		SCarSound(CCar* car);
		~SCarSound();
		Fvector relative_pos;
		float volume;
		u32 engine_start_delay; //snd_engine starts after engine_start_delay ms by snd_engine_start
		u32 time_state_start;
		CCar* pcar;
	} * m_car_sound;

private:
	typedef CEntity inherited;
private:
	CCarWeapon* m_car_weapon;
	float m_steer_angle;
	bool m_repairing;
	u16 m_bone_steer;
	CCameraBase* camera[3];
	CCameraBase* active_camera;

	Fvector m_camera_position;

	////////////////////////////////////////////////////
	friend struct SWheel;
	friend struct SDoor;

	xr_map<u16, SWheel> m_wheels_map;
	xr_vector<SWheelDrive> m_driving_wheels;
	xr_vector<SWheelSteer> m_steering_wheels;
	xr_vector<SWheelBreak> m_breaking_wheels;
	xr_vector<SExhaust> m_exhausts;
	shared_str m_exhaust_particles;
	xr_map<u16, SDoor> m_doors;
	xr_vector<SDoor*> m_doors_update;
	xr_vector<Fvector> m_gear_ratious;
	xr_vector<Fmatrix> m_sits_transforms; // m_sits_transforms[0] - driver_place
	float m_current_gear_ratio;

	/////////////////////////////////////////////////////////////
	bool b_auto_switch_transmission;

	/////////////////////////////////////////////////////////////
	float m_doors_torque_factor;
	/////////////////////////////////////////////////////////////

	float m_max_power; //best rpm
	float m_power_increment_factor;
	float m_power_decrement_factor;
	float m_rpm_increment_factor;
	float m_rpm_decrement_factor;
	/////////////////////porabola
	float m_a, m_b, m_c;

	float m_current_engine_power;
	float m_current_rpm;

	float m_axle_friction;

	float m_fSaveMaxRPM;
	float m_max_rpm;
	float m_min_rpm;
	float m_power_rpm; //max power
	float m_torque_rpm; //max torque


	float m_steering_speed;
	float m_ref_radius;
	bool m_speed_governed;
	float m_target_speed;
	float m_throttle;
	size_t m_current_transmission_num;
	///////////////////////////////////////////////////
	CCarLights m_lights;
	////////////////////////////////////////////////////
	/////////////////////////////////////////////////
	void InitParabola();
	float _stdcall Parabola(float rpm);
	//float GetSteerAngle();
	void LimitWheels();
	void Drive();
	void Starter();
	void StartEngine();
	void StopEngine();
	void Stall();
	void Clutch();
	void Unclutch();
	void SwitchEngine();
	void NeutralDrive();
	void UpdatePower();
	void ReleasePedals();
	void ResetKeys();

	////////////////////////////////////////////////////////////////////////////
	float RefWheelMaxSpeed();
	float EngineCurTorque();
	float RefWheelCurTorque();
	float EnginePower();
	float EngineDriveSpeed();
	float DriveWheelsMeanAngleRate();
	IC float EngineRpmFromWheels() { return _abs(DriveWheelsMeanAngleRate() * m_current_gear_ratio); }
	/////////////////////////////////////////////////////////////////////////	
	void SteerRight();
	void SteerLeft();
	void SteerIdle();
	void Transmission(size_t num);
	void CircleSwitchTransmission();
	void TransmissionUp();
	void TransmissionDown();
	IC size_t CurrentTransmission() { return m_current_transmission_num; }
	// for scripts: luabind has no converter for size_t (64-bit), so returning it from a binding crashed the game
	u32 CurrentTransmissionScript() { return u32(m_current_transmission_num); }
	void PressRight();
	void PressLeft();
	void PressForward();
	void PressBack();
	void PressBreaks();

	void ReleaseRight();
	void ReleaseLeft();
	void ReleaseForward();
	void ReleaseBack();
	void ReleaseBreaks();
	void Revert();
	float EffectiveGravity();
	float AntiGravityAccel();
	float GravityFactorImpulse();
	void StartBreaking();
	void StopBreaking();
	void UpdateBack();

	void HandBreak();
	void ReleaseHandBreak();
	void DriveForward();
	void DriveBack();
	void ParseDefinitions();
	void CreateSkeleton(CSE_Abstract* po); //creates m_pPhysicsShell
	void Init();

	void PlayExhausts();
	void StopExhausts();
	void UpdateExhausts();
	void ClearExhausts();
	void UpdateFuel(float time_delta);
	float AddFuel(float ammount); //ammount - fuel to load, ret - fuel loaded
	void CarExplode();
	/************************************************** added by Ray Twitty (aka Shadows) START **************************************************/
	//Alundaio
#ifdef ENABLE_CAR
	float GetfFuel();
	void SetfFuel(float fuel);
	float GetfFuelTank();
	void SetfFuelTank(float fuel_tank);
	float GetfFuelConsumption();
	void SetfFuelConsumption(float fuel_consumption);
	void ChangefFuel(float fuel);
	void ChangefHealth(float health);

	void PlayDamageParticles()
	{
		m_damage_particles.Play1(this);
		m_damage_particles.Play2(this);
	}

	void StopDamageParticles()
	{
		m_damage_particles.Stop1(this);
		m_damage_particles.Stop2(this);
	}

	bool isActiveEngine();
	float GetRPM() { return m_current_rpm; }
	void SetRPM(float val) { m_current_rpm = val; }
#endif
	//-Alundaio
	/*************************************************** added by Ray Twitty (aka Shadows) END ***************************************************/
	////////////////////////////////////////////		////////

	void OnCameraChange(int type);


	bool HUDview() { return IsFocused(); }

	static void _BCL cb_Steer(CBoneInstance* B);
	virtual void Hit(SHit* pHDS);
	virtual void Die(CObject* who);
	virtual void PHHit(SHit& H);
	bool WheelHit(float P, s16 element, ALife::EHitType hit_type);
	bool DoorHit(float P, s16 element, ALife::EHitType hit_type);
public:
	virtual bool allowWeapon() const { return true; };
	virtual bool HUDView() const;
	virtual Fvector ExitPosition();
	virtual Fvector ExitVelocity();
	void GetVelocity(Fvector& vel) { m_pPhysicsShell->get_LinearVel(vel); }
	void cam_Update(float dt, float fov);
	void detach_Actor();
	bool attach_Actor(CGameObject* actor);
	bool is_Door(u16 id, xr_map<u16, SDoor>::iterator& i);
	bool is_Door(u16 id);
	bool DoorOpen(u16 id);
	bool DoorClose(u16 id);
	bool DoorUse(u16 id);
	bool DoorSwitch(u16 id);
	bool Enter(const Fvector& pos, const Fvector& dir, const Fvector& foot_pos);
	bool Exit(const Fvector& pos, const Fvector& dir);
	bool Use(const Fvector& pos, const Fvector& dir, const Fvector& foot_pos);
	u16 DriverAnimationType();
	// Core events
	virtual DLL_Pure* _construct();
	virtual void Load(LPCSTR section);
	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void net_Destroy();
	virtual void UpdateCL();
	virtual void UpdateEx(float fov); //called by owner

	virtual void shedule_Update(u32 dt);
	virtual void renderable_Render();
	virtual bool bfAssignMovement(CScriptEntityAction* tpEntityAction);
	virtual bool bfAssignObject(CScriptEntityAction* tpEntityAction);

	// Network
	virtual void net_Export(NET_Packet& P); // export to server
	virtual void net_Import(NET_Packet& P); // import from server
	virtual BOOL net_Relevant() { return getLocal(); }; // relevant for export to server
	virtual BOOL UsedAI_Locations();
	virtual void net_Relcase(CObject* O);
	// Input
	virtual void OnMouseMove(int x, int y);
	virtual void OnKeyboardPress(int dik);
	virtual void OnKeyboardRelease(int dik);
	virtual void OnKeyboardHold(int dik);
	virtual void vfProcessInputKey(int iCommand, bool bPressed);
	virtual void OnEvent(NET_Packet& P, u16 type);
	virtual void OnAfterExplosion();
	virtual void OnBeforeExplosion();
	virtual void GetRayExplosionSourcePos(Fvector& pos);

	virtual void ActivateExplosionBox(const Fvector& size, Fvector& in_out_pos)
	{
	};
	virtual void ResetScriptData(void* P = 0);

	virtual void Action(u16 id, u32 flags);
	virtual void SetParam(int id, Fvector2 val);
	virtual void SetParam(int id, Fvector val);
	bool HasWeapon();
	bool WpnCanHit();
	float FireDirDiff();
	bool isObjectVisible(CScriptGameObject* O);
	Fvector CurrentVel();
	float GetSpeed();
	void SetTargetSpeed(float mps);
	void ClearTargetSpeed();
	void SetThrottle(float k);
	void SetSteer(float k);
	// wheel-ground friction multiplier (SWheelCollisionParams::mu_factor)
	float GetWheelFriction();
	void SetWheelFriction(float mu_factor);
	bool IsSpeedGoverned() const { return m_speed_governed; }
	float ThrottleFactor() const { return m_throttle; }
	float DriveRefSpeed();
	virtual float GetfHealth() const { return CEntity::GetfHealth(); };
	virtual float SetfHealth(float value) { return CEntity::SetfHealth(value); };

	// Hits
	virtual void HitSignal(float /**HitAmount/**/, Fvector& /**local_dir/**/, CObject* /**who/**/, s16 /**element/**/)
	{
	};

	virtual void HitImpulse(float /**amount/**/, Fvector& /**vWorldDir/**/, Fvector& /**vLocalDir/**/)
	{
	};

	virtual void g_fireParams(const CHudItem* /**pHudItem/**/, Fvector& /**P/**/, Fvector& /**D/**/)
	{
	};
	virtual u16 Initiator();
	// HUD
	virtual void OnHUDDraw(CCustomHUD* hud);

	CCameraBase* Camera() { return active_camera; }
	void SetExplodeTime(u32 et);
	u32 ExplodeTime();
	// Inventory for the car	
	CInventory* GetInventory() { return inventory; }
	void VisualUpdate(float fov = 90.0f);

protected:
	virtual void SpawnInitPhysics(CSE_Abstract* D);
	virtual void net_Save(NET_Packet& P);
	virtual BOOL net_SaveRelevant();
	void SaveNetState(NET_Packet& P);
	virtual void RestoreNetState(CSE_PHSkeleton* po);
	void SetDefaultNetState(CSE_PHSkeleton* po);

	virtual bool IsHudModeNow() { return false; };

public:
	CCar(void);
	virtual ~CCar(void);
	virtual BOOL AlwaysTheCrow();

public:
	virtual CEntity* cast_entity() { return this; }
private:
	template <class T>
	IC void fill_wheel_vector(LPCSTR S, xr_vector<T>& type_wheels);
	IC void fill_exhaust_vector(LPCSTR S, xr_vector<SExhaust>& exhausts);
	IC void fill_doors_map(LPCSTR S, xr_map<u16, SDoor>& doors);

	//Inventory for the car
	CInventory* inventory;

	virtual void reinit();
	virtual void reload(LPCSTR section);
	virtual CGameObject* cast_game_object() { return this; }
	virtual CExplosive* cast_explosive() { return this; }
	virtual CPhysicsShellHolder* cast_physics_shell_holder() { return this; }
	virtual CParticlesPlayer* cast_particles_player() { return this; }
	virtual CScriptEntity* cast_script_entity() { return this; }
	virtual IDamageSource* cast_IDamageSource() { return this; }
	virtual CHolderCustom* cast_holder_custom() { return this; }

private:
	car_memory* m_memory;

	/*----------------------------------------------------------------------------------------------------
		Vehicle Handling Overhaul (CarHandling.cpp)
	----------------------------------------------------------------------------------------------------*/
public:
	struct SHandlingParams
	{
#define VHO_DECLARE(name, def) float name;
		VHO_PARAM_LIST(VHO_DECLARE)
#undef VHO_DECLARE
		SHandlingParams();
	};

	struct SHandlingSurface
	{
		SGameMtl* material;
		float grip, rolling, slip;
	};

private:
	struct SHandlingState
	{
		float throttle, brake, steer, rpm, speed, clutch;
		float shift_timer, auto_timer, pending_damage, script_steer;
		float brake_hold; // 0..1: how far the held brake has built up (brake_build)
		float stall_timer; // s the engine has been held below stall rpm (stall_time)
		float ground_w, shift_throttle; // filtered inputs of the automatic gearbox
		float hand_slide; // 0..1 rear grip loss from the hand brake, fades out after release
		Fvector prev_vel; // chassis velocity last step, for crash detection
		float crash_dv, crash_cooldown, last_crash_speed;
		u32 crash_count, stall_count, stall_reason; // read by scripts for messages and player damage
		u32 collide_sound_time;
		bool script_steer_on, locked, hold_brake, overrev, last_hit_explosive;
		bool declutched; // hand brake held by the driver: the engine revs freely
		bool hand_latched; // hand brake left on with a double tap (handbrake_double_tap)
		u32 hand_tap; // time of the last hand brake press, for the double tap
		float clutch_engage; // 0..1 auto-clutch engagement when pulling away in a high manual gear
		bool low_range_on, diff_locked, crank_ok;
		float crank_timer; // engine cranking before it catches (or fails to)
		float engine_temp; // coolant temperature, degrees C
		float heat_k; // power left by an overheating engine, 0..1
		bool seized; // stalled from overheating: will not start until it has cooled down
		bool start_blocked; // set by scripts (emission interference): stalls, and the starter never catches
		float wheel_dist; // metres the driven wheels have rolled, either way (the odometer script reads it)
		float flood_timer; // engine flooded by deep water: will not start until it dries out
		float water_depth, water_check; // water above the lowest wheel bottom, m; refresh timer
		float flip_timer; // rolling a flipped car back onto its wheels
		float flip_sign; // which way about its long axis: away from whoever pushes (0 = not chosen yet)
		bool sprint; // sprint key held: full throttle instead of throttle_calm
		bool clutch_held; // clutch pedal mode: the driver's clutch key is down
		bool neutral; // out of gear: left parked, or the clutch key double-tapped
		bool fresh_spawn; // spawned new (not from a save): may be lifted clear of the ground once
		bool apply_pending; // parameters changed: HandlingApply runs once on the next frame
		float cog_h; // centre of gravity above the wheels' ground contact as built, m (0 = not measured)
		Fvector cog_prev_v, cog_acc; // chassis velocity last step and filtered acceleration, for cog_height
		float hand_k; // 0..1 how far the hand brake lever is pulled (handbrake_rise)
		float chassis_inertia_k; // chassis inertia multiplier applied for yaw_inertia (0 or 1 = the model's)
		Fvector inertia_add; // added on top of that about the car's right (pitch), up (yaw) and forward (roll) axes
		u32 dodge_next; // next look for people in the car's path
		// The car's own axes in the chassis frame. The chassis is the root bone, which some models have rotated
		// (Moskvich, ZAZ-968, ZIL-130, ZIL-131, KamAZ semi, LAZ), so its raw axes are not the car's right, up and forward.
		Fvector ax_right, ax_up, ax_fwd;
		float wheelbase, track, axle_mid_z, axle_mid_x, sprung_corner_mass;
		float brake_front_share, static_front_share; // brake_g: front axles' share of the brake force, and of the weight
		float front_sign; // +1 when the front of the car is +z in model space
		float front_u, rear_u; // frontmost steering axle and mean rear-half axle, along the car from axle_mid_z
		bool rear_steering; // steering wheels behind the middle (BTR): counter-steer at low speed only
		Fvector chase_pivot; // model space: chase camera orbit point over the middle of the wheelbase
		bool chase_pivot_ok;
		float mass_k; // physics mass / real_mass: forces are scaled so the car behaves like its real weight
		CPhysicsElement* chassis;
		u32 step_count;
		// this step's intermediate values, for the handling trace
		float dbg_thr_in, dbg_brk_in, dbg_steer_k, dbg_shaft, dbg_t_engine, dbg_t_shaft, dbg_t_wheels, dbg_crash_lost;
		u32 dbg_flags;
		// momentum balance of the whole vehicle: last step's momentum, and the external forces this step that are
		// not contacts (overhaul drag and flip force from PhTune; the model's air drag, applied a step late)
		Fvector dbg_prev_p, dbg_ext_force, dbg_air_force;
		u32 dbg_prev_step;
		float dbg_air_k; // model air_resistance linear coefficient per element (wheels have none)
		// trace event latches, per event type: armed again only once the event has cleared and its cooldown passed
		bool dbg_armed[3];
		u32 dbg_block_until[3];
		u32 dbg_lock_clear; // steps in a row without a wheel locking
	};

	struct SHandlingStock
	{
		float max_power, max_rpm, min_rpm, power_rpm, torque_rpm;
		xr_vector<Fvector> gears;
	};

	struct SHandling
	{
		bool enabled;
		shared_str section;
		SHandlingParams p;
		SHandlingState s;
		SHandlingStock stock;
		xr_vector<float> gears;
		xr_vector<SHandlingSurface> surfaces;
		struct SPusher
		{
			shared_str species;
			float k; // push share for this species; below 0 = heavy_push
		};
		xr_vector<SPusher> heavy_species; // creature species allowed to push the car
		xr_map<u16, int> pusher_cache; // object id -> index in heavy_species, -1 = an ordinary creature
		// wheels the overhaul drives: the model's driving wheels, plus the rest with all_wheel_drive, minus the
		// front axle with rear_drive_only
		xr_vector<SWheelDrive> drive;
		xr_vector<shared_str> ghost_species; // creatures the car drives through (no contact), hitting them instead
		xr_map<u16, bool> ghost_cache; // object id -> is a drive-through creature
		struct SRunOver
		{
			u16 id;
			bool alive; // alive: a hit with power and impulse; a body: an impulse on its ragdoll
			float power, impulse;
			Fvector dir;
			float car_dv; // speed the car loses handing over the momentum (bodies)
			float shove; // impulse through a living one's movement controller
			Fvector car_dir;
			float car_damage; // health the car loses to a big creature (ram_mass)
			float jolt; // m/s the blow takes off the car, which hurts the driver
		};
		xr_vector<SRunOver> run_overs; // hits queued by the contact callback, sent in UpdateCL
		xr_map<u16, u32> run_over_time; // last hit per creature, so one pass is one hit
		xr_vector<CObject*> near_objects; // scratch for HandlingDodge
		xr_map<u16, u32> dodge_time; // last dodge per person

		// Handling trace (debug_trace > 0): the last steps of the physics state, written to
		// logs\vho_trace\*.csv after a stall, a crash, a wheel locking or a script request
		enum { TRACE_STEPS = 500, TRACE_WHEELS = 8 };
		enum { EV_STALL, EV_CRASH, EV_LOCK, EV_COUNT };
		struct STraceWheel
		{
			float w, rim, comp, load_est, mvel, mfmax, diff_k, mu, slip2, patch, steer_tgt, steer_act;
			s16 mtl;
			u8 contact, contacts, neg;
		};
		struct STraceStep
		{
			u32 step, time_ms, flags;
			float h;
			// chassis in the car's own frame (root-bone axes; forward toward the steering wheels)
			float v_long, v_lat, v_vert, a_long, a_lat, a_vert, yaw_rate, roll_rate, pitch_rate, roll, pitch;
			float heading, vel_heading, sideslip;
			float fc_long, fc_lat, fc_norm; // net contact force on the whole vehicle, from its momentum balance
			float thr_in, thr, brk_in, brk, hand_k, hand_slide, steer, steer_k;
			float rpm, shaft, ground, ratio, clutch, engage, t_engine, t_shaft, t_wheels, crash_lost, crash_dv;
			u16 stall_count;
			u8 gear, stall_reason, wheels, valid; // valid: 1 = sideslip, 2 = contact force
			STraceWheel wheel[TRACE_WHEELS];
		};
		// The ring and the latched snapshot swap buffers, so latching a trace in the physics step copies nothing
		xr_vector<STraceStep> trace, dump;
		u32 trace_head = 0, trace_count = 0, dump_head = 0, dump_count = 0, dump_at = 0, auto_dumps = 0;
		bool dump_pending = false;
		shared_str dump_reason, trace_info; // trace_info: the script's description of the configuration
		xr_string dump_events; // the event that latched the trace, and any that came while it was pending
		shared_str latched_reason; // dump_reason and dump_events of the trace in `dump`, taken over when it latched
		xr_string latched_events;
	} m_vho;

	void HandlingInit();
	void HandlingApply();
	void HandlingRestore();
	void HandlingResetState();
	void HandlingApplySuspension(bool stock);
	void HandlingLoadSurfaces(LPCSTR section);
	const SHandlingSurface* HandlingSurface(SGameMtl* m) const;
	void HandlingPhUpdate(float step);
	void HandlingPhTune(float step);
	void HandlingUpdateCL();
	void HandlingSteer(float step);
	bool HandlingIsRear(const SWheel& w) const { return (w.pos.z - m_vho.s.axle_mid_z) * m_vho.s.front_sign < 0.f; }
	void HandlingSurfaceFactors(const SHandlingSurface* s, float& grip, float& rolling, float& slip) const;
	void HandlingShift(size_t gear);
	void HandlingShiftRequest(int dir);
	void HandlingHandBrakeTap();
	bool HandlingInput(int cmd, bool pressed);
	float HandlingEngineTorque(float rpm);
	void HandlingHit(SHit& HDS);
	bool HandlingAllowFuse();
	void HandlingContact(SWheel& wheel, dContact& c, SGameMtl* ground);
	void HandlingSound(ref_sound& snd, float base_volume);
	Fvector HandlingDir(const Fvector& local); // a car axis (ax_right, ax_up, ax_fwd) in the world
	void HandlingBumpStops(float step);
	void HandlingStall(u32 reason);
	void HandlingFuel(float step);
	void HandlingWater(float dt);
	void HandlingTemp(float dt);
	void HandlingFlip(float step);
	void HandlingMeasureCog();
	void HandlingSizeBrakes();
	float HandlingWheelLoad(SWheel& w, float standing);
	void HandlingSpawnLift();
	void HandlingSetWheelInertia(SWheel& w, float extra);
	u16 HandlingCogFix(Fvector& saved);
	void HandlingCogRestore(u16 bone, const Fvector& saved);
	float HandlingInertiaAbout(const Fvector& axis) const;
	void HandlingInertiaAdd(const Fvector& k);
	void HandlingLimitCorrection(dContact& c) const;
	void HandlingDodge();
	static float CreatureMass(CEntityAlive* e);
	static void HandlingBodyContact(bool& do_colide, bool bo1, dContact& c, SGameMtl* material_1, SGameMtl* material_2);
	bool HandlingClutchMode() const { return m_vho.p.clutch_pedal > 0.f && m_vho.p.automatic <= 0.f; }
	static void HandlingCreatureContact(bool& do_colide, bool bo1, dContact& c, SGameMtl* material_1,
	                                    SGameMtl* material_2);
	bool HandlingIsGhost(CObject* obj);
	void HandlingRunOver(CObject* obj, bool knock);
	bool HandlingSelectGear(size_t gear);
	void HandlingTraceRecord(float step);
	void HandlingTraceEvent(u32 type, LPCSTR reason, u32 post_steps);
	void HandlingTraceSnapshot();
	void HandlingTraceWrite();
	void HandlingTraceFree();

public:
	float HandlingPushScale(CObject* pusher, bool explosion);
	float HandlingMassScale(); // 1250 kg / mass, at most 1.5: shoves that move anything alike, as a fixed impulse
	const SHandlingParams& HandlingParams() const { return m_vho.p; }
	void SetGear(int gear); // 1..N forward gears; holds a manual choice against the automatic box for a while
	void ShiftGear(int dir); // +1 up, -1 down
	void SetHandlingHeavyPushers(LPCSTR species_list);
	void SetHandlingDriveThrough(LPCSTR species_list); // creature species the car passes through and knocks down
	Fvector ChaseCameraPivot();
	bool HandlingActive() const { return m_vho.enabled && !m_speed_governed; }
	bool IsHandlingEnabled() const { return m_vho.enabled; }
	void SetHandlingEnabled(bool val);
	bool LoadHandling(LPCSTR section);
	LPCSTR GetHandlingSection() { return m_vho.section.size() ? *m_vho.section : ""; }
	float GetHandlingParam(LPCSTR name);
	bool SetHandlingParam(LPCSTR name, float value);
	bool SetHandlingGears(LPCSTR gears, float reverse, float final_drive);
	bool SetHandlingSurface(LPCSTR material, float grip, float rolling, float slip);
	void ClearHandlingSurfaces() { m_vho.surfaces.clear(); }
	float GetEngineRPM();
	u32 GetGearCount() { return m_gear_ratious.size() ? u32(m_gear_ratious.size() - 1) : 0; }
	int GetGear() { return int(m_current_transmission_num); } // 0 = reverse
	u32 GetStallCount() { return m_vho.s.stall_count; }
	// 1 worn engine, 2 (no longer used: was electric), 3 crash, 4 lugging, 5 flooded, 6 failed to start, 7 seized, 8 blocked by a script
	u32 GetStallReason() { return m_vho.s.stall_reason; }
	u32 GetCrashCount() { return m_vho.s.crash_count; }
	float GetLastCrashSpeed() { return m_vho.s.last_crash_speed; } // m/s lost in the last crash
	bool HandlingCollideSound();
	bool HandlingCrankStart(); // true: the overhaul handles this start attempt (cranking, maybe failing)
	bool SetLowRange(bool on); // false when the car has no low range or is moving
	bool IsLowRange() { return m_vho.s.low_range_on; }
	bool SetDiffLock(bool on); // false when the car has no locking differentials
	bool IsDiffLocked() { return m_vho.s.diff_locked; }
	bool IsHandBrakeLatched() { return m_vho.s.hand_latched; }
	void SetClutchPedal(bool held) { m_vho.s.clutch_held = held; } // clutch pedal mode (clutch_pedal): key down
	bool IsClutchHeld() { return m_vho.s.clutch_held; }
	void SetNeutral(bool on) { m_vho.s.neutral = on; if (on) m_vho.s.locked = false; }
	bool IsNeutral() { return m_vho.s.neutral; }
	float GetCogHeight() { return m_vho.s.cog_h; } // as built; cog_height replaces it when set
	// how much of NPC sight passes through this car's body (its windows) while someone drives it; 0 = stock
	float VisionTransparency() { return m_vho.enabled && Owner() ? m_vho.p.see_through : 0.f; }
	bool IsFlipped();
	bool RecoverFlip(); // rolls a flipped, slow car toward its wheels for a moment; call again to keep going
	float GetWaterDepth() { return m_vho.s.water_depth; }
	bool IsFlooded() { return m_vho.s.flood_timer > 0.f; }
	bool IsCranking() { return m_vho.s.crank_timer > 0.f; }
	float GetEngineTemp() { return m_vho.s.engine_temp; }
	bool HeadLightsOn() { return m_lights.AnyOn(); }
	void SetHeadLights(bool on) { if (on) m_lights.TurnOnHeadLights(); else m_lights.TurnOffHeadLights(); }
	void SetStartBlocked(bool on) { m_vho.s.start_blocked = on; }
	bool IsStartBlocked() { return m_vho.s.start_blocked; }
	float GetWheelDistance() { return m_vho.s.wheel_dist; }
	float GetRedlineRPM() { return m_max_rpm / (2.f * PI / 60.f); }
	float GetIdleRPM() { return m_min_rpm / (2.f * PI / 60.f); }
	Fvector GetDriverEyePos(); // driver's eye in bone 0 space (script attachment space), for dashboard mounts
	float GetThrottle() { return m_vho.s.throttle; }
	float GetBrake() { return m_vho.s.brake; }
	float GetClutch() { return m_vho.s.clutch; }
	float GetSteerInput() { return m_vho.s.steer; }
	bool IsOverRevving() { return m_vho.s.overrev; }
	float GetPhysicsMass() { return m_pPhysicsShell ? m_pPhysicsShell->getMass() : 0.f; }
	LPCSTR GetSurface();
	LPCSTR GetWheelDebug(); // driven wheels for the telemetry HUD: bone, km/h at the rim, * = on the ground
	// handling trace (debug_trace); see docs/vehicles/03-vehicle-handling-overhaul.md
	void SetHandlingTraceInfo(LPCSTR info) { m_vho.trace_info = info; }
	LPCSTR GetHandlingFlags(); // clutch and pedal state of the last step, for the telemetry HUD

#ifdef CAR_NEW
private:
	bool m_remote_control;

	u16 m_camera_bone_def;
	u16 m_camera_bone_aim;
	float m_zoom_factor_def;
	float m_zoom_factor_aim;
	bool m_zoom_status;
    float m_viewport_near;

    struct SScope
    {
        u16 camera_bone;
        float zoom_factor;
    };
    xr_vector<SScope> m_scopes;
    u16 m_scope_active;

	LPCSTR m_on_before_hit_callback;
	LPCSTR m_on_before_use_callback;
	LPCSTR m_on_before_engine_callback;

public:
    virtual bool is_ai_obstacle() const;
    void SetUseAction(LPCSTR txt);
    bool IsRemoteControl() { return m_remote_control; };
    bool IsCameraZoom() { return m_zoom_status; };
    u16 GetCameraBone();
    float GetZoomFactor();
    float GetViewportNear() { return m_viewport_near; };
    void SetViewportNear(float val) { m_viewport_near = val; };

    bool IsScopeEnable() { return m_scopes.size() > 0; };
    u16 GetScopeSize() { return m_scopes.size(); };
    u16 GetScopeActive() { return m_scope_active; };
    void SetScopeActive(u16 val);
    void ScopeOnMouseWheel(int direction);

/*----------------------------------------------------------------------------------------------------
    CCarDrone
----------------------------------------------------------------------------------------------------*/
private:
    CCarDrone* m_car_drone;
    void CCarDrone_OnKeyboardPress(int dik);
    void CCarDrone_OnKeyboardRelease(int dik);
    void CCarDrone_OnKeyboardHold(int dik);

public:
    CCarDrone* CarDrone() { return m_car_drone; };
    /* Find a way to export CCarDrone to LUA then obj:get_car():CarDrone():CCarDrone_function() instead of doing these. */
    float CCarDrone_GetPowerEfficiency();
    void CCarDrone_SetPowerEfficiency(int val);

/*----------------------------------------------------------------------------------------------------
    SVisualCamera
----------------------------------------------------------------------------------------------------*/
public:
    struct SVisualCamera
    {
        /* 
        A physical camera model on the vehicle.
        If used, camera will lock fixed on this "turret".
        Mouse movement will rotate this "turret" instead of camera.
        */
        CCar* m_car;
        CCar* Car() { return m_car; }

        bool m_enable;
        bool IsEnable() { return m_enable; }

        bool m_active;
        bool IsActive() { return m_active; }
        void Activate(bool val);
        bool m_snap;
        bool Snap() { return m_snap; }

        u16 m_rotate_x_bone;
        u16 m_rotate_y_bone;
        float m_rotate_x_speed;
        float m_rotate_y_speed;
        float m_tgt_x_rot;
        float m_tgt_y_rot;
        float m_cur_x_rot;
        float m_cur_y_rot;
        Fvector2 m_lim_x_rot;
        Fvector2 m_lim_y_rot;

        Fvector m_desire_ang;
        Fvector GetDesireAngle() { return m_desire_ang; }
        void SetDesireAngle(Fvector vec);

        SVisualCamera(CCar* obj);
        ~SVisualCamera();
        void VisualUpdate(float fov);
        void UpdateDir();
        void UpdateCam();

        static void _BCL BoneCallbackX(CBoneInstance* B);
        static void _BCL BoneCallbackY(CBoneInstance* B);
        void SetBoneCallbacks(bool val);
        bool attach_Actor(CGameObject* obj);
        void detach_Actor();
        void net_Spawn(CSE_Abstract* DC);
        void OnMouseMove(int x, int y);
    } m_visual_camera;
    Fvector VisualCamera_GetDesireAngle() { return m_visual_camera.GetDesireAngle(); }
    float VisualCamera_GetRotXCur() { return -m_visual_camera.m_cur_x_rot; }
    float VisualCamera_GetRotYCur() { return -m_visual_camera.m_cur_y_rot; }
#endif

public:
DECLARE_SCRIPT_REGISTER_FUNCTION
};
