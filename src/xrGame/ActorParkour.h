#pragma once

#include "../xrCDB/xrXRC.h"
#include "../Include/xrRender/animation_motion.h"

class CActor;
class CBlend;
class CPHMovementControl;
class CObject;
class CBoneInstance;
class IKinematics;
class IKinematicsAnimated;

// Actor-owned, main-thread traversal. Queries never change actor/physics state.
// Every travelled interval is swept, including intervals skipped by a slow frame.
class CActorParkour
{
public:
    struct Options
    {
        float reach = .30f, minHeight = .30f, maxHeight = 2.4f;
        float minMantleHeight = 0.f, minVaultHeight = 0.f, heightOrigin = 0.f;
        bool hasHeightOrigin = false, crouchAtTop = false;
        float topCrouchStart = .75f;
        float vaultHeight = 1.f, mantleHeight = 1.4f, maxDepth = 1.2f;
        float maxDrop = 2.2f, maxFall = 6.f, momentum = .35f;
        float horizontalSpeed = 2.f, verticalSpeed = 2.f;
        bool preferVault = false, allowClimbVault = true, onlyHolstered = false;
        bool allowClimb = true, allowMantle = true, allowVault = true;
        bool airborneTop = true, cameraGuide = true;
        bool forwardControl = false, forwardHeld = false;
        // crossTop: with Forward held a standable top continues across it and off the far side.
        // vaultSpeed: from this approach speed (m/s along the route) an obstacle that can be
        // vaulted is vaulted rather than mantled onto (0: only preferVault decides).
        bool crossTop = true; float vaultSpeed = 0.f;
        // A top shallower than this (m, along the approach) counts as too thin to stand on
        // when it can be vaulted: it is always vaulted (0: any top the feet fit on).
        float minTopDepth = 0.f;
        float cameraStrength = .65f;
        float armReachHeight=2.15f, reachTimeout=.45f, handSpacing=.48f, handDepth=.06f, handsToLedgeHeightOffset=.04f;
        float armExtension=.80f, minHandSpacing=.14f;
        float centerAssist=.24f, maxSlope=.95993109f, shoulderAdjustment=.25f;
        float cameraDeadzone = .52359878f; // 30 degrees around the captured view
        float freelookYaw = 1.04719755f, freelookPitch = .78539816f; // 60 / 45 degrees
        // On-screen detection box in projected screen units (1 = screen edge),
        // measured from the crosshair. Lua owns these gameplay values.
        float boxWidth = .45f, boxTop = .75f, boxBottom = .65f;
        float detectRange = 1.4f; // acquisition distance from the body center
        bool hasTarget = false;   // ledge acquired earlier in this attempt
        Fvector target = {0,0,0};
        float targetRadius = .30f;
        bool limitedFreelook = true;
        bool levelDownwardView = false;
        float cameraLevelSpeed = 0.f;
        float maxCatchSpeed = 5.5f; // faster falls only produce a failed reach
        float catchDropPerSpeed = 0.f, catchMaxDrop = 0.f, catchBrakeTime = .15f;
        float standWidth = .09f; // narrowest standable top, measured along/across the approach
        float wallStep = .35f; // extra height a foot push off the wall below gives a jump
        float heavyLoad = .5f; // carried / max weight above which vaults are the slow, two-handed kind
        bool kneeFloor = false; // nothing below the body's own knee height is traversed
    };
    struct Candidate
    {
        bool valid = false, airborne = false;
        bool tentative = false; // seen but beyond arm reach; route not validated yet
        LPCSTR action = "none";
        LPCSTR route = "none";
        Fvector origin = {0,0,0}, lip = {0,0,0}, landing = {0,0,0}, direction = {0,0,1}, velocity = {0,0,0};
        float rise = 0.f, obstacleHeight = 0.f;
        bool freeRelease = false;
        bool upright = false; // no room to lean the head over the edge (low lintel/ceiling)
        float topTime = 0.f;
        Fvector topLanding = {0,0,0};
        u32 topBox = 0;
        u32 box = 0;
        xr_vector<Fvector> path;
        xr_vector<float> distances;
        xr_vector<u32> pathBoxes; // stance for each interval ending at path[i]
    };

