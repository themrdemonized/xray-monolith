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
#include "Weapon.h"
#include "../xrEngine/CameraBase.h"

void CActorParkour::BeginCamera()
{
    // The guide steers the first-person view only; a third-person camera stays free.
    if(m_actor->cam_active!=eacFirstEye) return;
    auto* camera=m_actor->cameras[eacFirstEye];
    m_savedYawLimits.set(camera->lim_yaw.x,camera->lim_yaw.y,camera->bClampYaw?1.f:0.f);
    m_savedPitchLimits.set(camera->lim_pitch.x,camera->lim_pitch.y,camera->bClampPitch?1.f:0.f);
    m_savedFreelook=m_actor->cam_freelook;m_savedTorsoYaw=m_actor->old_torso_yaw;
    m_lockedYaw=camera->yaw;m_guidePitch=camera->pitch;m_cameraOwned=true;
    if (m_aimReady)
    {
        // A ready weapon follows the view: the stock camera, only held in the limits.
        m_actor->cam_freelook=eflDisabled;m_actor->freelook_cam_control=0.f;
    }
    else
    {
        m_actor->old_torso_yaw=-m_actor->r_torso.yaw;
        m_actor->cam_freelook=eflEnabled;m_actor->freelook_cam_control=1.f;
    }
    const float range=m_options.limitedFreelook?m_options.freelookYaw:0.f;
    camera->lim_yaw.set(m_lockedYaw-range,m_lockedYaw+range);camera->bClampYaw=true;
}

