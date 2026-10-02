#include "stdafx.h"
#include <limits>
#include "Actor.h"
#include "ActorParkour.h"
#include "ai_space.h"
#include "script_engine.h"
#include "player_hud.h"
#include "CharacterPhysicsSupport.h"
#include "PHMovementControl.h"
#include "../xrPhysics/PHActorCharacter.h"
#include "inventory.h"
#include "HudItem.h"
#include "customdevice.h"
#include "../xrEngine/CameraBase.h"

void CActorParkour::BeginCamera()
{
    auto* camera=m_actor->cameras[eacFirstEye];
    m_savedYawLimits.set(camera->lim_yaw.x,camera->lim_yaw.y,camera->bClampYaw?1.f:0.f);
    m_savedPitchLimits.set(camera->lim_pitch.x,camera->lim_pitch.y,camera->bClampPitch?1.f:0.f);
    m_savedFreelook=m_actor->cam_freelook;m_savedTorsoYaw=m_actor->old_torso_yaw;
    m_lockedYaw=camera->yaw;m_guidePitch=camera->pitch;m_cameraOwned=true;
    Fvector aim;aim.sub(m_candidate.lip,camera->vPosition);
    const float target=-atan2f(aim.y,_max(.20f,sqrtf(aim.x*aim.x+aim.z*aim.z)));
    const float error=target-m_guidePitch;
    m_guideDirection=error < -m_options.cameraDeadzone ? -1.f :
        error > m_options.cameraDeadzone ? 1.f : 0.f;
    m_actor->old_torso_yaw=-m_actor->r_torso.yaw;
    m_actor->cam_freelook=eflEnabled;m_actor->freelook_cam_control=1.f;
    const float range=m_options.limitedFreelook?m_options.freelookYaw:0.f;
    camera->lim_yaw.set(m_lockedYaw-range,m_lockedYaw+range);camera->bClampYaw=true;
}

void CActorParkour::UpdateCamera(float dt)
{
    if(!m_cameraOwned||!m_active||dt<=0.f) return;
    auto* camera=m_actor->cameras[eacFirstEye];
    m_actor->cam_freelook=eflEnabled;m_actor->freelook_cam_control=1.f;
    if(m_options.cameraGuide && m_guideDirection!=0.f &&
        !(m_options.levelDownwardView && !m_reaching && m_guideDirection>0.f))
    {
        Fvector aim;aim.sub(m_candidate.lip,camera->vPosition);
        const float target=-atan2f(aim.y,_max(.20f,sqrtf(aim.x*aim.x+aim.z*aim.z)));
        const float error=(target-m_guidePitch)*m_guideDirection-m_options.cameraDeadzone;
        // Only bring a distant-from-center ledge to the edge of the dead zone.
        // Never reverse the initial guidance to level the view after rising.
        // Measure against the guide, not manual freelook, so looking aside isn't fought.
        if(error>0.f)
        {
            const float correction=_min(error*(1.f-expf(-dt*2.f)),dt*.52f)*
                m_options.cameraStrength*m_guideDirection;
            m_guidePitch+=correction;camera->pitch+=correction;
        }
    }
    if (m_options.levelDownwardView && camera->pitch > 0.f && !m_reaching)
    {
        const float correction=_min(camera->pitch,
            m_options.cameraLevelSpeed*dt*(1.f-expf(-camera->pitch*4.f)));
        camera->pitch-=correction;
        m_guidePitch=_min(m_guidePitch,camera->pitch);
    }
    const float yawRange=m_options.limitedFreelook?m_options.freelookYaw:0.f;
    const float pitchRange=m_options.limitedFreelook?m_options.freelookPitch:0.f;
    camera->lim_yaw.set(m_lockedYaw-yawRange,m_lockedYaw+yawRange);
    camera->lim_pitch.set(m_guidePitch-pitchRange,m_guidePitch+pitchRange);
    if (m_savedPitchLimits.z!=0.f)
    {
        camera->lim_pitch.x=_max(camera->lim_pitch.x,m_savedPitchLimits.x);
        camera->lim_pitch.y=_min(camera->lim_pitch.y,m_savedPitchLimits.y);
    }
    camera->bClampYaw=camera->bClampPitch=true;
    clamp(camera->yaw,camera->lim_yaw.x,camera->lim_yaw.y);
    clamp(camera->pitch,camera->lim_pitch.x,camera->lim_pitch.y);
}