    explicit CActorParkour(CActor* actor);
    // Off until the mod's script switches it on: without the mod, the engine
    // behaves exactly as stock (no bone hooks, no per-frame work, no queries).
    void SetEnabled(bool enabled);
    bool Enabled() const { return m_enabled; }
    bool Query(const Options& options);
    bool Start(float seconds, bool hands);
    void Update(float dt);
    void SetForwardHeld(bool held) { if (!held) m_forwardHeld = false; }
    void Cancel(LPCSTR reason = "cancelled");
    void Reset(); // net destroy/death: no stance expansion against a dying controller
    void UpdateStance();
    void UpdateCamera(float dt);
    bool Airborne() const;
    bool Active() const { return m_active; }
    bool Reaching() const { return m_reaching; }
    bool OwnsMovement() const { return m_active && !m_missing; }
    void ConfirmHandContact(bool ready);
    bool Crouched() const;
    bool HandsEnabled() const { return m_hands; }
    // Every hand the move uses ([0] left, [1] right in the mask) has a contact.
    bool HandContactsReady() const
    { return m_handMask && (!(m_handMask & 1) || m_contactValid[0]) && (!(m_handMask & 2) || m_contactValid[1]); }
    float HandWeight() const;
    float HandRelease() const; // 0 while supporting, 1 once both hands pushed off
    float HandRelease(int side) const; // one hand: [0] left, [1] right (lets go first)
    float ShoulderAdjustment() const { return m_options.shoulderAdjustment; }
    bool Caught() const { return m_active && !m_missing && m_handConfirmed; } // hands hold the edge
    bool HandTarget(bool left, Fvector& position, Fvector& normal) const;
    const Fvector& HandDirection(bool left) const { return m_handDirection[left ? 0 : 1]; }
    float Progress() const;
    float Duration() const { return m_duration; }
    LPCSTR Reason() const { return m_reason; }
    const Candidate& Result() const { return m_candidate; }
    bool HandsEmpty() const;

