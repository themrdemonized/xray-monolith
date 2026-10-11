#pragma once

#include "../xrCDB/xrXRC.h"

class CActor;
class CPHMovementControl;
class CObject;

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
        float cameraStrength = .65f;
        float armReachHeight=2.15f, reachTimeout=.45f, handSpacing=.48f, handDepth=.06f, handsToLedgeHeightOffset=.04f;
        float armExtension=.80f, minHandSpacing=.14f;
        float centerAssist=.24f, maxSlope=.95993109f, shoulderAdjustment=.25f;
        float cameraDeadzone = .52359878f; // 30 degrees around the captured view
        float freelookYaw = 1.04719755f, freelookPitch = .78539816f; // 60 / 45 degrees
        // Camera-space acceptance window. Lua owns these gameplay values.
        float viewHorizontal = .31415927f, viewUp = .90757121f, viewDown = .24434610f;
        bool limitedFreelook = true;
        float grabHorizontal = 0.f, grabVertical = 0.f;
        bool levelDownwardView = false;
        float cameraLevelSpeed = 0.f;
        float catchDropPerSpeed = 0.f, catchMaxDrop = 0.f, catchBrakeTime = .15f;
    };
    struct Candidate
    {
        bool valid = false, airborne = false;
        LPCSTR action = "none";
        LPCSTR route = "none";
        Fvector origin = {0,0,0}, lip = {0,0,0}, landing = {0,0,0}, direction = {0,0,1}, velocity = {0,0,0};
        float rise = 0.f, obstacleHeight = 0.f;
        bool freeRelease = false;
        float topTime = 0.f;
        Fvector topLanding = {0,0,0};
        u32 topBox = 0;
        u32 box = 0;
        xr_vector<Fvector> path;
        xr_vector<float> distances;
        xr_vector<u32> pathBoxes; // stance for each interval ending at path[i]
    };

    explicit CActorParkour(CActor* actor);
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
    bool OwnsMovement() const { return m_active && (!m_reaching || m_handConfirmed); }
    void ConfirmHandContact(bool ready, float slack = 0.f);
    bool Crouched() const;
    bool HandsEnabled() const { return m_hands; }
    bool HandContactsReady() const { return m_contactValid[0] && m_contactValid[1]; }
    float HandWeight() const;
    float ShoulderAdjustment() const { return m_options.shoulderAdjustment; }
    bool HandTarget(bool left, Fvector& position, Fvector& normal) const;
    const Fvector& HandDirection(bool left) const { return m_handDirection[left ? 0 : 1]; }
    float Progress() const;
    float Duration() const { return m_duration; }
    LPCSTR Reason() const { return m_reason; }
    const Candidate& Result() const { return m_candidate; }
    bool HandsEmpty() const;

private:
    CPHMovementControl* Movement() const;
    void ApplyStance(u32 box);
    bool Available();
    bool Ray(const Fvector& start, const Fvector& dir, float range, Fvector& hit, Fvector& normal);
    bool Sweep(const Fvector& from, const Fvector& to, u32 box, float extraHorizontalSkin = 0.f);
    bool Supported(const Fvector& feet, float radius);
    bool Build(Candidate& c, const Fvector& landing, u32 box, bool* blockedApproach = nullptr, u32 initialBox = u32(-1));
    bool FindLanding(Candidate& c);
    void SmoothPath(Candidate& c);
    void TravelRates(const Candidate& c, float& horizontalRate, float& verticalRate) const;
    bool ContactReady(const Candidate& c) const;
    float ViewScore(const Fvector& point) const;
    Fvector BodyForward() const;
    void CaptureHands();
    void BeginCamera();
    void EndCamera();
    Fvector AtDistance(float distance) const;
    float Radius(u32 box) const;
    bool InViewWindow(const Fvector& point) const;
    bool InGrabWindow(const Fvector& point) const;

    CActor* m_actor;
    Options m_options;
    Candidate m_candidate;
    xrXRC m_collider;
    xr_vector<CObject*> m_nearby;
    bool m_active = false, m_hands = false, m_restorePending = false;
    bool m_forwardHeld = false;
    bool m_reaching = false, m_handConfirmed = false;
    float m_reachTime = 0.f;
    float m_catchSlack = 0.f, m_catchSpeed = 0.f, m_catchElapsed = 0.f, m_catchDrop = 0.f;
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
    Fvector m_savedYawLimits, m_savedPitchLimits;
    float m_lockedYaw = 0.f, m_guidePitch = 0.f, m_savedTorsoYaw = 0.f;
    float m_guideDirection = 0.f;
};