void CActorParkour::UpdateCamera(float dt)
{
    if(!m_cameraOwned||!m_active||dt<=0.f) return;
    auto* camera=m_actor->cameras[eacFirstEye];
    if (m_aimReady) { m_actor->cam_freelook=eflDisabled;m_actor->freelook_cam_control=0.f; }
    else { m_actor->cam_freelook=eflEnabled;m_actor->freelook_cam_control=1.f; }
    if (m_options.cameraGuide)
    {
        const float progress=Progress();
        const float over=progress*progress*(3.f-2.f*progress);
        // Aimed from the eyes' place on the guided path, not from the camera: the camera
        // rides the animated head and is pushed off walls, and steering by it would feed
        // that motion back into the view.
        Fvector eye=m_actor->Position();eye.y+=m_bodyEyeHeight>0.f?m_bodyEyeHeight:1.6f;
        Fvector aim;aim.sub(m_candidate.lip,eye);
        const float ledgePitch=-atan2f(aim.y,_max(.20f,sqrtf(aim.x*aim.x+aim.z*aim.z)));
        const float targetPitch=ledgePitch*(1.f-over);
        // Camera yaw is -getH(), i.e. +atan2(x,z); a negated yaw mirrors the exit.
        const float targetYaw=atan2f(m_candidate.direction.x,m_candidate.direction.z);
        // Approach gently, then increasingly face the exit as the body crosses.
        // The remaining-time response also works for short vaults without a snap.
        const float response=m_options.cameraStrength*(1.f+6.f*over)/
            _max(.10f,m_duration*(1.f-progress));
        const float blend=1.f-expf(-dt*response);
        const float deadzone=m_options.cameraDeadzone*(1.f-over);
        const float error=targetPitch-camera->pitch;
        float pitchDelta=(error>0.f?_max(0.f,error-deadzone):_min(0.f,error+deadzone))*blend;
        const float speedLimit=m_options.cameraLevelSpeed;
        if (speedLimit>0.f) clamp(pitchDelta,-dt*speedLimit,dt*speedLimit);
        const float yawError=atan2f(sinf(targetYaw-camera->yaw),cosf(targetYaw-camera->yaw));
        float yawDelta=yawError*blend*over;
        if (speedLimit>0.f) clamp(yawDelta,-dt*speedLimit,dt*speedLimit);
        camera->pitch+=pitchDelta;camera->yaw+=yawDelta;
        m_guidePitch+=pitchDelta;m_lockedYaw+=yawDelta;
    }
    else if (m_options.levelDownwardView && camera->pitch>0.f && !m_reaching)
    {
        const float correction=_min(camera->pitch,
            m_options.cameraLevelSpeed*dt*(1.f-expf(-camera->pitch*4.f)));
        camera->pitch-=correction;m_guidePitch-=correction;
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
    if (!m_aimReady) m_actor->old_torso_yaw=m_savedTorsoYaw;
    m_actor->freelook_cam_control=m_savedFreelook==eflDisabled?0.f:1.f;
}

void ParkourSetDebugLog(bool enabled);
void ParkourLogDiagnostics(const CActorParkour& parkour, LPCSTR stage);

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
    xr_string Text(const luabind::object& table, LPCSTR key, LPCSTR fallback)
    {
        if (!table || table.type() != LUA_TTABLE) return fallback;
        auto value = table[key];
        return value && value.type() == LUA_TSTRING ? xr_string(luabind::object_cast<LPCSTR>(value)) : xr_string(fallback);
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
        p.crossTop = Boolean(options,"cross_top",true);
        p.vaultSpeed = _max(0.f, Number(options,"vault_speed",0.f));
        p.minTopDepth = _max(0.f, Number(options,"min_top_depth",0.f));
        p.allowClimb = Boolean(options, "allow_climb", true);
        p.allowMantle = Boolean(options, "allow_mantle", true);
        p.allowVault = Boolean(options, "allow_vault", true);
        p.onlyHolstered = Boolean(options, "only_holstered", false);
        p.airborneTop = Boolean(options,"airborne_top",true);
        p.cameraGuide = Boolean(options,"camera_guide",true);
        p.levelDownwardView = Boolean(options,"level_downward_view",false);
        p.cameraLevelSpeed = deg2rad(Number(options,"camera_level_speed",0.f));
        p.maxCatchSpeed = Number(options,"max_catch_speed",5.5f);
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
        p.boxWidth = Number(options, "box_width", .45f);
        p.boxTop = Number(options, "box_top", .75f);
        p.boxBottom = Number(options, "box_bottom", .65f);
        p.detectRange = Number(options, "detect_range", 1.4f);
        p.target.set(Number(options, "target_x", 0.f), Number(options, "target_y", 0.f), Number(options, "target_z", 0.f));
        p.hasTarget = Boolean(options, "has_target", false) && _valid(p.target);
        p.targetRadius = Number(options, "target_radius", .30f);
        p.minHandSpacing = Number(options, "min_hand_spacing", _min(.14f,p.handSpacing));
        p.standWidth = Number(options, "stand_width", .09f);
        p.wallStep = Number(options, "wall_step", .35f);
        p.heavyLoad = Number(options, "heavy_load", .5f);
        p.kneeFloor = Boolean(options, "knee_floor", false);
        ParkourSetDebugLog(Boolean(options, "debug_log", false));
        const float values[] = {p.cameraLevelSpeed,p.maxCatchSpeed,p.boxWidth,p.boxTop,p.boxBottom,p.detectRange,p.targetRadius,p.catchDropPerSpeed,p.catchMaxDrop,p.catchBrakeTime,p.minMantleHeight, p.minVaultHeight, p.topCrouchStart, p.reach, p.minHeight, p.maxHeight, p.vaultHeight, p.mantleHeight,
            p.maxDepth, p.maxDrop, p.maxFall, p.momentum, p.horizontalSpeed, p.verticalSpeed,
            p.armReachHeight, p.armExtension, p.reachTimeout, p.handSpacing, p.minHandSpacing,
            p.handDepth, p.handsToLedgeHeightOffset, p.centerAssist, p.maxSlope, p.shoulderAdjustment, p.cameraStrength,
            p.cameraDeadzone, p.freelookYaw, p.freelookPitch, p.standWidth, p.wallStep, p.heavyLoad,
            p.vaultSpeed, p.minTopDepth};
        for (float value : values)
            if (!_valid(value) || value < 0.f)
            { result["reason"] = "invalid_options"; return result; }
        // Reject invalid geometry/timing instead of silently changing Lua's values.
        if (p.catchBrakeTime <= 0.f || p.boxWidth <= 0.f || p.boxTop <= 0.f || p.boxBottom <= 0.f ||
            !_valid(p.heightOrigin) || p.topCrouchStart <= 0.f || p.topCrouchStart >= 1.f ||
            p.maxHeight < p.minHeight || p.mantleHeight < p.vaultHeight ||
            p.horizontalSpeed <= 0.f || p.verticalSpeed <= 0.f || p.reachTimeout <= 0.f ||
            p.handSpacing <= 0.f || p.minHandSpacing > p.handSpacing || p.standWidth <= 0.f ||
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
            result["tentative"] = c.tentative;
            result["action"] = c.action; result["route"] = c.route; result["rise"] = c.rise;
            result["airborne"] = c.airborne;
            result["obstacle_height"] = c.obstacleHeight;
            result["velocity_forward"] = c.velocity.dotproduct(c.direction);
            result["velocity_vertical"] = c.velocity.y;
            if (!c.distances.empty()) result["duration_hint"] = c.distances.back();
            result["free_release"] = c.freeRelease;
            result["lip"] = c.lip; result["landing"] = c.landing;
            result["direction"] = c.direction;
            // For a script choosing the presentation: a top low enough to step onto,
            // and (for anything the body hangs from: a climb, a window, a fence) whether
            // the feet have a wall below the edge to push or kick off.
            result["step"] = actor->Parkour().StepUp(c);
            if (!c.tentative && c.rise > 1.2f)
                result["wall_below"] = actor->Parkour().CandidateWallBelow();
            result["wall_kick"] = !c.tentative && actor->Parkour().CandidateWallKick();
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
        if (actor) ParkourLogDiagnostics(actor->Parkour(), "start");
        result["reason"] = actor ? actor->Parkour().Reason() : "no_actor";
        if (actor) { result["duration"] = actor->Parkour().Duration(); result["action"] = actor->Parkour().Result().action;
            result["hand_contacts"] = actor->Parkour().HandContactsReady();
            // A step onto a low top uses the feet: no hands clip.
            result["step"] = actor->Parkour().Active() && actor->Parkour().StepUp(actor->Parkour().Result()); }
        return result;
    }
    // A script that chooses the presentation itself (Tactical Traversal):
    // presentation = {hands = "none"|"left"|"right"|"both", clip = {name, start = "ground"|"air"|"any",
    // height, face, contact, release, frames, mirror, lead, finish, land, ahead, fit}}
    // (see CActorParkour::ClipDef). Without a valid clip the body uses
    // the procedural pose. The engine still validates the route and owns the movement.
    luabind::object Begin(float seconds, luabind::object presentation)
    {
        using Clip = CActorParkour::ClipDef;
        const xr_string hands = Text(presentation, "hands", "both");
        const u8 mask = hands == "none" ? 0 : hands == "left" ? 1 : hands == "right" ? 2 : 3;
        if (auto* actor = Actor())
        {
            Clip clip = {};
            xr_string name;
            luabind::object def;
            if (presentation && presentation.type() == LUA_TTABLE) def = presentation["clip"];
            if (def && def.type() == LUA_TTABLE)
            {
                const xr_string start = Text(def, "start", "ground");
                const float height = Number(def, "height", -1.f), face = Number(def, "face", -1.f);
                const float contact = Number(def, "contact", 0.f), release = Number(def, "release", 0.f);
                const float frames = Number(def, "frames", 0.f);
                if (_valid(height) && _valid(face) && _valid(contact) && _valid(release) && _valid(frames) &&
                    height >= 0.f && face >= 0.f && contact >= 0.f && release >= 0.f && frames > contact && frames < 255.f)
                {
                    name = Text(def, "name", "");
                    clip.start = u8(start == "air" ? Clip::air : start == "any" ? Clip::any : Clip::ground);
                    clip.height = height; clip.face = face;
                    clip.contact = u8(contact); clip.release = u8(release); clip.frames = u8(frames);
                    clip.mirror = Boolean(def, "mirror", false); clip.wall = true;
                    const float lead = Number(def, "lead", 0.f), finish = Number(def, "finish", 0.f);
                    clip.lead = u8(_valid(lead) ? clampr(lead, 0.f, frames - 1.f) : 0.f);
                    clip.finish = u8(_valid(finish) && finish > contact ? _min(finish, frames) : 0.f);
                    const float land = Number(def, "land", 0.f), ahead = Number(def, "ahead", -1.f);
                    clip.fit = Boolean(def, "fit", false) && _valid(land) && _valid(ahead);
                    clip.land = clip.fit ? land : 0.f; clip.ahead = clip.fit ? ahead : -1.f;
                }
            }
            clip.name = name.c_str();
            actor->Parkour().SetAimReady(Boolean(presentation, "aim", false));
            actor->Parkour().Present(clip, mask, Number(presentation, "blend_in", 1.f/6.f),
                Number(presentation, "blend_out", .25f));
        }
        return Start(seconds, mask != 0);
    }
    // A body clip played on its own (CActorParkour::Perform): {clip = {name, frames,
    // lead, finish, mirror}, distance, camera = "height"|"follow", blend_in, blend_out}.
    luabind::object Perform(float seconds, luabind::object options)
    {
        auto result = Table();
        result["ok"] = false;
        auto* actor = Actor();
        if (!actor) { result["reason"] = "no_actor"; return result; }
        luabind::object def;
        if (options && options.type() == LUA_TTABLE) def = options["clip"];
        if (!def || def.type() != LUA_TTABLE) { result["reason"] = "no_clip"; return result; }
        const float frames = Number(def, "frames", 0.f), lead = Number(def, "lead", 0.f), finish = Number(def, "finish", 0.f);
        if (!_valid(frames) || frames < 2.f || frames >= 255.f) { result["reason"] = "invalid_clip"; return result; }
        const xr_string name = Text(def, "name", "");
        CActorParkour::ClipDef clip = {};
        clip.name = name.c_str();
        clip.frames = u8(frames);
        clip.lead = u8(_valid(lead) ? clampr(lead, 0.f, frames - 1.f) : 0.f);
        clip.finish = u8(_valid(finish) && finish > 0.f ? _min(finish, frames) : 0.f);
        clip.mirror = Boolean(def, "mirror", false); clip.wall = true;
        const bool ok = actor->Parkour().Perform(seconds, clip, Number(options, "distance", 0.f),
            Text(options, "camera", "height") == "follow" ? 2 : 1,
            Number(options, "blend_in", .1f), Number(options, "blend_out", .2f));
        result["ok"] = ok; result["reason"] = actor->Parkour().Reason();
        return result;
    }
    // What the hands hold now, so the script can choose what the move does with it.
    // Empty table: nothing in hand. single: the item is used one-handed (weapon
    // config single_handed, throwables, knives); busy: mid action (reload, fire,
    // switch, throw, show/hide), its hands cannot be taken.
    luabind::object HandsState()
    {
        auto result = Table(); auto* actor = Actor();
        if (!actor) return result;
        PIItem item = actor->inventory().ActiveItem();
        if (!item) return result;
        result["section"] = item->object().cNameSect().c_str();
        result["single"] = item->IsSingleHanded();
        bool busy = false;
        if (auto* hud = item->cast_hud_item())
            busy = hud->IsPending() || (hud->GetState() != CHUDState::eIdle && hud->GetState() != CHUDState::eBore);
        auto* weapon = smart_cast<CWeapon*>(item);
        result["weapon"] = weapon != nullptr;
        result["zoomed"] = weapon && weapon->IsZoomed();
        result["lowerable"] = weapon && weapon->m_bCanBeLowered;
        result["busy"] = busy;
        return result;
    }
    // Clears the hands a move needs: weapon/detector "hide" (instantly, slot kept, as
    // Holster) or "keep" (a kept weapon leaves its sights; it is lowered by the script).
    void PrepareHands(luabind::object options)
    {
        auto* actor = Actor();
        if (!actor || !actor->Parkour().Active()) return;
        auto& inventory = actor->inventory();
        if (auto* item = inventory.ActiveItem())
        {
            if (Text(options, "weapon", "hide") == "hide")
            {
                if (auto* hud = item->cast_hud_item())
                {
                    hud->OnHiddenItem();
                    hud->SwitchState(CHUDState::eHidden);
                    if (g_player_hud) g_player_hud->detach_item(hud);
                }
                inventory.Activate(NO_ACTIVE_SLOT, true);
            }
            else if (auto* weapon = smart_cast<CWeapon*>(item); weapon && weapon->IsZoomed())
                weapon->OnZoomOut();
        }
        if (Text(options, "detector", "hide") == "hide")
            if (auto* detector = smart_cast<CCustomDevice*>(inventory.ItemFromSlot(DETECTOR_SLOT)))
                detector->ForceHide();
    }
    bool Active() { return Actor() && Actor()->Parkour().Active(); }
    // The mod switches the native features on; without it the engine stays stock.
    void SetEnabled(bool enabled) { if (auto* actor = Actor()) actor->Parkour().SetEnabled(enabled); }
    bool Enabled() { return Actor() && Actor()->Parkour().Enabled(); }
    void SetEyeLevel(bool on) { if (auto* actor = Actor()) actor->Parkour().SetEyeLevel(on); }
    bool EyeLevel() { return Actor() && Actor()->Parkour().EyeLevel(); }
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
    // The share of the world's gravity the actor falls with (1: stock, the mod's
    // lighter falls below it). Jumps keep their height.
    bool AirGravity(float scale)
    {
        auto* character = ActorCharacter();
        if (!character) return false;
        character->SetAirGravity(scale);
        return true;
    }
    float AirGravityValue()
    {
        auto* character = ActorCharacter();
        return character ? character->AirGravity() : 1.f;
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
    void LegsSpeedSync(bool on) { if (g_actor) g_actor->SetLegsSpeedSync(on); }
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
        // Hands still in view are lowered out of sight before the clip stops.
        if (g_player_hud) g_player_hud->StopParkourMotion();
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
        result["caught"]=actor && actor->Parkour().Caught();
        if(actor)
        {
            const auto& velocity=actor->Parkour().Result().velocity;
            result["catch_vertical_speed"]=_max(0.f,-velocity.y);
            result["catch_horizontal_speed"]=sqrtf(velocity.x*velocity.x+velocity.z*velocity.z);
        }
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
        def("parkour_set_enabled", &SetEnabled), def("parkour_enabled", &Enabled),
        def("parkour_set_eye_level", &SetEyeLevel), def("parkour_eye_level", &EyeLevel),
        def("parkour_cancel", &Cancel), def("parkour_forward_held", &ForwardHeld), def("parkour_status", &Status), def("parkour_crouched", &Crouched),
        def("parkour_airborne", &Airborne), def("parkour_stop_hands", &StopHands),
        def("parkour_holster", &Holster), def("parkour_vertical_speed", &VerticalSpeed),
        def("parkour_begin", &Begin), def("parkour_perform", &Perform), def("parkour_hands_state", &HandsState), def("parkour_prepare_hands", &PrepareHands),
        def("actor_carry_capacity", &CarryCapacity), def("actor_movement_scale", &MovementScale),
        def("actor_legs_speed_sync", &LegsSpeedSync),
        def("actor_step_over", &StepOver), def("actor_step_over_enabled", &StepOverEnabled),
        def("actor_step_over_strength", &StepOverStrength), def("actor_step_over_strength_value", &StepOverStrengthValue),
        def("actor_step_over_speed", &StepOverSpeed),
        def("actor_air_gravity", &AirGravity), def("actor_air_gravity_value", &AirGravityValue),
        def("actor_step_over_height", &StepOverHeight), def("movement_terrain_tuning", &TerrainTuning)];
}