    // Third-person body: the world model's arms hold the same contacts as the
    // HUD hands and the legs play a cycle matching the phase of the move.
    enum BodyPose { bodyNone, bodyHang, bodyStep, bodyTuck, bodyWalk, bodyHop, bodyStepUp };
    BodyPose Pose() const;
    float Lean() const { return m_bodyLean; } // forward torso bend, radians
    float Grip() const { return _max(m_bodyArms[0].weight, m_bodyArms[1].weight); } // 0 free .. 1 holding
    // The move poses the body (arms, legs): the stock foot IK must not shift it.
    bool PosesBody() const
    { return m_enabled && (OwnsMovement() || Grip() > 0.f || m_bodyLegs[0].weight > 0.f || m_bodyLegs[1].weight > 0.f || ClipWeight() > 0.f); }
    static constexpr float LeanPivot = .65f; // lower spine to eyes: the bend moves head and shoulders
    static constexpr float HangLean = .2f;   // chest in to the wall while hanging
    static constexpr float PressLean = .65f; // head and shoulders over the hands, pressing up
    static constexpr float ShoulderPivot = .5f; // lower spine to shoulders
    static constexpr float HeadRadius = .12f;
    static constexpr float OverheadReach = .3f; // past the body's radius, how far behind an overhead edge is still grabbed
    void AttachBody(IKinematics* model);
    void DetachBody(IKinematics* model);
    void UpdateBody(float dt);
    // An authored clip (shipped by the traversal mod) plays the whole body for
    // the move; the stock torso/legs selection and look callbacks stand down. When
    // the move ends the stock cycles mix back in while the clip fades out.
    bool ClipPlaying() const { return m_clipPlaying && !m_clipFading; }
    float ClipWeight() const { return m_clipPlaying ? m_clipWeight : 0.f; } // the clip's share of the body's mix
    // A landing (roll, hard landing) is still on the body, until it has faded out.
    bool PerformedClip() const { return m_clipPerformed && ClipWeight() > 0.f; }
    // World position between the body's eyes while a clip plays: the first-person
    // camera rides the clip's head, so both views are one body.
    bool ClipEye(Fvector& eye) const;
    // A clip authored against its own edge: `face` metres ahead, `height` up. 30 fps:
    // hands land at `contact`, push off at `release`. Mirrored copies end in "_m".
    struct ClipDef
    {
        enum Kind { step, hop, mantle, climb, vault, over, window };
        enum Start { ground, air, any };
        enum Style { anyStyle, speed, steps, oneHand };
        LPCSTR name; u8 kind, start; float height, face; u8 contact, release, frames; bool mirror, wall; u8 style;
        // Scripted clips only. lead: frames skipped at the start (a body already moving is
        // past the clip's set-off). finish: the frame the move is done (0: the last); the rest
        // plays on while the clip mixes out. land: the feet's height at the clip's end (m above
        // its start); ahead: how far forward a planted end leaves the actor (<0: in the air).
        // With fit the clip eases, from the hands' release, so that its feet end where the
        // guided move leaves the actor.
        u8 lead = 0, finish = 0; float land = 0.f, ahead = -1.f; bool fit = false;
    };
    // The next Start() plays this clip (empty name: no clip) and uses only the hands
    // in `handMask` (1 left, 2 right) instead of choosing both from the built-in table.
    // A script decides the presentation; it applies to that one start only.
    // blendIn/blendOut: seconds to mix the clip in from, and back out to, the stock animation.
    void Present(const ClipDef& clip, u8 handMask, float blendIn = 1.f/6.f, float blendOut = .25f);
    u8 HandMask() const { return m_handMask; }
    // A body clip played on its own, outside the routes (a landing roll, a hard landing).
    // The clip stays at the actor, which physics keeps on the ground and out of walls while
    // the move sets its horizontal speed: `distance` m forward over the clip, eased in and
    // out. camera: 1 the eyes' height only (no tumbling view), 2 the eyes and the head's
    // forward roll (the view turns over with the body).
    bool Perform(float seconds, const ClipDef& clip, float distance, u8 camera, float blendIn, float blendOut);
    bool Performing() const { return m_active && m_perform; }
    // The next move keeps a weapon ready: the normal view (the weapon follows it, within
    // the look limits) instead of a free look that leaves the weapon behind.
    void SetAimReady(bool on) { m_aimReady = on; }
    bool AimReady() const { return m_aimReady; }
    // The input a performed move walks the body with (world space, CActor::g_Physics).
    const Fvector& PerformAccel() const { return m_performAccel; }
    u8 ClipCamera() const { return m_clipPlaying ? m_clipCamera : 0; }
    float ClipPitch() const { return m_clipPitch; } // the head's forward roll since the clip began (rad)
    // The first-person camera after its push off nearby walls. In a clip the head is
    // often right at a wall, where the push flickers on and off from frame to frame;
    // it is applied at once and eased off over a quarter second instead.
    Fvector SteadyCameraPush(const Fvector& free, const Fvector& pushed, float dt);
    bool CandidateWallBelow() { return m_candidate.valid && WallBelow(m_candidate); }
    // The edge is just out of reach: the move starts with a foot kicking off the wall
    // below, which lifts the body into the catch (see WallKick).
    bool CandidateWallKick() const { return m_candidate.valid && ReachSlack(m_candidate) < 0.f; }
    // Eye level: the camera at the body model's own eye height instead of the
    // configured fraction of the collision box (only while the mod is enabled).
    void SetEyeLevel(bool on) { m_eyeLevel = on; }
    bool EyeLevel() const { return m_eyeLevel; }
    float CameraFactor(float stock) const;

private:
    CPHMovementControl* Movement() const;
    void ApplyStance(u32 box);
    bool Available();
    bool Ray(const Fvector& start, const Fvector& dir, float range, Fvector& hit, Fvector& normal);
    bool Sweep(const Fvector& from, const Fvector& to, u32 box, float extraHorizontalSkin = 0.f);
    bool Supported(const Fvector& feet, float radius, const Fvector& forward);
    bool Build(Candidate& c, const Fvector& landing, u32 box, bool* blockedApproach = nullptr, u32 initialBox = u32(-1));
    bool FindLanding(Candidate& c);
    void SmoothPath(Candidate& c);
    void TravelRates(const Candidate& c, float& horizontalRate, float& verticalRate) const;
    bool ContactReady(const Candidate& c);
    void Shoulder(Fvector& shoulder, float& arm) const;
    float ReachSlack(const Candidate& c) const;
    float ReachCeiling(bool airborne) const;
    bool WallBelow(const Candidate& c);
    float WallKick(const Candidate& c);
    void HookBody(bool hook);
    bool SphereClear(const Fvector& center, float radius);
    float PostureLean(const Candidate& c, float below, bool grip) const;
    float ShoulderAt(u32 box) const;
    Fvector GripPoint(int side) const;
    void FitBody(Candidate& c);
    void PlanGrip();
    void UpdateLegs(float dt);
    static void _BCL LegCallback(CBoneInstance* bone);
    static void _BCL RootCallback(CBoneInstance* bone);
    void SelectClip(bool airborne);
    void PrepareClip();
    float ClipTime() const;
    void ClipFrames(float& from, float& contact, float& end) const;
    float ClipMix(IKinematicsAnimated* animated) const;
    void UpdateClip(float dt);
    void ClipAdjust(Fmatrix& adjust) const;
    void StopClip();
public:
    bool StepUp(const Candidate& c) const; // low enough to step onto: feet, no hands
private:
    static void _BCL BodyCallback(CBoneInstance* bone);
    float ViewScore(const Fvector& point) const;
    Fvector BodyForward() const;
    void CaptureHands();
    void BeginCamera();
    void EndCamera();
    Fvector AtDistance(float distance) const;
    float Radius(u32 box) const;
    bool ScreenPoint(const Fvector& point, float& x, float& y) const;
    bool InViewWindow(const Fvector& point) const;