void CActorParkour::EndCamera()
{
    if(!m_cameraOwned) return;
    auto* camera=m_actor->cameras[eacFirstEye];m_cameraOwned=false;
    camera->lim_yaw.set(m_savedYawLimits.x,m_savedYawLimits.y);camera->bClampYaw=m_savedYawLimits.z!=0.f;
    camera->lim_pitch.set(m_savedPitchLimits.x,m_savedPitchLimits.y);camera->bClampPitch=m_savedPitchLimits.z!=0.f;
    m_actor->cam_freelook=m_savedFreelook;
    m_actor->old_torso_yaw=m_savedTorsoYaw;
    m_actor->freelook_cam_control=m_savedFreelook==eflDisabled?0.f:1.f;
}

namespace
{
    luabind::object Table() { return luabind::newtable(ai().script_engine().lua()); }
    float Number(const luabind::object& table, LPCSTR key, float fallback)
    {
        if (!table || table.type() != LUA_TTABLE) return fallback;
        auto value = table[key];
        if (!value || value.type() == LUA_TNIL) return fallback;
        if (value.type() != LUA_TNUMBER) return std::numeric_limits<float>::quiet_NaN();
        float n = luabind::object_cast<float>(value);
        return n;
    }
    bool Boolean(const luabind::object& table, LPCSTR key, bool fallback)
    {
        if (!table || table.type() != LUA_TTABLE) return fallback;
        auto value = table[key];
        return value && value.type() == LUA_TBOOLEAN ? luabind::object_cast<bool>(value) : fallback;
    }
    luabind::object Query(luabind::object options)
    {
        auto result = Table();
        auto* actor = Actor();
        result["ok"] = false;
        if (!actor) { result["reason"] = "no_actor"; return result; }
        CActorParkour::Options p;
        p.reach = Number(options, "reach", .30f);
        p.minHeight = Number(options, "min_height", .30f);
        p.minMantleHeight = Number(options, "min_mantle_height", 0.f);
        p.minVaultHeight = Number(options, "min_vault_height", 0.f);
        p.heightOrigin = Number(options, "height_origin", actor->Position().y);
        p.hasHeightOrigin = true;
        p.crouchAtTop = Boolean(options, "crouch_at_top", false);
        p.topCrouchStart = Number(options, "top_crouch_start", .75f);
        p.vaultHeight = Number(options, "vault_height", 1.f);
        p.mantleHeight = Number(options, "mantle_height", 1.4f);
        p.maxHeight = Number(options, "max_height", 2.4f);
        p.maxDepth = Number(options, "max_depth", 1.2f);
        p.maxDrop = Number(options, "max_drop", 2.2f);
        p.maxFall = Number(options, "max_fall", 6.f);
        p.momentum = Number(options, "momentum", .8f);
        p.horizontalSpeed = Number(options, "horizontal_speed", 2.f);
        p.verticalSpeed = Number(options, "vertical_speed", 2.f);
        p.preferVault = Boolean(options, "prefer_vault", false);
        p.allowClimbVault = Boolean(options, "allow_climb_vault", true);
        p.forwardControl = Boolean(options,"forward_control",false);
        p.forwardHeld = Boolean(options,"forward_held",false);
        p.allowClimb = Boolean(options, "allow_climb", true);
        p.allowMantle = Boolean(options, "allow_mantle", true);
        p.allowVault = Boolean(options, "allow_vault", true);
        p.onlyHolstered = Boolean(options, "only_holstered", false);
        p.airborneTop = Boolean(options,"airborne_top",true);
        p.cameraGuide = Boolean(options,"camera_guide",true);
        p.levelDownwardView = Boolean(options,"level_downward_view",false);
        p.cameraLevelSpeed = deg2rad(Number(options,"camera_level_speed",0.f));
        p.grabHorizontal = deg2rad(Number(options,"grab_horizontal_deg",0.f));
        p.grabVertical = deg2rad(Number(options,"grab_vertical_deg",0.f));
        p.catchDropPerSpeed = Number(options,"catch_drop_per_speed",0.f);
        p.catchMaxDrop = Number(options,"catch_max_drop",0.f);
        p.catchBrakeTime = Number(options,"catch_brake_time",.15f);
        p.limitedFreelook = Boolean(options,"limited_freelook",true);
        p.armReachHeight = Number(options, "arm_reach_height", 2.15f);
        p.armExtension = Number(options, "arm_extension", .80f);
        p.reachTimeout = Number(options, "reach_timeout", .45f);
        p.handSpacing = Number(options, "hand_spacing", .48f);
        p.handDepth = Number(options, "hand_depth", .06f);
        p.handsToLedgeHeightOffset = Number(options, "hands_to_ledge_height_offset", .04f);
        p.centerAssist = Number(options, "center_assist", .24f);
        p.maxSlope = deg2rad(Number(options, "max_slope", 55.f));
        p.shoulderAdjustment = Number(options, "shoulder_adjustment", .25f);
        p.cameraStrength = Number(options, "camera_strength", .65f);
        p.cameraDeadzone = deg2rad(Number(options, "camera_deadzone", 30.f));
        p.freelookYaw = deg2rad(Number(options, "freelook_yaw", 60.f));
        p.freelookPitch = deg2rad(Number(options, "freelook_pitch", 45.f));
        p.viewHorizontal = deg2rad(Number(options, "view_horizontal_deg", 22.f));
        p.viewUp = deg2rad(Number(options, "view_up_deg", 70.f));
        p.viewDown = deg2rad(Number(options, "view_down_deg", 60.f));
        p.minHandSpacing = Number(options, "min_hand_spacing", _min(.14f,p.handSpacing));
        const float values[] = {p.cameraLevelSpeed,p.grabHorizontal,p.grabVertical,p.catchDropPerSpeed,p.catchMaxDrop,p.catchBrakeTime,p.minMantleHeight, p.minVaultHeight, p.topCrouchStart, p.reach, p.minHeight, p.maxHeight, p.vaultHeight, p.mantleHeight,
            p.maxDepth, p.maxDrop, p.maxFall, p.momentum, p.horizontalSpeed, p.verticalSpeed,
            p.armReachHeight, p.armExtension, p.reachTimeout, p.handSpacing, p.minHandSpacing,
            p.handDepth, p.handsToLedgeHeightOffset, p.centerAssist, p.maxSlope, p.shoulderAdjustment, p.cameraStrength,
            p.cameraDeadzone, p.freelookYaw, p.freelookPitch, p.viewHorizontal, p.viewUp, p.viewDown};
        for (float value : values)
            if (!_valid(value) || value < 0.f)
            { result["reason"] = "invalid_options"; return result; }
        // Reject invalid geometry/timing instead of silently changing Lua's values.
        if (p.catchBrakeTime <= 0.f || p.grabHorizontal >= deg2rad(90.f) || p.grabVertical >= deg2rad(90.f) ||
            !_valid(p.heightOrigin) || p.topCrouchStart <= 0.f || p.topCrouchStart >= 1.f ||
            p.maxHeight < p.minHeight || p.mantleHeight < p.vaultHeight ||
            p.horizontalSpeed <= 0.f || p.verticalSpeed <= 0.f || p.reachTimeout <= 0.f ||
            p.handSpacing <= 0.f || p.minHandSpacing > p.handSpacing ||
            p.maxSlope >= deg2rad(90.f))
        { result["reason"] = "invalid_options"; return result; }
        const bool ok = actor->Parkour().Query(p);
        result["ok"] = ok; result["reason"] = actor->Parkour().Reason();
        const bool debug=Boolean(options,"debug",false);
        if(debug && actor->cam_Active())
        {
            result["origin"]=actor->cam_Active()->vPosition;
            Fvector end;end.mad(actor->cam_Active()->vPosition,actor->cam_Active()->vDirection,.8f);
            result["search_end"]=end;
        }
        if (ok)
        {
            const auto& c = actor->Parkour().Result();
            result["action"] = c.action; result["route"] = c.route; result["rise"] = c.rise;
            result["airborne"] = c.airborne;
            result["obstacle_height"] = c.obstacleHeight;
            result["velocity_forward"] = c.velocity.dotproduct(c.direction);
            result["velocity_vertical"] = c.velocity.y;
            result["duration_hint"] = c.distances.back();
            result["free_release"] = c.freeRelease;
            result["lip"] = c.lip; result["landing"] = c.landing;
            if(debug && Boolean(options,"debug_path",false))
            {
                auto path=Table();
                for(size_t i=0;i<c.path.size();++i) path[int(i+1)]=c.path[i];
                result["path"]=path;
            }
        }
        return result;
    }
    luabind::object Start(float seconds, bool hands)
    {
        auto result = Table(); auto* actor = Actor();
        result["ok"] = actor && actor->Parkour().Start(seconds, hands);
        result["reason"] = actor ? actor->Parkour().Reason() : "no_actor";
        if (actor) { result["duration"] = actor->Parkour().Duration(); result["action"] = actor->Parkour().Result().action;
            result["hand_contacts"] = actor->Parkour().HandContactsReady(); }
        return result;
    }
    bool Active() { return Actor() && Actor()->Parkour().Active(); }
    bool Crouched() { return Actor() && Actor()->Parkour().Crouched(); }
    bool Airborne()
    {
        auto* actor = Actor();
        return actor && actor->Parkour().Airborne();
    }
    float Progress() { return Actor() ? Actor()->Parkour().Progress() : 0.f; }
    float VerticalSpeed()
    {
        auto* actor = Actor();
        return actor && actor->character_physics_support() ?
            actor->character_physics_support()->movement()->GetVelocity().y : 0.f;
    }
    CPHActorCharacter* ActorCharacter()
    {
        auto* actor = g_actor;
        if (!actor || !actor->character_physics_support()) return nullptr;
        auto* movement = actor->character_physics_support()->movement();
        if (!movement || !movement->PHCharacter()) return nullptr;
        return movement->PHCharacter()->CastActorCharacter();
    }
    bool StepOver(bool enabled, float height)
    {
        auto* character = ActorCharacter();
        if (!character) return false;
        character->SetSmoothStep(enabled, height);
        return true;
    }
    bool StepOverStrength(float strength)
    {
        auto* character = ActorCharacter();
        if (!character) return false;
        character->SetSmoothStepStrength(strength);
        return true;
    }
    float StepOverStrengthValue()
    {
        auto* character = ActorCharacter();
        return character ? character->SmoothStepStrength() : 1.f;
    }
    void TerrainTuning(bool actor, bool npc, float penalty)
    {
        CPHSimpleCharacter::ConfigureTerrain(actor,npc,penalty);
    }
    bool StepOverSpeed(float multiplier, float start, float full)
    {
        auto* character=ActorCharacter();
        if(!character) return false;
        character->SetSmoothStepSpeed(multiplier,start,full);
        return true;
    }
    bool StepOverEnabled()
    {
        auto* character = ActorCharacter();
        return character && character->SmoothStepEnabled();
    }
    float StepOverHeight()
    {
        auto* character = ActorCharacter();
        return character ? character->SmoothStepHeight() : 0.f;
    }
    void Cancel()
    {
        if (auto* actor = Actor()) actor->Parkour().Cancel();
    }
    float CarryCapacity() { return g_actor ? g_actor->MaxCarryWeight() : 0.f; }
    void MovementScale(float scale)
    {
        if (g_actor) g_actor->SetScriptMovementScale(scale);
    }
    void ForwardHeld(bool held)
    {
        if (auto* actor = Actor()) actor->Parkour().SetForwardHeld(held);
    }
    void StopHands()
    {
        if (g_player_hud && g_player_hud->ParkourMotionPlaying()) g_player_hud->StopScriptAnim();
    }
    void Holster()
    {
        auto* actor = Actor();
        if (!actor || !actor->Parkour().Active()) return;
        auto& inventory = actor->inventory();
        if (auto* item = inventory.ActiveItem())
        {
            if (auto* hud = item->cast_hud_item())
            {
                hud->OnHiddenItem(); // stops zoom and weapon-specific activity
                hud->SwitchState(CHUDState::eHidden);
                if (g_player_hud) g_player_hud->detach_item(hud);
            }
            // Keep the real inventory slot. Saving/cancelling must not strand a
            // weapon in the backpack or depend on delayed move-to-slot events.
            inventory.Activate(NO_ACTIVE_SLOT, true);
        }
        if (auto* detector = smart_cast<CCustomDevice*>(inventory.ItemFromSlot(DETECTOR_SLOT)))
            detector->ForceHide();
    }
    luabind::object Status()
    {
        auto result = Table(); auto* actor = Actor();
        result["active"] = Active(); result["progress"] = Progress();
        result["reaching"] = actor && actor->Parkour().Reaching();
        result["reason"] = actor ? actor->Parkour().Reason() : "no_actor";
        result["duration"] = actor ? actor->Parkour().Duration() : 0.f;
        result["action"] = actor ? actor->Parkour().Result().action : "none";
        result["hand_weight"] = actor ? actor->Parkour().HandWeight() : 0.f;
        return result;
    }
}

void RegisterParkour(lua_State* L)
{
    using namespace luabind;
    module(L, "level") [def("parkour_query", &Query), def("parkour_start", &Start),
        def("parkour_active", &Active), def("parkour_progress", &Progress),
        def("parkour_cancel", &Cancel), def("parkour_forward_held", &ForwardHeld), def("parkour_status", &Status), def("parkour_crouched", &Crouched),
        def("parkour_airborne", &Airborne), def("parkour_stop_hands", &StopHands),
        def("parkour_holster", &Holster), def("parkour_vertical_speed", &VerticalSpeed),
        def("actor_carry_capacity", &CarryCapacity), def("actor_movement_scale", &MovementScale),
        def("actor_step_over", &StepOver), def("actor_step_over_enabled", &StepOverEnabled),
        def("actor_step_over_strength", &StepOverStrength), def("actor_step_over_strength_value", &StepOverStrengthValue),
        def("actor_step_over_speed", &StepOverSpeed),
        def("actor_step_over_height", &StepOverHeight), def("movement_terrain_tuning", &TerrainTuning)];
}