    CActor* m_actor;
    Options m_options;
    Candidate m_candidate;
    xrXRC m_collider;
    xr_vector<CObject*> m_nearby;
    bool m_active = false, m_hands = false, m_restorePending = false;
    bool m_forwardHeld = false;
    bool m_reaching = false, m_handConfirmed = false, m_missing = false;
    float m_reachTime = 0.f;
    float m_catchSpeed = 0.f, m_catchDrop = 0.f;
    Fvector m_catchOrigin;
    BOOL m_savedGravity = TRUE;
    bool m_contactValid[2] = {false, false};
    Fvector m_contact[2], m_normal[2], m_handDirection[2], m_lastPosition;
    u32 m_savedBox = 0, m_queryFrame = 0;
    float m_elapsed = 0.f, m_duration = 1.f;
    float m_contactStart = .06f, m_contactFull = .24f, m_contactRelease = .78f, m_contactEnd = .94f;
    LPCSTR m_reason = "idle";
    bool m_cameraOwned = false;
    u8 m_savedFreelook = 0;
    bool m_aimReady = false;
    Fvector m_savedYawLimits, m_savedPitchLimits;
    float m_lockedYaw = 0.f, m_guidePitch = 0.f, m_savedTorsoYaw = 0.f;

    struct BodyBone { CActorParkour* owner; int side, part; };
    struct BodyArm
    {
        u16 bones[3] = {u16(-1), u16(-1), u16(-1)}; // upper arm, forearm, hand
        Fvector axis[3]; // child offset in each bone's own frame (elbow, wrist, fingers)
        Fvector thumb;   // hand -> thumb, in the hand's frame
        float upper = 0.f, lower = 0.f;
        bool hand = false;
        float weight = 0.f;
        Fvector target, fingers, inward; // world space, converted when the bones are built
    };
    bool m_enabled = false;
    IKinematics* m_body = nullptr;
    BodyBone m_bodyBones[2][3];
    BodyArm m_bodyArms[2]; // [0] left, [1] right, like m_contact
    struct BodyLeg
    {
        u16 bones[2] = {u16(-1), u16(-1)}; // thigh, calf
        Fvector axis[2];  // knee and ankle offsets in each bone's own frame
        float upper = 0.f, lower = 0.f;
        float weight = 0.f;
        bool planted = false, done = false; // done: this foot has pushed off for this move
        bool onTop = false; // the current foothold is on the top surface
        bool kneel = false; // the knee, not the foot, rests on the edge
        Fvector knee;       // world space: where the knee rests while kneeling
        float swing = 1.f; // 0..1 moving from the old foothold to the new one
        Fvector from, hold, at; // world space: previous foothold, current foothold, ankle now
    };
    BodyBone m_legBones[2][2];
    BodyLeg m_bodyLegs[2];
    bool m_wallBelow = false;
    float m_bodyLean = 0.f;
    // The character model's own arm, measured on its skeleton, so the reach the
    // controller allows is exactly what the visible arms (and so both views) reach.
    float m_bodyArmLength = 0.f, m_bodyShoulderHeight = 0.f;
    float m_bodyHipHeight = 0.f, m_bodyShoulderWidth = 0.f, m_bodyKneeHeight = 0.f;
    float m_bodyForward = 1.f; // model-space Z sign the skeleton faces (from foot to toe)
    float m_bodyEyeHeight = 0.f; // standing eye height of the model (0: not measured)
    u16 m_eyeBones[2] = {u16(-1), u16(-1)};
    bool m_eyeLevel = false;
    Fvector m_startFeet = {0.f, 0.f, 0.f};

    const ClipDef* m_clip = nullptr;
    ClipDef m_presented = {};      // the script's clip, valid for the next Start()
    ClipDef m_playing = {};        // the script's clip in play
    shared_str m_playingName;
    shared_str m_presentedName;
    bool m_presentedValid = false;
    u8 m_presentedMask = 3, m_handMask = 3;
    float m_presentedIn = 6.f, m_presentedOut = 4.f; // mixing rates (1/seconds)
    float m_blendIn = 6.f, m_blendOut = 4.f;
    bool m_clipMirror = false, m_clipPlaying = false, m_clipFading = false;
    u32 m_clipCount = 0;
    float m_clipReach = 0.f, m_clipTime = 0.f, m_clipWeight = 0.f;
    Fmatrix m_clipOrigin;     // world: feet at the clip's first frame, k facing the edge
    Fvector m_clipShift;      // edge offset from the authored one: x right, y up, z forward
    Fmatrix m_clipWorld;      // world: the clip's root this frame (held while fading out)
    float m_clipRate = 1.f;   // clip seconds per second while it plays out its settle
    bool m_clipPlayOut = false; // the move finished: the settle plays while mixing out
    Fvector m_fitLanding = {0.f, 0.f, 0.f}; // where the move puts the feet (clip fit)
    mutable Fvector m_eyeSmooth = {0.f, 0.f, 0.f};
    mutable u32 m_eyeFrame = 0; mutable bool m_eyeSmoothValid = false;
    mutable float m_clipPitch = 0.f, m_pitchRaw = 0.f; mutable bool m_pitchValid = false;
    u16 m_neckBone = u16(-1), m_headBone = u16(-1);
    bool m_perform = false, m_clipPerformed = false;
    u8 m_clipCamera = 0;          // 0 route clip; 1/2 a performed clip's camera
    float m_performDistance = 0.f;
    Fvector m_performDir = {0.f, 0.f, 1.f};
    Fvector m_performAccel = {0.f, 0.f, 0.f};
    Fvector m_cameraPush = {0.f, 0.f, 0.f};
    MotionID m_clipMotion;
    CBlend* m_clipBlends[4] = {nullptr, nullptr, nullptr, nullptr};
    u16 m_rootBone = u16(-1);

    // Routes that failed recently, so standing in a tight spot does not rerun the
    // full body-sweep validation for the same edge every frame.
    struct FailedRoute { Fvector lip, feet; u32 time = 0; };
    FailedRoute m_failed[8];
    u32 m_failedNext = 0;
};
