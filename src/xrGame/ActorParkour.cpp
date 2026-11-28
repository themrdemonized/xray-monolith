#include "stdafx.h"
#include "ActorParkour.h"
#include "Actor.h"
#include "CharacterPhysicsSupport.h"
#include "PHMovementControl.h"
#include "../xrPhysics/PHActorCharacter.h"
#include "Level.h"
#include "inventory.h"
#include "customdevice.h"
#include "../xrEngine/gamemtllib.h"
#include "../xrEngine/xr_collide_form.h"
#include "../xrEngine/CameraBase.h"
#include "UIGameCustom.h"
#include "ui/UIDialogWnd.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "../Include/xrRender/animation_blend.h"
#include "../xrEngine/SkeletonMotionDefs.h"
#include "../xrPhysics/IPHWorld.h"
#include "Weapon.h"
#include "player_hud.h"

// Built-in clip choice for scripts that do not present their own (Ledge Grabbing
// Ext: meshes\actors\modded_stalker_animations\lge_parkour_body.omf, tools\xray_anim_mcp).
// A clip's origin is the feet at its first frame, facing the edge it was made for.
namespace
{
    using Clip = CActorParkour::ClipDef;
    const Clip g_clips[] = {
        {"lge_step_up_050",        Clip::step,   Clip::ground, .50f, .35f,  0,  0, 24, true,  true,  Clip::anyStyle},
        {"lge_hop_050",            Clip::hop,    Clip::ground, .50f, .40f,  0,  0, 28, true,  true,  Clip::anyStyle},
        {"lge_land_air_040",       Clip::step,   Clip::air,    .40f, .35f,  0,  0, 18, true,  true,  Clip::anyStyle},
        {"lge_hop_air_040",        Clip::hop,    Clip::air,    .40f, .40f,  0,  0, 22, true,  true,  Clip::anyStyle},
        {"lge_mantle_air_070",     Clip::mantle, Clip::air,    .70f, .45f,  3,  8, 30, true,  true,  Clip::anyStyle},
        {"lge_mantle_080",         Clip::mantle, Clip::ground, .80f, .45f,  9, 14, 36, true,  true,  Clip::anyStyle},
        {"lge_mantle_110",         Clip::mantle, Clip::ground, 1.1f, .50f, 12, 20, 50, true,  true,  Clip::anyStyle},
        {"lge_mantle_140",         Clip::mantle, Clip::ground, 1.4f, .50f, 12, 23, 53, true,  true,  Clip::anyStyle},
        {"lge_mantle_air_110",     Clip::mantle, Clip::air,    1.1f, .50f,  4, 10, 40, true,  true,  Clip::anyStyle},
        {"lge_mantle_air_140",     Clip::mantle, Clip::air,    1.4f, .50f,  4, 10, 40, true,  true,  Clip::anyStyle},
        {"lge_climb_180",          Clip::climb,  Clip::ground, 1.8f, .40f, 13, 25, 55, true,  true,  Clip::anyStyle},
        {"lge_climb_220",          Clip::climb,  Clip::ground, 2.2f, .40f, 13, 25, 55, true,  true,  Clip::anyStyle},
        {"lge_climb_air_200",      Clip::climb,  Clip::air,    2.0f, .40f,  4, 17, 47, true,  true,  Clip::anyStyle},
        {"lge_climb_air_nowall",   Clip::climb,  Clip::air,    2.0f, .40f,  4, 17, 47, true,  false, Clip::anyStyle},
        {"lge_vault_speed_080",    Clip::vault,  Clip::any,    .80f, .45f,  7, 11, 20, true,  true,  Clip::speed},
        {"lge_vault_step_080",     Clip::vault,  Clip::any,    .80f, .45f,  9, 12, 28, true,  true,  Clip::steps},
        {"lge_vault_step_110",     Clip::vault,  Clip::any,    1.1f, .45f,  9, 12, 28, true,  true,  Clip::steps},
        {"lge_vault_onehand_080",  Clip::vault,  Clip::any,    .80f, .45f,  9, 13, 28, false, true,  Clip::oneHand},
        {"lge_vault_onehand_110",  Clip::vault,  Clip::any,    1.1f, .45f,  9, 13, 28, false, true,  Clip::oneHand},
        {"lge_climb_over_180",     Clip::over,   Clip::ground, 1.8f, .40f, 13, 28, 41, true,  true,  Clip::anyStyle},
        {"lge_climb_over_air_200", Clip::over,   Clip::air,    2.0f, .40f,  4, 20, 33, true,  true,  Clip::anyStyle},
        {"lge_window_090",         Clip::window, Clip::ground, .90f, .45f, 10, 16, 29, true,  true,  Clip::anyStyle},
        {"lge_window_130",         Clip::window, Clip::ground, 1.3f, .45f, 10, 17, 30, true,  true,  Clip::anyStyle},
        {"lge_window_air_130",     Clip::window, Clip::air,    1.3f, .45f,  4, 13, 26, true,  true,  Clip::anyStyle},
    };
    constexpr float ClipFps = 30.f;
}

namespace
{
    constexpr float skin = .015f;
    constexpr float step = .06f;
    bool Blocks(const CDB::TRI& triangle)
    {
        const SGameMtl* material = GMLib.GetMaterialByIdx(triangle.material);
        // Preserve vanilla ladder/passable filtering, but do not hide a climbable
        // material that is explicitly an actor obstacle (some fences use that mix).
        if (!material) return true;
        const bool obstacle = material->Flags.test(SGameMtl::flActorObstacle);
        const bool passable = material->Flags.test(SGameMtl::flPassable);
        const bool climbable = material->Flags.test(SGameMtl::flClimable);
        return (!passable || obstacle) && (!climbable || obstacle);
    }
    float Smooth(float u) { return u * u * (3.f - 2.f * u); }
    float Guided(float u)
    {
        // Keep some linear component so contact does not create a visible pause/
        // hover before the pull starts; the smooth component still removes harsh
        // acceleration changes.
        return .35f * u + .65f * Smooth(u);
    }
    bool ValidBox(const Fbox& b)
    {
        return _valid(b.min) && _valid(b.max) && b.max.y - b.min.y > .3f &&
            b.max.x > b.min.x && b.max.z > b.min.z;
    }
    // Clip a broad-phase triangle against a conservative swept cylinder prism.
    // Eight horizontal planes enclose the round/elliptical body without the
    // large square corners that rejected close diagonal approaches to roofs.
    struct BodyPlanes
    {
        Fvector normals[26];
        float limits[26];
        BodyPlanes(const Fvector& from, const Fvector& to, const Fvector& forward,
            const Fvector& right, float frontRadius, float sideRadius, float height)
        {
            const float directions[8][2]={{1,0},{-1,0},{0,1},{0,-1},
                {.70710678f,.70710678f},{-.70710678f,.70710678f},
                {.70710678f,-.70710678f},{-.70710678f,-.70710678f}};
            // Additional lower bevel planes enclose the rounded foot sphere. A
            // flat cylinder bottom falsely collides with every inclined landing.
            for(int plane=0;plane<26;++plane)
            {
                Fvector& normal = normals[plane]; float& limit = limits[plane];
                if(plane<8)
                {
                    const float x=directions[plane][0], z=directions[plane][1];
                    normal=right;normal.mul(x);normal.mad(forward,z);
                    limit=_max(from.dotproduct(normal),to.dotproduct(normal))+
                        sqrtf(_sqr(sideRadius*x)+_sqr(frontRadius*z));
                }
                else if(plane<10)
                {
                    normal.set(0.f,plane==8?1.f:-1.f,0.f);
                    limit=plane==8?_max(from.y,to.y)+height-skin:-_min(from.y,to.y)-skin;
                }
                else
                {
                    const int index=(plane-10)%8;
                    const float slope=plane<18?1.f:2.41421356f;
                    const float x=directions[index][0], z=directions[index][1];
                    const float radius=sqrtf(_sqr(sideRadius*x)+_sqr(frontRadius*z));
                    normal=right;normal.mul(x);normal.mad(forward,z);normal.y=-slope;
                    limit=_max(from.dotproduct(normal),to.dotproduct(normal))+
                        radius*(sqrtf(1.f+slope*slope)-slope);
                }
            }
        }
    };
    bool IntersectsBody(const Fvector* vertices, const CDB::TRI& triangle, const BodyPlanes& body)
    {
        Fvector polygon[32], clipped[32];
        int count=3;
        for(int i=0;i<3;++i) polygon[i]=vertices[triangle.verts[i]];
        for(int plane=0;plane<26 && count;++plane)
        {
            const Fvector& normal = body.normals[plane];
            const float limit = body.limits[plane];
            int size=0;
            Fvector previous=polygon[count-1];
            float previousDistance=previous.dotproduct(normal)-limit;
            for(int i=0;i<count;++i)
            {
                const Fvector current=polygon[i];
                const float distance=current.dotproduct(normal)-limit;
                if((distance<=0.f)!=(previousDistance<=0.f))
                    clipped[size++].lerp(previous,current,previousDistance/(previousDistance-distance));
                if(distance<=0.f) clipped[size++]=current;
                previous=current;previousDistance=distance;
            }
            count=size;
            for(int i=0;i<count;++i) polygon[i]=clipped[i];
        }
        return count!=0;
    }
}

// Debug-log diagnostics (MCM debug tips). File-local so the shared Actor.h
// layout is unchanged; there is a single controlled actor.
struct ParkourLandingDiag
{
    u16 samples=0, notWalkable=0, depth=0, permission=0, minHeight=0, unsupported=0, wideBody=0;
    u16 column=0, approach=0, path=0, needTop=0;
};
static struct
{
    bool enabled=false;
    ParkourLandingDiag landing, rejected;
    Fvector rejectedLip={0,0,0};
    float rejectedRise=0.f, startRise=0.f, startFeetY=0.f;
    float reachError=-1.f, reachBest=-1.f, reachExcess=-1.f;
    u32 reachFrames=0, startTime=0;
} g_parkourDiag;

void ParkourSetDebugLog(bool enabled) { g_parkourDiag.enabled = enabled; }

// Worst-arm wrist error and shoulder-to-target excess over arm length (HUD units).
void ParkourReportReach(float error, float excess)
{
    auto& d = g_parkourDiag;
    d.reachError = error; d.reachExcess = excess; ++d.reachFrames;
    if (d.reachBest < 0.f || error < d.reachBest) d.reachBest = error;
}

void ParkourLogDiagnostics(const CActorParkour& parkour, LPCSTR stage)
{
    CActor* actor = Actor();
    if (!g_parkourDiag.enabled || !actor) return;
    // Polling repeats the same result every frame; log writes would stall frames.
    static shared_str lastStage, lastReason;
    static u32 lastTime = 0;
    if (xr_strcmp(stage,"end") && lastStage == stage && lastReason == parkour.Reason() &&
        Device.dwTimeGlobal - lastTime < 1000) return;
    lastStage = stage; lastReason = parkour.Reason(); lastTime = Device.dwTimeGlobal;
    const auto& g = g_parkourDiag;
    const Fvector feet = actor->Position();
    auto* support = actor->character_physics_support();
    const Fvector velocity = support && support->movement() ? support->movement()->GetVelocity() : Fvector().set(0.f,0.f,0.f);
    const auto& c = parkour.Result();
    const bool query = !xr_strcmp(stage,"query");
    // A failed query only has a lip when some candidate reached FindLanding.
    const bool hasLip = !query || g.rejectedRise != 0.f;
    const Fvector lip = query ? (hasLip ? g.rejectedLip : feet) : c.lip;
    const ParkourLandingDiag& d = query ? g.rejected : g.landing;
    Fvector horizontal; horizontal.sub(lip,feet); horizontal.y = 0.f;
    float lipEye = 0.f, viewUp = 0.f;
    if (auto* camera = hasLip ? actor->cam_FirstEye() : nullptr)
    {
        Fvector to; to.sub(lip,camera->vPosition);
        lipEye = to.y;
        viewUp = rad2deg(atan2f(to.dotproduct(camera->Up()),_max(EPS,to.dotproduct(camera->vDirection))));
    }
    const bool active = parkour.Active();
    Msg("[Parkour] %s reason=%s action=%s route=%s rise=%.2f startRise=%.2f rejRise=%.2f dFeet=%.2f "
        "lipDist=%.2f lipUp=%.2f lipEye=%.2f viewUp=%.0f air=%d vy=%.2f toward=%.2f "
        "reach=%d/%d t=%.2f frames=%u err=%.3f best=%.3f excess=%.3f prog=%.2f",
        stage, parkour.Reason(), c.action, c.route, c.rise, query ? 0.f : g.startRise, g.rejectedRise,
        active ? feet.y - g.startFeetY : 0.f,
        horizontal.magnitude(), lip.y - feet.y, lipEye, viewUp,
        parkour.Airborne() ? 1 : 0, velocity.y, velocity.dotproduct(c.direction),
        parkour.Reaching() ? 1 : 0, parkour.Reaching() && parkour.OwnsMovement() ? 1 : 0,
        active ? float(Device.dwTimeGlobal - g.startTime) * .001f : 0.f, query ? 0u : g.reachFrames,
        query ? -1.f : g.reachError, query ? -1.f : g.reachBest,
        query ? -1.f : g.reachExcess, query ? 0.f : parkour.Progress());
    Msg("[Parkour] %s landing samples=%u notWalkable=%u depth=%u permission=%u minHeight=%u unsupported=%u "
        "wideBody=%u column=%u approach=%u path=%u needTop=%u",
        stage, d.samples, d.notWalkable, d.depth, d.permission, d.minHeight, d.unsupported,
        d.wideBody, d.column, d.approach, d.path, d.needTop);
}

CActorParkour::CActorParkour(CActor* actor) : m_actor(actor) {}

CPHMovementControl* CActorParkour::Movement() const
{
    return m_actor->character_physics_support() ? m_actor->character_physics_support()->movement() : nullptr;
}

bool CActorParkour::Crouched() const
{
    return m_active ? OwnsMovement() && Movement() && Movement()->BoxID()!=0 : m_restorePending;
}

void CActorParkour::ApplyStance(u32 box)
{
    if(Movement()->BoxID()!=box) Movement()->ActivateBox(box,TRUE);
    if(box)
    {
        m_actor->mstate_real |= mcCrouch;
        if(box==1) m_actor->mstate_real &= ~mcAccel;
        else m_actor->mstate_real |= mcAccel;
    }
    else m_actor->mstate_real &= ~mcCrouch;
}

bool CActorParkour::HandsEmpty() const
{
    if (m_actor->inventory().ActiveItem()) return false;
    auto* device = smart_cast<CCustomDevice*>(m_actor->inventory().ItemFromSlot(DETECTOR_SLOT));
    return !device || device->IsHidden();
}

bool CActorParkour::Available()
{
    if (!m_actor->g_Alive() || !Movement() || !Movement()->CharacterExist()) m_reason = "no_actor";
    else if (m_actor != Level().CurrentControlEntity() || m_actor->Holder() || m_actor->IsTalking()) m_reason = "unavailable";
    else if (CurrentGameUI() && CurrentGameUI()->TopInputReceiver() && CurrentGameUI()->TopInputReceiver()->StopAnyMove()) m_reason = "menu";
    else if ((m_actor->cam_active != eacFirstEye && m_actor->cam_active != eacLookAt) ||
        (m_actor->MovingState() & mcClimb)) m_reason = "ladder_or_camera";
    else if (m_options.onlyHolstered && !HandsEmpty()) m_reason = "hands_occupied";
    else return true;
    return false;
}

float CActorParkour::Radius(u32 box) const
{
    const Fbox& b = Movement()->Boxes()[box];
    // CPHSimpleCharacter::SetBox uses the smaller horizontal dimension.
    return .5f * _min(b.max.x - b.min.x, b.max.z - b.min.z);
}

Fvector CActorParkour::BodyForward() const
{
    Fvector forward = m_actor->XFORM().k;
    forward.y = 0.f;
    if (forward.square_magnitude() < EPS_S && m_actor->cameras[eacFirstEye])
    {
        forward = m_actor->cameras[eacFirstEye]->vDirection;
        forward.y = 0.f;
    }
    forward.normalize_safe();
    return forward;
}

bool CActorParkour::ScreenPoint(const Fvector& point, float& x, float& y) const
{
    // The camera actually viewed (first or third person), so the box is on screen.
    auto* camera = m_actor->cam_Active();
    if (!camera) return false;
    Fvector to; to.sub(point, camera->vPosition);
    Fvector forward = camera->vDirection; forward.normalize_safe();
    Fvector right = camera->Right(); right.normalize_safe();
    Fvector up = camera->Up(); up.normalize_safe();
    const float z = to.dotproduct(forward);
    if (z <= .05f) return false;
    // The current world projection, so FOV, aspect and zoom match what is seen:
    // -1..1 is the visible screen on both axes.
    const float sx = _abs(Device.mProject._11) > EPS ? Device.mProject._11 : 1.f;
    const float sy = _abs(Device.mProject._22) > EPS ? Device.mProject._22 : 1.f;
    x = to.dotproduct(right) * sx / z;
    y = to.dotproduct(up) * sy / z;
    return _valid(x) && _valid(y);
}

float CActorParkour::ViewScore(const Fvector& point) const
{
    float x, y;
    if (!ScreenPoint(point, x, y)) return flt_max;
    const float vertical = y >= 0.f ? y / _max(.05f, m_options.boxTop) : -y / _max(.05f, m_options.boxBottom);
    return 2.f * _abs(x) / _max(.05f, m_options.boxWidth) + vertical;
}

bool CActorParkour::InViewWindow(const Fvector& point) const
{
    // On-screen detection box: above center for high climbs/climb-overs, the
    // center band and below it for mantles and vaults.
    float x, y;
    return ScreenPoint(point, x, y) && _abs(x) <= m_options.boxWidth &&
        y <= m_options.boxTop && y >= -m_options.boxBottom;
}

bool CActorParkour::Ray(const Fvector& start, const Fvector& dir, float range, Fvector& hit, Fvector& normal)
{
    if (range <= EPS) return false;
    // Two-sided and material-filtered. No fixed skip count: a stack of passable
    // triangles cannot hide a solid wall. The controller owns its query buffer.
    m_collider.ray_options(0);
    m_collider.ray_query(Level().ObjectSpace.GetStaticModel(), start, dir, range);
    const auto* triangles = Level().ObjectSpace.GetStaticTris();
    const auto* vertices = Level().ObjectSpace.GetStaticVerts();
    float nearest = range + 1.f;
    for (auto* r = m_collider.r_begin(); r != m_collider.r_end(); ++r)
    {
        if (r->range < nearest && Blocks(triangles[r->id]))
        {
            nearest = r->range;
            const auto& t = triangles[r->id];
            Fvector a, b;
            a.sub(vertices[t.verts[1]], vertices[t.verts[0]]);
            b.sub(vertices[t.verts[2]], vertices[t.verts[0]]);
            normal.crossproduct(a, b).normalize_safe();
        }
    }
    if (nearest > range) return false;
    hit.mad(start, dir, nearest);
    if (normal.dotproduct(dir) > 0.f) normal.invert();
    return true;
}

bool CActorParkour::Sweep(const Fvector& from, const Fvector& to, u32 box, float extraHorizontalSkin)
{
    const Fbox& shape = Movement()->Boxes()[box];
    if (!ValidBox(shape) || !_valid(from) || !_valid(to)) return false;
    // Controller positions are feet positions; ph_box centers aren't world offsets.
    // Union of endpoint AABBs contains the complete linear swept body, not just
    // sampled endpoints. Skin only removes existing floor/wall contact tolerance.
    const float baseRx = (shape.max.x - shape.min.x) * .5f;
    const float baseRz = (shape.max.z - shape.min.z) * .5f;
    const float baseRadius = _min(baseRx, baseRz);
    const float frontRadius = baseRadius;
    const float sideRadius = baseRadius;
    // The footprint octagon has one fixed world orientation. Turned with the camera
    // or the approach, a route validated while looking one way could be blocked
    // by its own corners when executed along another (an "obstructed" cancel).
    const Fvector forward = {0.f, 0.f, 1.f}, right = {1.f, 0.f, 0.f};
    // Validate the original circular actor footprint, independent of camera yaw.
    const float horizontalSkin = .025f + _max(0.f, extraHorizontalSkin);
    const float rx = _max(.02f, sqrtf(_sqr(forward.x * frontRadius) + _sqr(right.x * sideRadius)) - horizontalSkin);
    const float rz = _max(.02f, sqrtf(_sqr(forward.z * frontRadius) + _sqr(right.z * sideRadius)) - horizontalSkin);
    // ODE clamps a negative cylinder length to 1 cm. A low crouch box shorter
    // than its diameter is therefore not as short as its LTX height suggests.
    const float height = _max(shape.max.y - shape.min.y, 2.f * baseRadius + .01f);
    Fbox swept;
    swept.min.set(_min(from.x, to.x) - rx, _min(from.y, to.y) + skin, _min(from.z, to.z) - rz);
    swept.max.set(_max(from.x, to.x) + rx, _max(from.y, to.y) + height - skin, _max(from.z, to.z) + rz);
    Fvector center, half;
    swept.getcenter(center);
    swept.getradius(half);
    m_collider.box_options(CDB::OPT_FULL_TEST);
    m_collider.box_query(Level().ObjectSpace.GetStaticModel(), center, half);
    const auto* triangles = Level().ObjectSpace.GetStaticTris();
    const auto* vertices = Level().ObjectSpace.GetStaticVerts();
    const BodyPlanes body(from, to, forward, right,
        _max(.02f,frontRadius-horizontalSkin),_max(.02f,sideRadius-horizontalSkin),height);
    for (auto* r = m_collider.r_begin(); r != m_collider.r_end(); ++r)
        if (Blocks(triangles[r->id]) && IntersectsBody(vertices,triangles[r->id],body)) return false;

    // Conservative dynamic broad phase, refreshed on every executed interval.
    // No promises of support on moving props: their bounding volumes block traversal.
    Level().ObjectSpace.GetNearest(m_nearby, center, half.magnitude() + .1f, m_actor);
    for (auto* object : m_nearby)
    {
        if (object->H_Parent() || !object->CFORM() || object->getDestroy()) continue;
        if (!(object->spatial.type & (STYPE_PHYSIC | STYPE_OBSTACLE))) continue;
        Fbox bounds;
        bounds.xform(object->CFORM()->getBBox(), object->XFORM());
        if (swept.intersect(bounds)) return false;
    }
    return true;
}

namespace
{
    // Closest point on a triangle (Ericson, Real-Time Collision Detection 5.1.5).
    Fvector ClosestOnTriangle(const Fvector& p, const Fvector& a, const Fvector& b, const Fvector& c)
    {
        Fvector ab, ac, ap, bp, cp, result;
        ab.sub(b, a); ac.sub(c, a); ap.sub(p, a);
        const float d1 = ab.dotproduct(ap), d2 = ac.dotproduct(ap);
        if (d1 <= 0.f && d2 <= 0.f) return a;
        bp.sub(p, b);
        const float d3 = ab.dotproduct(bp), d4 = ac.dotproduct(bp);
        if (d3 >= 0.f && d4 <= d3) return b;
        const float vc = d1*d4 - d3*d2;
        if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) return result.mad(a, ab, d1/(d1 - d3));
        cp.sub(p, c);
        const float d5 = ab.dotproduct(cp), d6 = ac.dotproduct(cp);
        if (d6 >= 0.f && d5 <= d6) return c;
        const float vb = d5*d2 - d1*d6;
        if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) return result.mad(a, ac, d2/(d2 - d6));
        const float va = d3*d6 - d5*d4;
        if (va <= 0.f && d4 - d3 >= 0.f && d5 - d6 >= 0.f)
        {
            Fvector bc; bc.sub(c, b);
            return result.mad(b, bc, (d4 - d3)/((d4 - d3) + (d5 - d6)));
        }
        const float denom = 1.f/(va + vb + vc);
        result.mad(a, ab, vb*denom);
        return result.mad(ac, vc*denom);
    }
}

bool CActorParkour::SphereClear(const Fvector& center, float radius)
{
    // A body part (the head) against the level and props, like Sweep for the capsule.
    Fvector half = {radius, radius, radius};
    m_collider.box_options(CDB::OPT_FULL_TEST);
    m_collider.box_query(Level().ObjectSpace.GetStaticModel(), center, half);
    const auto* triangles = Level().ObjectSpace.GetStaticTris();
    const auto* vertices = Level().ObjectSpace.GetStaticVerts();
    for (auto* r = m_collider.r_begin(); r != m_collider.r_end(); ++r)
    {
        const auto& t = triangles[r->id];
        if (!Blocks(t)) continue;
        const Fvector closest = ClosestOnTriangle(center, vertices[t.verts[0]], vertices[t.verts[1]], vertices[t.verts[2]]);
        if (closest.distance_to_sqr(center) < radius*radius) return false;
    }
    Fbox sphere; sphere.set(center, center); sphere.grow(radius);
    Level().ObjectSpace.GetNearest(m_nearby, center, radius + .1f, m_actor);
    for (auto* object : m_nearby)
    {
        if (object->H_Parent() || !object->CFORM() || object->getDestroy()) continue;
        if (!(object->spatial.type & (STYPE_PHYSIC | STYPE_OBSTACLE))) continue;
        Fbox bounds;
        bounds.xform(object->CFORM()->getBBox(), object->XFORM());
        if (sphere.intersect(bounds)) return false;
    }
    return true;
}

bool CActorParkour::Supported(const Fvector& feet, float radius, const Fvector& forward)
{
    const Fvector down = {0.f, -1.f, 0.f};

    // Support follows the local surface plane instead of assuming a perfectly flat
    // top. This keeps sloped roofs valid while still requiring the actor's center
    // and enough of the footprint to be genuinely supported.
    Fvector centerStart = feet, centerHit, centerNormal;
    centerStart.y += .12f;
    if (!Ray(centerStart, down, .30f, centerHit, centerNormal) ||
        centerNormal.y < cosf(m_options.maxSlope) || _abs(centerHit.y - feet.y) > .12f)
        return false;

    // Probe across (forward) and along (right) the approach, so a fence or rail
    // gives the same result whatever its world orientation.
    Fvector ahead = forward; ahead.y = 0.f;
    if (ahead.square_magnitude() < EPS_S) ahead.set(0.f, 0.f, 1.f);
    ahead.normalize();
    const Fvector side = {ahead.z, 0.f, -ahead.x};
    auto sample = [&](float across, float along, float scale)->bool
    {
        const float dx = (ahead.x * across + side.x * along) * scale;
        const float dz = (ahead.z * across + side.z * along) * scale;
        const float planeY = centerHit.y -
            (centerNormal.x * dx + centerNormal.z * dz) / _max(.20f, centerNormal.y);
        Fvector start = feet, hit, normal;
        start.x += dx;
        start.z += dz;
        start.y = planeY + .12f;
        return Ray(start, down, .26f, hit, normal) && normal.y >= cosf(m_options.maxSlope) &&
            _abs(hit.y - planeY) <= .10f;
    };

    const float outer = radius * .72f;
    int contacts = 1;
    contacts += sample( 1.f, 0.f, outer) ? 1 : 0;
    contacts += sample(-1.f, 0.f, outer) ? 1 : 0;
    contacts += sample( 0.f, 1.f, outer) ? 1 : 0;
    contacts += sample( 0.f,-1.f, outer) ? 1 : 0;
    if (contacts >= 3) return true;

    // Compact posts/thin fence tops may only support a narrow footprint.
    const float inner = m_options.standWidth * .5f;
    return sample( 1.f, 0.f, inner) && sample(-1.f, 0.f, inner) &&
           sample( 0.f, 1.f, inner) && sample( 0.f,-1.f, inner);
}

void CActorParkour::TravelRates(const Candidate& c, float& horizontalRate, float& verticalRate) const
{
    const float approach = _max(0.f, c.velocity.dotproduct(c.direction));
    const float approachFactor = clampr(approach / 6.f, 0.f, 1.f);
    // Upward jump velocity is deliberately NOT used here: a jump must not turn
    // an immediate grab into a super-fast climb. Height produces a continuous
    // gravity/effort penalty. Low vaults/mantles naturally get more benefit;
    // high climbs naturally get less. The action-name thresholds never change speed.
    // Physical effort depends on the actual rise, not on the user's configured
    // maximum allowed height. Raising maxHeight must never make the same ledge
    // faster. The curve saturates for very tall climbs instead.
    const float heightFactor = clampr(c.rise / 2.5f, 0.f, 1.f);
    const float approachBonus = .38f * approachFactor * (1.f - .55f * heightFactor);
    horizontalRate = m_options.horizontalSpeed * (.82f + approachBonus);
    const float lowHeightBonus = .28f * (1.f - heightFactor);
    const float gravityPenalty = .36f * heightFactor;
    verticalRate = m_options.verticalSpeed * (1.f + lowHeightBonus - gravityPenalty);
}

bool CActorParkour::Build(Candidate& c, const Fvector& landing, u32 box, bool* blockedApproach, u32 initialBox)
{
    if (!ValidBox(Movement()->Boxes()[box])) return false;
    float top = c.lip.y + .045f;
    Fvector surfaceStart=c.lip,surfaceHit,surfaceNormal,down={0,-1,0};surfaceStart.y+=.12f;
    if(Ray(surfaceStart,down,.25f,surfaceHit,surfaceNormal) && surfaceNormal.y>=cosf(m_options.maxSlope))
    {
        // A flat body footprint needs clearance above the high side of a tilted
        // lip. This changes the tested lift, never shrinks the collision shape.
        const float grade=sqrtf(surfaceNormal.x*surfaceNormal.x+surfaceNormal.z*surfaceNormal.z)/surfaceNormal.y;
        top+=_min(.18f,Radius(box)*grade);
    }
    Fvector right = {c.direction.z, 0.f, -c.direction.x};
    Fvector offset; offset.sub(c.lip, c.origin);
    const float lateral = offset.dotproduct(right);

    Fvector points[5] = {c.origin, c.origin, c.origin, landing, landing};
    // Small alignment happens before entering the opening, not through a jamb.
    points[1].mad(right, lateral);
    // Under an overhang (a slab or canopy over the body) the lift is only clear in
    // front of its edge: the body first swings out beneath the lip. Against a wall
    // the body can never be this close to it, so ordinary ledges keep their path.
    if (c.origin.y < c.lip.y - .1f)
    {
        const float hang = Radius(initialBox == u32(-1) ? _min(u32(Movement()->BoxID()), 1u) : initialBox) + .03f;
        Fvector fromLip; fromLip.sub(c.origin, c.lip);
        const float along = fromLip.dotproduct(c.direction);
        if (along > .05f - hang) points[1].mad(c.direction, -(along + hang));
    }
    // Clear a raised far-side floor before crossing its front edge, rather than
    // cutting diagonally through it from the lower window sill.
    points[2] = points[1]; points[2].y = _max(_max(top, c.origin.y), landing.y + .03f);
    points[3].y = _max(points[2].y, landing.y);
    // Releasing gravity requires the complete body to be beyond the edge.
    // Validate the falling column without adding it to the guided trajectory.
    if (!Sweep(points[3], landing, box)) { ++g_parkourDiag.landing.column; return false; }
    // Guide only the lift and crossing. Once clear, the real physics controller
    // owns the descent, including a drop on the far side of a fence/window.
    c.freeRelease = landing.y < points[3].y - .12f;
    // Keep the grab and initial pull in the starting stance. Only tuck during
    // the final lift; a wider crouch still has to clear the nearby wall.
    // From low crouch the grab starts crouched: a body holding an edge is never that low.
    const u32 approachBox = initialBox == u32(-1) ? _min(u32(Movement()->BoxID()), 1u) : initialBox;
    if (!Sweep(points[0], points[0], approachBox))
    { ++g_parkourDiag.landing.approach; if (blockedApproach) *blockedApproach=true; return false; }
    const float crouchY = points[1].y + (points[2].y-points[1].y)*m_options.topCrouchStart;
    c.path.clear(); c.distances.clear(); c.pathBoxes.clear();
    c.path.push_back(points[0]); c.distances.push_back(0.f); c.pathBoxes.push_back(approachBox);
    float length = 0.f;
    float horizontalRate, verticalRate;
    TravelRates(c,horizontalRate,verticalRate);
    auto travelTime = [horizontalRate, verticalRate](const Fvector& a, const Fvector& b)
    {
        const float dx = b.x - a.x, dz = b.z - a.z;
        return sqrtf(dx*dx + dz*dz) / horizontalRate + _abs(b.y - a.y) / verticalRate;
    };
    for (int phase = 0; phase < 3; ++phase)
    {
        const u32 phaseBox = phase < 2 ? approachBox : box;
        if (points[phase].distance_to(points[phase+1])<=EPS) continue;
        // A clear conservative sweep covers every sampled interval. If its
        // hull touches geometry, retain the finer checks for tight openings.
        const bool clearPhase = Sweep(points[phase],points[phase+1],phaseBox);
        const int count = _max(1, int(ceilf(points[phase].distance_to(points[phase + 1]) / step)));
        for (int i = 1; i <= count; ++i)
        {
            Fvector next;
            next.lerp(points[phase], points[phase + 1], float(i) / float(count));
            u32 segmentBox = phaseBox;
            if (phase == 1 && box != approachBox &&
                (c.pathBoxes.back() == box || (c.path.back().y >= crouchY &&
                    Sweep(c.path.back(), next, box)))) segmentBox = box;
            if (!(clearPhase && segmentBox == phaseBox) && !Sweep(c.path.back(), next, segmentBox))
            {
                // This prefix is shared by every landing for the same lip/stance.
                // Cache only an obstruction below the common lift height.
                if (blockedApproach && (phase==0 || (phase==1 && next.y<=_max(top,c.origin.y))))
                    *blockedApproach=true;
                ++g_parkourDiag.landing.path;
                return false;
            }
            length += travelTime(c.path.back(), next);
            c.path.push_back(next); c.distances.push_back(length);
            c.pathBoxes.push_back(segmentBox);
        }
    }
    if (length <= 0.f || !_valid(length)) return false;
    // 20% / 40% lower speed, rather than merely 20% / 40% longer duration.
    const float stanceSpeed = box == 1 ? .8f : box == 2 ? .6f : 1.f;
    for (auto& time : c.distances) time /= stanceSpeed;
    c.box = box; c.landing = landing;
    FitBody(c);
    return true;
}

void CActorParkour::SmoothPath(Candidate& c)
{
    xr_vector<Fvector> input;
    input.reserve(c.path.size());
    for (int pass = 0; pass < 8; ++pass)
    {
        input = c.path;
        for (size_t i = 1; i + 1 < input.size(); ++i)
        {
            if ((c.topTime > 0.f && c.distances[i] == c.topTime) ||
                c.pathBoxes[i] != c.pathBoxes[i+1]) continue;
            Fvector average; average.add(input[i-1], input[i+1]).mul(.5f);
            Fvector proposed; proposed.lerp(input[i], average, .5f);
            Fvector change; change.sub(proposed,input[i]);
            if (change.square_magnitude() < 1e-10f) continue;
            if (Sweep(c.path[i-1],proposed,c.pathBoxes[i]) &&
                Sweep(proposed,input[i+1],c.pathBoxes[i+1])) c.path[i]=proposed;
        }
    }
    const float duration=c.distances.back();
    float horizontalRate, verticalRate, elapsed=0.f;
    TravelRates(c,horizontalRate,verticalRate);
    size_t topIndex=0;
    for (size_t i=1;i<c.path.size();++i)
    {
        if (c.topTime>0.f && c.distances[i]==c.topTime) topIndex=i;
        Fvector delta;delta.sub(c.path[i],c.path[i-1]);
        elapsed+=sqrtf(delta.x*delta.x+delta.z*delta.z)/horizontalRate+_abs(delta.y)/verticalRate;
        c.distances[i]=elapsed;
    }
    if (elapsed>0.f)
        for (auto& time : c.distances) time*=duration/elapsed;
    if (topIndex) c.topTime=c.distances[topIndex];
}

bool CActorParkour::FindLanding(Candidate& c)
{
    const Fvector down = {0.f, -1.f, 0.f};
    const float radius = Radius(0);
    g_parkourDiag.landing = ParkourLandingDiag();
    // Identify an actual bounded opening, including an internal window divider.
    // Only center within the opening containing this probe; never jump a frame.
    Fvector right = {c.direction.z, 0.f, -c.direction.x}, left = right;
    left.invert();
    float openingWidth = flt_max;
    float leftBound = -flt_max, rightBound = flt_max;
    float bodyHeight = flt_max, smallestRadius = flt_max;
    for (u32 box : {0u,1u,2u})
    {
        const Fbox& shape = Movement()->Boxes()[box];
        if (!ValidBox(shape)) continue;
        smallestRadius = _min(smallestRadius,Radius(box));
        bodyHeight = _min(bodyHeight,_max(shape.max.y-shape.min.y,2.f*Radius(box)+.01f));
    }
    // Intersect jamb clearances at several depths/heights. A single ray can
    // center on the outer frame while missing the narrower opening behind it.
    const float sideRange = 2.f*radius + m_options.centerAssist;
    for (float depth : {.04f,radius,2.f*radius})
        for (float height : {bodyHeight*.35f,bodyHeight*.80f})
        {
            Fvector probe=c.lip, leftHit, rightHit, leftNormal, rightNormal;
            probe.mad(c.direction,depth); probe.y+=height;
            if (!Ray(probe,left,sideRange,leftHit,leftNormal) ||
                !Ray(probe,right,sideRange,rightHit,rightNormal) ||
                leftNormal.dotproduct(right)<.5f || rightNormal.dotproduct(left)<.5f) continue;
            Fvector delta;
            delta.sub(leftHit,c.lip); leftBound=_max(leftBound,delta.dotproduct(right));
            delta.sub(rightHit,c.lip); rightBound=_min(rightBound,delta.dotproduct(right));
        }
    if (leftBound != -flt_max && rightBound != flt_max)
    {
        openingWidth=rightBound-leftBound;
        Fvector fromActor; fromActor.sub(c.lip,c.origin);
        const float lateral=fromActor.dotproduct(right);
        const float shift=clampr((leftBound+rightBound)*.5f,
            -m_options.centerAssist-lateral,m_options.centerAssist-lateral);
        if (openingWidth >= 2.f*smallestRadius && _abs(shift)>EPS)
        {
            Fvector centered=c.lip,support,supportNormal;
            centered.mad(right,shift);centered.y+=.09f;
            if (Ray(centered,down,.18f,support,supportNormal) &&
                supportNormal.y>=cosf(m_options.maxSlope) && _abs(support.y-c.lip.y)<=.06f)
            {support.y=c.lip.y;c.lip=support;}
        }
    }
    Fvector roofStart=c.lip, roofHit, roofNormal, up={0,1,0};
    roofStart.mad(c.direction,.05f);roofStart.y+=.15f;
    const bool windowOpening=Ray(roofStart,up,2.4f,roofHit,roofNormal)&&roofNormal.y<-.65f;
    Candidate topCandidate = c, overCandidate = c;
    bool topFound = false, overFound = false;
    bool blockedApproach[3] = {false,false,false};
    bool farEdge = false;
    float previousTopY = c.lip.y, topDepth = flt_max;
    // Search actual support on both sides of a lip. While the samples remain
    // locally continuous, treat them as the same top surface even when it slopes.
    // A missing sample or a sudden height discontinuity marks the far edge.
    for (float d = .002f; d <= m_options.maxDepth + radius + .40f; d += .02f)
    {
        Fvector probe, hit, normal;
        probe.mad(c.lip, c.direction, d); probe.y += .18f;
        const bool floor = Ray(probe, down, c.rise + m_options.maxDrop + .55f, hit, normal);
        const bool walkable = floor && normal.y >= cosf(m_options.maxSlope) &&
            hit.y >= c.origin.y - m_options.maxDrop &&
            hit.y <= c.lip.y + .75f;

        if (!farEdge)
        {
            const bool continuous = walkable && _abs(hit.y - previousTopY) <= .16f;
            if (!continuous) { farEdge = true; topDepth = d; }
            else previousTopY = hit.y;
        }
        ++g_parkourDiag.landing.samples;
        if (!walkable) { ++g_parkourDiag.landing.notWalkable; continue; }

        // Feet position is the bottom of the rounded foot, not its contact point
        // on an incline. Raise the center by the sphere/plane support offset.
        hit.y += .015f + radius*(1.f/normal.y-1.f);
        bool onTop = !farEdge;
        if (windowOpening && onTop)
        {
            // The far floor may be flush with the sill. Crossing a window is
            // defined by clearing its lintel, not by finding a drop in the floor.
            Fvector ceilingStart=hit, ceilingHit, ceilingNormal;
            ceilingStart.mad(c.direction,-radius); ceilingStart.y=c.lip.y+.15f;
            if (d > radius + .06f &&
                (!Ray(ceilingStart,up,2.4f,ceilingHit,ceilingNormal) ||
                 ceilingHit.y > roofHit.y+.12f)) onTop=false;
        }
        if (onTop ? topFound : overFound) continue;
        if (!onTop && d > m_options.maxDepth + radius) { ++g_parkourDiag.landing.depth; continue; }
        if (!onTop && !windowOpening && c.rise > m_options.vaultHeight &&
            !m_options.forwardControl && !m_options.allowClimbVault) { ++g_parkourDiag.landing.permission; continue; }
        if (!Supported(hit, radius, c.direction)) { ++g_parkourDiag.landing.unsupported; continue; }
        Candidate trial = c;
        trial.route = onTop ? "top" : (windowOpening ? "window" : "over");
        trial.action = (onTop ? (c.rise > m_options.mantleHeight ? "climb" : "mantle") :
            (c.rise <= m_options.vaultHeight ? "vault" : "climb_vault"));
        // Windows belong to Vault at every height. High open climb-overs need
        // both Climb and Vault; a disabled route must not hide another valid one.
        if (windowOpening ? (onTop || !m_options.allowVault) :
            onTop ? (c.rise > m_options.mantleHeight ? !m_options.allowClimb : !m_options.allowMantle) :
            (!m_options.allowVault || (c.rise > m_options.vaultHeight &&
                (m_options.forwardControl && c.rise<=m_options.mantleHeight ? !m_options.allowMantle : !m_options.allowClimb))))
        { ++g_parkourDiag.landing.permission; continue; }
        if (c.obstacleHeight+EPS_S < (onTop ? m_options.minMantleHeight : m_options.minVaultHeight) &&
            (!onTop || c.rise <= m_options.mantleHeight)) { ++g_parkourDiag.landing.minHeight; continue; }
        bool fits = false;
        // Prefer the requested top stance. Only standing and crouched: the visible
        // body never gets lower than its crouch, so the 0.44 m low-crouch box would
        // pass openings the body itself could only go through by clipping.
        const u32 tiers[] = {m_options.crouchAtTop ? 1u : 0u, m_options.crouchAtTop ? 0u : 1u};
        for (u32 box : tiers)
        {
            if (blockedApproach[box]) continue;
            // Contact skin is not extra body clearance through a window frame.
            // Each stance must fit its real shoulder width between the jambs.
            if(2.f*Radius(box)>openingWidth+EPS_S) { ++g_parkourDiag.landing.wideBody; continue; }
            if (Build(trial, hit, box, &blockedApproach[box])) { fits = true; break; }
        }
        if (!fits) continue;
        if (onTop) { topCandidate = trial; topFound = true; }
        else { overCandidate = trial; overFound = true; }
        if (topFound && overFound) break;
    }
    // A top too shallow to stand on (by the script's measure) is only gone over.
    if (topFound && overFound && !windowOpening && topDepth < m_options.minTopDepth) topFound = false;
    // A fast approach vaults what can be vaulted (within the depth limit) instead of
    // getting onto it; slower, a standable top is mantled.
    const bool vault = overFound && !windowOpening && (m_options.preferVault ||
        (m_options.vaultSpeed > 0.f && c.velocity.dotproduct(c.direction) >= m_options.vaultSpeed));
    if (m_options.forwardControl && !windowOpening && c.rise > m_options.vaultHeight)
    {
        // With a standable top, a climb/mantle ends there. Holding Forward may
        // continue across only after that endpoint, never replace it with a drop.
        if (!topFound)
        {
            // No standable top (thin fence/rail): climbing over is the only route.
            // Reach was already checked from the arms; height alone must not veto it.
            g_parkourDiag.landing.needTop = overFound ? 2 : 1;
            if (!overFound) return false;
            c = overCandidate;
            return true;
        }
        if (vault) { c = overCandidate; return true; }
        c = topCandidate;
        if (overFound && m_options.forwardHeld && m_options.crossTop && topCandidate.box==overCandidate.box)
        {
            Candidate crossing=overCandidate;
            crossing.origin=topCandidate.path.back();
            if (Build(crossing,overCandidate.landing,overCandidate.box,nullptr,topCandidate.box))
            {
                c.topTime=c.distances.back(); c.topLanding=c.landing; c.topBox=c.box;
                for(size_t i=1;i<crossing.path.size();++i)
                { c.path.push_back(crossing.path[i]); c.distances.push_back(c.topTime+crossing.distances[i]);
                  c.pathBoxes.push_back(crossing.pathBoxes[i]); }
                c.landing=crossing.landing;c.freeRelease=crossing.freeRelease;
            }
        }
    }
    else if (overFound && windowOpening) c = overCandidate;
    else if (vault) c = overCandidate;
    else if (topFound && c.airborne && m_options.airborneTop) c = topCandidate;
    else if (overFound && (!topFound || m_options.preferVault)) c = overCandidate;
    else if (topFound) c = topCandidate;
    else return false;
    return true;
}

bool CActorParkour::Query(const Options& options)
{
    if (!m_enabled) { m_reason = "disabled"; return false; }
    // Debug queries during a move must not erase the executing trajectory.
    if (m_active) { m_reason = "busy"; return false; }
    m_candidate = Candidate();
    m_options = options;
    // The body sets the move classes and grip: vault up to the hips, mantle up to
    // the chest, climb above that; hands on the edge shoulder-width apart.
    if (m_bodyHipHeight > 0.f)
    {
        m_options.vaultHeight = m_bodyHipHeight;
        m_options.mantleHeight = _max(m_bodyHipHeight, m_bodyShoulderHeight - .15f);
        m_options.maxHeight = _max(m_options.maxHeight, m_options.mantleHeight);
    }
    // Below the knees is walking (and the step-over smoothing): from the knees up a
    // top is mantled or vaulted, above the chest it is a climb (mantleHeight).
    if (m_options.kneeFloor && m_bodyKneeHeight > 0.f)
        m_options.minHeight = _max(m_options.minHeight, m_bodyKneeHeight);
    if (m_bodyShoulderWidth > 0.f)
    {
        m_options.handSpacing = m_bodyShoulderWidth;
        m_options.minHandSpacing = _min(m_options.minHandSpacing, m_bodyShoulderWidth*.5f);
    }
    if (m_bodyArmLength > 0.f && Movement())
    {
        // How far ahead an edge can be grabbed: beyond the body's front by about half
        // an arm (reaching forward, leaning in), from its center by most of an arm.
        m_options.reach = m_bodyArmLength*.55f;
        m_options.armExtension = Radius(0) + m_bodyArmLength*.8f;
    }
    g_parkourDiag.rejected = ParkourLandingDiag(); g_parkourDiag.rejectedRise = 0.f; g_parkourDiag.rejectedLip.set(0.f,0.f,0.f);
    if (!Available()) { ParkourLogDiagnostics(*this,"query"); return false; }

    auto* movement = Movement();
    if (movement->GetVelocity().y < -m_options.maxFall) { m_reason = "falling"; ParkourLogDiagnostics(*this,"query"); return false; }
    const Fvector feet = m_actor->Position();
    const bool airborne = Airborne();
    // Obstacle height is measured from the takeoff ground, but once the body has
    // dropped below it (jumping down off a roof) from the current feet instead.
    const float ground = m_options.hasHeightOrigin ? m_options.heightOrigin : feet.y;
    const float heightOrigin = airborne ? _min(ground,feet.y) : ground;
    const float minimumRise = _max(.04f,m_options.minHeight-(feet.y-heightOrigin));
    // Vaults and mantles are found from the feet. A climb is limited by what the
    // arms reach from the shoulders, including a jump still to come, not by a
    // fixed height: a tall fence is acquired before the jump that reaches it.
    const float riseLimit = _max(m_options.maxHeight,
        m_options.allowClimb ? ReachCeiling(airborne) - feet.y : 0.f);

    auto* camera = m_actor->cam_Active();
    if (!camera) { m_reason = "no_camera"; return false; }
    Fvector dir = camera->vDirection;
    dir.y = 0.f;
    // Looking almost straight up/down should not destroy the horizontal search
    // direction. Preserve the actor yaw for geometry probing; screen-space intent
    // is still checked against the real camera direction below.
    if (dir.square_magnitude() < .01f) dir = BodyForward();
    if (dir.square_magnitude() < .01f) { m_reason = "look_forward"; return false; }
    dir.normalize();
    Fvector right; right.set(dir.z, 0.f, -dir.x);
    const Fvector down = {0.f, -1.f, 0.f};
    const float radius = Radius(0), grabRange = radius + m_options.reach;
    // Ledges are acquired further out than they can be grabbed, while they are
    // still inside the screen box, and then tracked while the player closes in.
    const float range = _max(grabRange, m_options.detectRange);
    const float columns[] = {0.f, .12f, -.12f, .24f, -.24f};

    // Two searches share the scan: vaults/mantles judged from the feet and climbs
    // from the shoulders. Each keeps its own best ledge; the choice is made below.
    struct Choice { Candidate c; float score = flt_max, center = flt_max; bool tracked = false; } low, high;
    xr_vector<Fvector> validated;
    bool sawLedge = false;
    bool routeRejected = false;

    auto consider = [&](const Fvector& lip, float rise)
    {
        if (lip.y-heightOrigin+EPS_S < m_options.minHeight || rise < .04f || rise > riseLimit) return;
        Fvector delta; delta.sub(lip, feet); delta.y = 0.f;
        const float horizontalDistance = delta.magnitude();
        if (horizontalDistance > range + .04f) return;

        // A tracked ledge stays valid after it leaves the box (a tall fence goes
        // above the screen as the player reaches it) while it is still faced.
        Fvector facing = delta;
        if (facing.square_magnitude() > EPS_S) facing.normalize();
        const bool ahead = horizontalDistance <= radius || facing.square_magnitude() <= EPS_S ||
            facing.dotproduct(dir) >= .5f;
        const bool tracked = m_options.hasTarget && lip.distance_to(m_options.target) <= m_options.targetRadius && ahead;
        const bool inBox = InViewWindow(lip);
        const bool climb = rise > m_options.mantleHeight;
        // A vault or mantle needs no aim: an edge within reach in front of the body
        // counts while looking up or away. A climb must be looked at (or locked).
        // An edge over the body, or just behind it, is grabbed without being looked at.
        // That covers standing under a slab or canopy and jumping at it (the body swings
        // out under the edge), and a window, fence or wall the body is pressed against.
        // Above the hips, so it still counts at the top of a jump, where the edge has
        // come down to chest height.
        const bool overhead = horizontalDistance <= radius + OverheadReach &&
            lip.y > feet.y + m_options.vaultHeight;
        const bool nearby = (!climb && ahead && horizontalDistance <= grabRange + .04f) || overhead;
        if (!inBox && !tracked && !nearby) return;
        sawLedge = true;

        Choice& best = climb ? high : low;
        const float viewScore = inBox ? ViewScore(lip) : (tracked ? .35f : 2.f);
        float x, y;
        const float center = ScreenPoint(lip, x, y) ? sqrtf(x*x + y*y) : flt_max;
        Candidate trial;
        trial.origin = feet;
        trial.lip = lip;
        trial.rise = rise;
        trial.obstacleHeight = lip.y-heightOrigin;
        trial.direction = dir;
        trial.velocity = movement->GetVelocity();
        trial.airborne = airborne;
        // Out of arm reach (too far, or too high until the jump gets there): track
        // the ledge. The full route validation runs only once the hands can grab.
        if (horizontalDistance > grabRange + .04f || !ContactReady(trial))
        {
            const float score = 6.f + viewScore + horizontalDistance*.08f;
            if (score < best.score)
            {
                best.score = score; best.center = center; best.tracked = tracked;
                best.c = trial; best.c.tentative = true;
            }
            return;
        }
        const float scoreFloor = _min(viewScore*.25f + horizontalDistance*2.f,
            viewScore + horizontalDistance*.08f);
        if (scoreFloor >= best.score) return;
        // Columns and both scans often find the same edge; validate it once.
        for (const Fvector& done : validated)
            if (_sqr(done.x-lip.x) + _sqr(done.z-lip.z) < .0016f && _abs(done.y-lip.y) < .03f) return;
        validated.push_back(lip);
        for (const auto& failed : m_failed)
            if (failed.time && Device.dwTimeGlobal - failed.time < 400 &&
                failed.lip.distance_to(lip) < .05f && failed.feet.distance_to(feet) < .10f)
            { routeRejected = true; return; }
        if (!FindLanding(trial))
        {
            routeRejected = true;
            g_parkourDiag.rejected = g_parkourDiag.landing; g_parkourDiag.rejectedLip = lip; g_parkourDiag.rejectedRise = rise;
            auto& failed = m_failed[m_failedNext++ % 8];
            failed.lip = lip; failed.feet = feet; failed.time = _max(1u, Device.dwTimeGlobal);
            return;
        }
        // A route that must stay upright (low ceiling) cannot lean in for reach:
        // track it until the hands really get there.
        if (trial.upright && !ContactReady(trial))
        {
            const float score = 6.f + viewScore + horizontalDistance*.08f;
            if (score < best.score)
            {
                best.score = score; best.center = center; best.tracked = tracked;
                best.c = trial; best.c.tentative = true;
            }
            return;
        }

        // In a window, prefer the near sill over raised flooring beyond it.
        // Looking down into the opening must not move the grab to the far side.
        const float score = !strcmp(trial.route,"window") ?
            viewScore*.25f + delta.magnitude()*2.f : viewScore + delta.magnitude()*.08f;
        if (score < best.score)
        {
            best.score = score; best.center = center; best.tracked = tracked;
            best.c = trial;
        }
    };

    // Re-test the acquired surface directly. A fast jump may pass a thin lip
    // between scan columns while the hands are still physically beside it.
    if (m_options.hasTarget)
    {
        Fvector from=m_options.target,lip,normal;from.y+=.12f;
        if (Ray(from,down,.25f,lip,normal) && normal.y>=cosf(m_options.maxSlope))
            consider(lip,lip.y-feet.y);
    }

    // Wall-backed ledges/fences: find the blocked->clear transition in several
    // horizontal columns, then validate the actual top and full body route.
    for (float lateral : columns)
    {
        bool blockedBelow = false;
        Fvector face, faceNormal;
        for (float h = _max(.04f, minimumRise - .04f);
             h <= riseLimit + .12f; h += .08f)
        {
            Fvector origin, hit, normal;
            origin.mad(feet, right, lateral); origin.y += h;
            // Start a few centimetres behind the actor center. This makes the
            // blocked->clear scan reliable when the physical body is already
            // touching/coplanar with a wall or fence.
            const float backShift = _min(.07f, radius * .20f);
            origin.mad(dir, -backShift);
            const bool blocked = Ray(origin, dir, range + backShift, hit, normal);
            Fvector depthChange = {0.f,0.f,0.f};
            if(blocked && blockedBelow) depthChange.sub(hit,face);
            const bool clearsNearFace=blockedBelow && (!blocked || depthChange.dotproduct(dir)>.06f);
            if (clearsNearFace && _abs(faceNormal.y)<=.6f)
            {
                // A farther wall/floor can still be hit above the sill. The
                // near face has ended even though the whole ray isn't clear.
                for (float inset : {.003f, .015f, .035f})
                {
                    Fvector probe = face, lip, lipNormal;
                    probe.mad(dir, inset); probe.y = origin.y + .025f;
                    if (!Ray(probe, down, .22f, lip, lipNormal) || lipNormal.y < cosf(m_options.maxSlope)) continue;
                    consider(lip, lip.y - feet.y);
                    break;
                }
            }
            blockedBelow=blocked;
            if(blocked) {face=hit;faceNormal=normal;}
        }
    }

    // Exposed roof/slab/rail tops: top-surface-first fallback. Require a real near
    // edge so ordinary flat ground never becomes a parkour candidate.
    const float edgeStep = .04f;
    const float edgeDrop = _max(.12f, _min(.25f, m_options.minHeight * .60f));
    for (float lateral : columns)
    {
        for (float d = edgeStep - (radius + OverheadReach); d <= range + .001f; d += edgeStep)
        {
            Fvector probe, lip, normal;
            probe.mad(feet, right, lateral); probe.mad(dir, d);
            probe.y += riseLimit + .05f;
            if (!Ray(probe, down, riseLimit - minimumRise + .18f, lip, normal) ||
                normal.y < cosf(m_options.maxSlope)) continue;
            const float rise = lip.y - feet.y;
            if (lip.y-heightOrigin+EPS_S < m_options.minHeight || rise < .04f || rise > riseLimit) continue;
            // A probe started inside a slab hits its underside (the rays are two-sided).
            // A real top has room above it for the hands.
            Fvector headroomStart = lip, headroomHit, headroomNormal;
            headroomStart.y += .03f;
            if (Ray(headroomStart, Fvector().set(0.f, 1.f, 0.f), .3f, headroomHit, headroomNormal)) continue;

            Fvector behindProbe, behindHit, behindNormal;
            behindProbe.mad(feet, right, lateral);
            behindProbe.mad(dir, d - edgeStep * 1.5f);
            behindProbe.y = probe.y;
            const bool behindSurface = Ray(behindProbe, down,
                riseLimit + m_options.maxDrop + .20f, behindHit, behindNormal) &&
                behindNormal.y >= cosf(m_options.maxSlope);
            if (behindSurface && behindHit.y > lip.y - edgeDrop) continue;

            consider(lip, rise);
        }
    }

    // Feet first: a vault or mantle, unless a climb is what the player aims at
    // (closer to the screen center). A locked ledge outranks a new one, so the
    // view moving during the jump toward it cannot switch targets, and a ledge
    // in arm reach outranks one that is still only being approached.
    Choice* pick = low.score != flt_max ? &low : nullptr;
    if (high.score != flt_max)
    {
        if (!pick || (high.tracked != pick->tracked ? high.tracked :
            high.c.tentative != pick->c.tentative ? !high.c.tentative : high.center < pick->center))
            pick = &high;
    }
    if (pick)
    {
        Candidate& best = pick->c;
        if (!best.tentative) SmoothPath(best);
        best.valid = true;
        m_candidate = best;
        m_queryFrame = Device.dwFrame;
        m_reason = best.tentative ? "tracking" : "ready";
        g_parkourDiag.startRise=0.f;g_parkourDiag.reachFrames=0;
        g_parkourDiag.reachError=g_parkourDiag.reachBest=g_parkourDiag.reachExcess=-1.f;
        return true;
    }

    m_reason = routeRejected ? "no_clear_route" : (sawLedge ? "outside_route" : "no_ledge");
    ParkourLogDiagnostics(*this,"query");
    return false;
}

bool CActorParkour::Airborne() const
{
    auto* movement = Movement();
    if (!movement || !movement->CharacterExist()) return false;
    auto* character = movement->PHCharacter();
    auto* actorCharacter = character ? character->CastActorCharacter() : nullptr;
    return actorCharacter ? !actorCharacter->Grounded() :
        movement->Environment() == CPHMovementControl::peInAir;
}

void CActorParkour::Shoulder(Fvector& shoulder, float& arm) const
{
    // Standing, the arm sphere around the shoulders tops out at armReachHeight
    // above the feet; it moves with the body when crouching or jumping.
    constexpr float shoulderBelowEye = .20f, cameraShift = .05f; // CActor::CameraHeight
    Fvector standing; Movement()->Boxes()[0].getsize(standing);
    const float standingEye = CameraFactor(m_actor->m_fCamHeightFactor)*(standing.y-cameraShift);
    shoulder = m_actor->Position();
    if (m_bodyArmLength > 0.f && standingEye > EPS)
    {
        // The visible model's shoulder and arm, lowered with the eyes when crouching.
        arm = m_bodyArmLength;
        shoulder.y += m_bodyShoulderHeight*m_actor->CameraHeight()/standingEye;
        return;
    }
    arm = _max(.10f, m_options.armReachHeight - (standingEye - shoulderBelowEye));
    shoulder.y += m_actor->CameraHeight() - shoulderBelowEye;
}

float CActorParkour::ReachCeiling(bool airborne) const
{
    // Highest edge the hands can get to: the top of the arm sphere, raised by the
    // jump still available (a full jump on the ground, the rest of the rise in air).
    Fvector shoulder; float arm;
    Shoulder(shoulder, arm);
    float gravity = physics_world() ? physics_world()->Gravity() : 0.f;
    // In the air the rise left is braked by the actor's own (possibly lighter) gravity;
    // a jump from the ground keeps its height either way.
    if (airborne && Movement() && Movement()->PHCharacter())
        if (auto* actorCharacter = Movement()->PHCharacter()->CastActorCharacter())
            gravity *= actorCharacter->AirGravity();
    const float speed = airborne ? _max(0.f, Movement()->GetVelocity().y) : m_actor->m_fJumpSpeed;
    const float jump = gravity > EPS ? _min(1.5f, speed*speed/(2.f*gravity)) : 0.f;
    return shoulder.y + arm + jump;
}

float CActorParkour::ReachSlack(const Candidate& c) const
{
    // Mantles and vaults are judged from the feet: their edge is at most chest
    // high (mantle height), so the hands always reach it and the body may sink.
    if (c.rise <= m_options.mantleHeight) return flt_max;
    // A climb edge is above the chest and must lie within arm's length of the
    // shoulders. Returns how much lower the body may still go with the edge in
    // reach (negative: out of reach).
    Fvector shoulder; float arm;
    Shoulder(shoulder, arm);
    // Hanging, the body leans its chest in to the wall (PostureLean), which carries
    // the shoulders this much closer to the edge, unless it must stay upright.
    const float lean = c.upright ? 0.f : sinf(HangLean)*ShoulderPivot;
    const float horizontal = _sqr(_max(0.f, sqrtf(_sqr(c.lip.x-shoulder.x) + _sqr(c.lip.z-shoulder.z)) - lean));
    if (horizontal > arm*arm) return -1.f;
    return sqrtf(arm*arm-horizontal) - (c.lip.y-shoulder.y);
}

bool CActorParkour::WallBelow(const Candidate& c)
{
    // A face under the edge that the feet can push against, rather than open air
    // below a sill, an overhang or a railing.
    const Fvector feet = m_actor->Position();
    Fvector horizontal; horizontal.sub(c.lip, feet); horizontal.y = 0.f;
    const float range = horizontal.magnitude() + .25f;
    for (float height : {.25f, .6f})
    {
        Fvector from = feet, hit, normal;
        from.y += height;
        if (from.y > c.lip.y - .15f) break;
        if (Ray(from, c.direction, range, hit, normal) && _abs(normal.y) < .5f) return true;
    }
    return false;
}

float CActorParkour::WallKick(const Candidate& c)
{
    // Jumping at a wall, one foot pushes off it and adds a little height to the
    // reach. Only while rising or near the top of the jump, never mid-fall, and
    // moving at the wall: a foot has nothing to push with otherwise.
    if (m_options.wallStep <= 0.f || !c.airborne || c.velocity.y < -1.f ||
        c.velocity.dotproduct(c.direction) < 1.f) return 0.f;
    return WallBelow(c) ? m_options.wallStep : 0.f;
}

bool CActorParkour::ContactReady(const Candidate& c)
{
    auto* movement = Movement();
    if (!movement) return false;
    // Normal vault/mantle/climb can be pre-approved while approaching, but guided
    // movement must not begin until the ledge is inside plausible arm reach.
    const Fvector feet = m_actor->Position();
    Fvector horizontal; horizontal.sub(c.lip, feet); horizontal.y = 0.f;
    const float distance = horizontal.magnitude();
    if (_max(0.f, distance-Radius(0)) > m_options.reach || distance > m_options.armExtension) return false;
    const float slack = ReachSlack(c);
    if (slack < 0.f && (slack <= -1.f || slack < -WallKick(c))) return false;
    // Do not reverse a clear fly-by. Small drift is tolerated because contact with
    // the wall often changes velocity just before the grab.
    return movement->GetVelocity().dotproduct(c.direction) > -1.50f;
}

void CActorParkour::Present(const ClipDef& clip, u8 handMask, float blendIn, float blendOut)
{
    m_presented = clip;
    m_presentedName = clip.name ? clip.name : "";
    m_presented.name = m_presentedName.c_str();
    m_presentedMask = handMask & 3;
    m_presentedIn = 1.f/clampr(_valid(blendIn) ? blendIn : 1.f/6.f, .05f, 1.f);
    m_presentedOut = 1.f/clampr(_valid(blendOut) ? blendOut : .25f, .05f, 1.f);
    m_presentedValid = true;
}

bool CActorParkour::Perform(float seconds, const ClipDef& clip, float distance, u8 camera, float blendIn, float blendOut)
{
    if (!m_enabled) { m_reason = "disabled"; return false; }
    if (m_active) { m_reason = "busy"; return false; }
    if (!Available()) return false;
    if (!_valid(seconds) || seconds <= 0.f || !clip.name || !clip.name[0] || !clip.frames)
    { m_reason = "invalid_clip"; return false; }
    m_playingName = clip.name; m_playing = clip; m_playing.name = m_playingName.c_str();
    // No edge: no reach, no easing onto one, no landing fit.
    m_playing.contact = m_playing.release = 0; m_playing.fit = false; m_playing.start = ClipDef::air;
    m_clip = &m_playing;
    Fvector forward = BodyForward();
    forward.y = 0.f;
    if (forward.square_magnitude() < EPS_S) forward.set(0.f, 0.f, 1.f);
    forward.normalize();
    m_performDir = forward;
    m_performAccel.set(0.f, 0.f, 0.f);
    m_clipMirror = clip.mirror && (m_clipCount++ & 1);
    m_clipOrigin.identity();
    m_clipOrigin.k = forward; m_clipOrigin.j.set(0.f, 1.f, 0.f);
    m_clipOrigin.i.crossproduct(m_clipOrigin.j, m_clipOrigin.k);
    m_clipOrigin.c = m_actor->Position();
    m_clipShift.set(0.f, 0.f, 0.f);
    float from, contact, end;
    ClipFrames(from, contact, end);
    m_clipRate = (end - contact)/ClipFps/seconds;
    m_blendIn = 1.f/clampr(_valid(blendIn) ? blendIn : .1f, .05f, 1.f);
    m_blendOut = 1.f/clampr(_valid(blendOut) ? blendOut : .2f, .05f, 1.f);
    m_presentedValid = false;
    m_handMask = 0; m_hands = false; m_contactValid[0] = m_contactValid[1] = false;
    m_reaching = m_missing = false; m_handConfirmed = true;
    m_perform = m_clipPerformed = true; m_clipCamera = camera == 2 ? 2 : 1;
    m_aimReady = false;
    m_performDistance = clampr(_valid(distance) ? distance : 0.f, 0.f, 4.f);
    if (!m_restorePending) m_savedBox = Movement()->BoxID();
    m_savedGravity = Movement()->AppliesGravity();
    m_elapsed = 0.f; m_duration = seconds; m_forwardHeld = false;
    m_active = true; m_candidate.valid = false;
    m_lastPosition = m_actor->Position();
    m_reason = "performing";
    return true;
}

bool CActorParkour::Start(float seconds, bool hands)
{
    // A script's presentation belongs to this attempt only, whether it starts or not.
    const bool presented = m_presentedValid;
    m_presentedValid = false;
    m_handMask = presented ? m_presentedMask : 3;
    m_presentedMask = 3;
    m_perform = false;
    m_blendIn = presented ? m_presentedIn : 6.f;
    m_blendOut = presented ? m_presentedOut : 4.f;
    if (!m_enabled) { m_reason = "disabled"; return false; }
    if (m_active) { m_reason = "busy"; return false; }
    if (!m_candidate.valid || Device.dwFrame - m_queryFrame > 2 ||
        m_actor->Position().distance_to(m_candidate.origin) > .05f)
    { m_reason = "stale_query"; m_candidate.valid = false; return false; }
    if (!Available()) return false;
    if (!_valid(seconds) || seconds <= 0.f) { m_reason = "invalid_duration"; return false; }

    // Querying is passive. Keep ordinary jump/gravity until the current body is
    // actually close enough to put hands on the approved ledge. The view was
    // already approved by the same-frame query (screen box or tracked ledge).
    if (m_candidate.tentative || !ContactReady(m_candidate)) { m_reason = "not_in_reach"; return false; }

    if (!Supported(m_candidate.landing, Radius(0), m_candidate.direction)) { m_reason = "landing_changed"; return false; }
    LPCSTR section = nullptr;
    for (LPCSTR hudSection : {"tt_hands", "item_anm_ledge_grabbing"})
        if (pSettings->section_exist(hudSection)) { section = hudSection; break; }
    if (section)
    {
        m_contactStart = READ_IF_EXISTS(pSettings,r_float,section,"parkour_ik_contact_start",.06f);
        m_contactFull = READ_IF_EXISTS(pSettings,r_float,section,"parkour_ik_contact_full",.24f);
        m_contactRelease = READ_IF_EXISTS(pSettings,r_float,section,"parkour_ik_contact_release",.78f);
        m_contactEnd = READ_IF_EXISTS(pSettings,r_float,section,"parkour_ik_contact_end",.94f);
        if (!_valid(m_contactStart) || !_valid(m_contactFull) || !_valid(m_contactRelease) ||
            !_valid(m_contactEnd) || m_contactStart<0.f || m_contactEnd>1.f ||
            m_contactFull<=m_contactStart || m_contactRelease<m_contactFull || m_contactEnd<=m_contactRelease)
        { m_reason="invalid_contact_phases"; return false; }
    }
    m_hands=hands && m_handMask;
    m_contactValid[0] = m_contactValid[1] = false;
    CaptureHands();
    // A hand the move does not use leaves no contact (no IK target, no HUD target).
    for (int side = 0; side < 2; ++side)
        if (!(m_handMask & (1 << side))) m_contactValid[side] = false;
    if (m_handMask && !HandContactsReady())
    {m_hands=false;m_reason="no_hand_support";return false;}

    // One committed attempt. Above the catch speed the hands still reach for the
    // edge once while the body keeps falling ("slipped"); below it the grab holds.
    const Fvector feet = m_actor->Position();
    const float fallSpeed = _max(0.f, -Movement()->GetVelocity().y);
    m_missing = fallSpeed > m_options.maxCatchSpeed;
    if (m_missing && !m_hands) { m_reason = "too_fast"; return false; }
    float dip = 0.f;
    if (!m_missing)
    {
        // Falling speed carries the body down under the arms before the pull-up.
        // The edge must stay within arm reach at the bottom of that dip, and the
        // dip must not turn the move into another action or route.
        dip = _min(m_options.catchMaxDrop, fallSpeed*m_options.catchDropPerSpeed);
        const float slack = ReachSlack(m_candidate);
        dip = _min(dip, _max(0.f, slack));
        // Short of the edge by no more than a wall step: the foot pushing off the
        // face below lifts the body into reach (a negative dip).
        if (slack < 0.f) dip = slack;
        if (_abs(dip) > EPS)
        {
            Fvector bottom = feet; bottom.y -= dip;
            Candidate dipped = m_candidate;
            dipped.origin = bottom; dipped.rise += dip; dipped.topTime = 0.f;
            // A lift may turn a climb into a mantle (Lua plays the started action).
            if (Sweep(feet, bottom, Movement()->BoxID()) && FindLanding(dipped) &&
                (dip < 0.f || !strcmp(dipped.action,m_candidate.action)) && !strcmp(dipped.route,m_candidate.route) &&
                (dipped.topTime>0.f)==(m_candidate.topTime>0.f))
            {
                SmoothPath(dipped);
                seconds *= dipped.distances.back() / _max(EPS, m_candidate.distances.back());
                m_candidate = dipped;
            }
            else if (dip < 0.f) { m_hands=false; m_reason = "not_in_reach"; return false; }
            else dip = 0.f;
        }
        m_wallBelow = WallBelow(m_candidate);
        PlanGrip();
        for (size_t i = 1; i < m_candidate.path.size(); ++i)
            if (!Sweep(m_candidate.path[i - 1], m_candidate.path[i], m_candidate.pathBoxes[i]))
            { m_hands=false; m_reason = "path_changed"; return false; }
    }
    // Up to about knee height a top is simply stepped onto: feet, no hands (a
    // presenting script decides that itself).
    if (!presented && m_hands && !m_missing && StepUp(m_candidate)) m_hands = false;
    m_startFeet = feet;
    // The body plays an authored clip when the mod ships them. The requested duration
    // (pace, load and fatigue settings) is the clip's part after the hands land; the
    // reach before it keeps the clip's own proportion. The hands let go when the
    // clip's do (both views).
    m_clip = nullptr;
    if (!m_missing)
    {
        m_clipPerformed = false; m_clipCamera = 0;
        if (!presented) SelectClip(m_candidate.airborne || Airborne());
        else if (m_presentedName.size())
        {
            // Played from a copy: the next Present() (a retry while this clip mixes out)
            // must not change the clip in play.
            m_playingName = m_presentedName; m_playing = m_presented; m_playing.name = m_playingName.c_str();
            m_clip = &m_playing; PrepareClip();
        }
    }
    if (m_clip)
    {
        float from, contact, end;
        ClipFrames(from, contact, end);
        m_clipReach = (contact - from)*seconds/(end - contact);
        m_clipRate = (end - contact)/ClipFps/seconds;
        if (m_clip->contact && m_clip->release > m_clip->contact)
        {
            m_contactRelease = clampr(float(m_clip->release - m_clip->contact)/(end - m_clip->contact),
                m_contactFull + .02f, .95f);
            m_contactEnd = _min(1.f, m_contactRelease + .1f);
        }
    }
    for (auto& leg : m_bodyLegs) { leg.planted = leg.done = leg.onTop = leg.kneel = false; leg.swing = 1.f; }
    m_catchOrigin = feet; m_catchDrop = dip; m_catchSpeed = fallSpeed;
    // Where the guided move leaves the actor: the landing, or the release point of a drop.
    m_fitLanding = m_candidate.path.empty() ? m_candidate.landing : m_candidate.path.back();
    m_reaching = m_hands || _abs(dip) > EPS || (m_clip && m_clip->contact);
    m_handConfirmed = false; m_reachTime = 0.f;
    if (!m_restorePending) m_savedBox = Movement()->BoxID();
    m_savedGravity = Movement()->AppliesGravity();
    if (!m_missing)
    {
        if (!m_reaching) ApplyStance(m_candidate.pathBoxes[1]);
        Movement()->SetApplyGravity(FALSE);
        Movement()->SetVelocity(0.f, 0.f, 0.f);
        m_actor->mstate_wishful &= ~mcJump;
        m_actor->mstate_real &= ~(mcJump | mcFall | mcSprint);
    }
    m_elapsed = 0.f; m_duration = seconds;
    g_parkourDiag.startRise = m_candidate.rise; g_parkourDiag.startFeetY = feet.y;
    g_parkourDiag.reachError = g_parkourDiag.reachBest = g_parkourDiag.reachExcess = -1.f;
    g_parkourDiag.reachFrames = 0; g_parkourDiag.startTime = Device.dwTimeGlobal;
    m_forwardHeld=m_options.forwardHeld;
    m_active = true; m_candidate.valid = false;
    m_lastPosition = feet;
    if (!m_missing) BeginCamera();
    m_reason = m_missing ? "slipping" : "moving";
    return true;
}

void CActorParkour::CaptureHands()
{
    const Fvector down = {0.f, -1.f, 0.f};
    Fvector right; right.set(m_candidate.direction.z, 0.f, -m_candidate.direction.x);
    Fvector center=m_candidate.lip, centerHit, planeNormal;
    center.y+=.12f;
    if(!Ray(center,down,.25f,centerHit,planeNormal)||planeNormal.y<cosf(m_options.maxSlope))
    {m_contactValid[0]=m_contactValid[1]=false;m_hands=false;return;}
    for (int i = 0; i < 2; ++i)
    {
        m_contactValid[i] = false;
        // Prefer the same bottom ledge for both hands. Bring a contact inward
        // around a post/frame rather than attaching the wrist to unrelated walls.
        const float halfSpan=m_options.handSpacing*.5f;
        const float spans[] = {halfSpan,halfSpan*.85f,halfSpan*.65f,halfSpan*.45f,halfSpan*.25f,m_options.minHandSpacing*.5f};
        const float depth=m_options.handDepth;
        const float depths[] = {depth,depth*.5f,0.f,depth*1.5f,depth*2.f,depth*3.f,depth*4.f};
        // Try the full useful span at several depths first. At a convex roof
        // corner each hand can reach its own side instead of collapsing together.
        for (float span : spans)
        {
            if (span < m_options.minHandSpacing*.5f) continue;
            for (float depth : depths)
            {
                Fvector from;
                from.mad(m_candidate.lip, right, i == 0 ? -span : span);
                from.mad(m_candidate.direction,depth);
                const float planeY=centerHit.y-(planeNormal.x*(from.x-centerHit.x)+
                    planeNormal.z*(from.z-centerHit.z))/planeNormal.y;
                from.y=planeY+.10f;
                if (Ray(from, down, .20f, m_contact[i], m_normal[i]) && m_normal[i].y >= cosf(m_options.maxSlope) &&
                    _abs(m_contact[i].y-planeY) <= .08f &&
                    _abs(m_contact[i].y-m_candidate.lip.y) <= .22f)
                { m_contactValid[i] = true; break; }
            }
            if(m_contactValid[i]) break;
        }
    }
    if (m_contactValid[0] != m_contactValid[1])
    {
        const int missing=m_contactValid[0] ? 1 : 0, supported=1-missing;
        Fvector from=m_contact[supported];
        from.mad(right,missing==0 ? -m_options.minHandSpacing : m_options.minHandSpacing);
        const float planeY=centerHit.y-(planeNormal.x*(from.x-centerHit.x)+
            planeNormal.z*(from.z-centerHit.z))/planeNormal.y;
        from.y=planeY+.10f;
        m_contactValid[missing]=Ray(from,down,.20f,m_contact[missing],m_normal[missing]) &&
            m_normal[missing].y>=cosf(m_options.maxSlope) &&
            _abs(m_contact[missing].y-planeY)<=.08f &&
            _abs(m_contact[missing].y-m_candidate.lip.y)<=.22f;
    }
    for (int i=0;i<2;++i)
    {
        m_handDirection[i] = m_candidate.direction;
        if (m_contactValid[i])
        {
            // The near face beneath each palm supplies its own inward direction
            // at a convex corner; the top normal supplies the independent tilt.
            Fvector from=m_contact[i], hit, faceNormal;
            const float reach=m_options.handSpacing+m_options.handDepth;
            from.mad(m_candidate.direction,-reach);from.y-=.03f;
            if (Ray(from,m_candidate.direction,reach+.01f,hit,faceNormal) && _abs(faceNormal.y)<.6f)
            {
                m_handDirection[i].set(-faceNormal.x,0.f,-faceNormal.z);
                m_handDirection[i].normalize_safe();
            }
        }
    }
    // The shared exertion clip requires two plausible contacts. Without them
    // hide it instead of leaving one unsupported arm in the authored high pose.
    if (!HandContactsReady()) m_hands = false;
}

bool CActorParkour::HandTarget(bool left, Fvector& position, Fvector& normal) const
{
    const int i = left ? 0 : 1;
    if (!m_active || !m_hands || !m_contactValid[i]) return false;
    position = m_contact[i]; normal = m_normal[i];
    position.y -= m_options.handsToLedgeHeightOffset;
    return true;
}

float CActorParkour::Progress() const { return clampr(m_elapsed / m_duration, 0.f, 1.f); }
float CActorParkour::HandWeight() const
{
    if (!m_active || !m_hands) return 0.f;
    if(m_reaching) return clampr((m_reachTime + Device.fTimeDelta)/.035f,0.f,1.f);
    // Planted hands keep full weight; HandRelease() moves them away after push-off
    // instead of fading back to the clip's own (raised) pose mid-traversal.
    const float t = Progress();
    return m_handConfirmed ? 1.f : Smooth(clampr((t - m_contactStart) / (m_contactFull - m_contactStart), 0.f, 1.f));
}

float CActorParkour::HandRelease() const
{
    if (!m_active || m_reaching) return 0.f;
    return Smooth(clampr((Progress() - m_contactRelease) / (m_contactEnd - m_contactRelease), 0.f, 1.f));
}

float CActorParkour::HandRelease(int side) const
{
    // Standing up, one hand lets go a moment before the other. Only ever earlier
    // than planned: the plan is where the arms can no longer reach.
    if (!m_active || m_reaching) return 0.f;
    const float early = side == 1 ? .06f : 0.f;
    return Smooth(clampr((Progress() + early - m_contactRelease) / (m_contactEnd - m_contactRelease), 0.f, 1.f));
}

Fvector CActorParkour::AtDistance(float distance) const
{
    const auto& c = m_candidate;
    for (size_t i = 1; i < c.path.size(); ++i)
        if (distance <= c.distances[i])
        {
            Fvector result;
            const float span = c.distances[i] - c.distances[i - 1];
            result.lerp(c.path[i - 1], c.path[i], span > EPS ? (distance - c.distances[i - 1]) / span : 1.f);
            return result;
        }
    return c.path.back();
}

void CActorParkour::ConfirmHandContact(bool ready)
{
    // Visual only: the rendered hands have landed, so the pull may begin. The
    // grab itself was already decided in world space by Start().
    if (m_active && m_reaching && !m_missing && ready) m_handConfirmed = true;
}

void CActorParkour::Update(float dt)
{
    if (!m_active) return;
    if (!Available()) { Cancel(m_reason); return; }
    if (!_valid(dt) || dt <= 0.f) return;

    if (m_perform)
    {
        // The move sets the pace (the derivative of the clip's eased travel, so both
        // cover the same distance) and the character controller walks the body at it
        // (its speed limit is a tenth of the input), so ground, steps and walls are
        // handled as in a walk. A velocity set here would be stopped by the controller
        // braking a body nobody steers.
        m_elapsed = _min(m_duration, m_elapsed + dt);
        const float u = Progress();
        const float speed = m_performDistance/m_duration*6.f*u*(1.f - u);
        m_performAccel.set(m_performDir.x*speed*10.f, 0.f, m_performDir.z*speed*10.f);
        m_lastPosition = m_actor->Position();
        if (m_elapsed >= m_duration) Cancel("complete");
        return;
    }

    if(m_reaching)
    {
        m_reachTime+=dt;
        // Too fast to hold: the hands tried once, physics keeps the fall.
        if (m_missing) { if (m_reachTime>=m_options.reachTimeout) Cancel("slipped"); return; }
        if (m_actor->Position().distance_to(m_lastPosition) > .4f) { Cancel("external_move"); return; }
        // Caught: the body decelerates under the arms (ease-out), then pulls up
        // from the bottom of the dip, where the validated path begins.
        const float brake=_max(.05f,m_options.catchBrakeTime);
        const float u=clampr(m_reachTime/brake,0.f,1.f);
        Fvector position=m_catchOrigin;
        position.y-=m_catchDrop*(2.f*u-u*u);
        if (!Sweep(m_lastPosition,position,Movement()->BoxID())) { Cancel("obstructed");return; }
        Movement()->SetVelocity(0.f,0.f,0.f);
        Movement()->SetPosition(position);m_actor->Position()=position;
        m_actor->spatial_move();m_lastPosition=position;
        // Wait for the rendered hands to land; never longer than the reach timeout.
        // With a clip the body's hands land on the clip's own timing; the first-person
        // hands are pinned to the edge by their IK and never hold the body back.
        if (u<1.f || (m_hands && !m_clip && !m_handConfirmed && m_reachTime<m_options.reachTimeout) ||
            (m_clip && m_reachTime < m_clipReach)) return;
        m_reaching=false;m_handConfirmed=true;
        ApplyStance(m_candidate.pathBoxes[1]);
        return;
    }

    if (m_actor->Position().distance_to(m_lastPosition) > .4f)
    { Cancel("external_move"); return; }

    const Fvector current = m_actor->Position();
    const float previousProgress = Progress();
    const float previous = Guided(previousProgress) *
        m_candidate.distances.back();
    const bool stopOnTop=m_candidate.topTime>0.f && !m_forwardHeld && previous<=m_candidate.topTime;
    m_elapsed = _min(m_duration, m_elapsed + dt);
    const float currentProgress = Progress();
    float distance = Guided(currentProgress) *
        m_candidate.distances.back();
    const bool reachedTop=stopOnTop && distance>=m_candidate.topTime;
    if(reachedTop) distance=m_candidate.topTime;
    Fvector position = current;

    {
        position = m_lastPosition;
        // Follow every intervening validated edge (never cut across a corner on lag).
        for (size_t i = 1; i < m_candidate.path.size(); ++i)
        {
            if (m_candidate.distances[i] <= previous) continue;
            const Fvector next = m_candidate.distances[i] < distance ? m_candidate.path[i] : AtDistance(distance);
            if (!Sweep(position, next, m_candidate.pathBoxes[i]))
            {
                Movement()->SetPosition(position); m_actor->Position() = position;
                m_actor->spatial_move();
                Cancel("obstructed"); return;
            }
            ApplyStance(m_candidate.pathBoxes[i]);
            position = next;
            if (m_candidate.distances[i] >= distance) break;
        }
    }

    Movement()->SetVelocity(0.f, 0.f, 0.f);

    Movement()->SetPosition(position); m_actor->Position() = position;
    m_actor->spatial_move(); m_lastPosition = position;
    if (reachedTop || m_elapsed >= m_duration)
    {
        if(reachedTop)
        {
            m_candidate.landing=m_candidate.topLanding;m_candidate.box=m_candidate.topBox;m_candidate.freeRelease=false;
            m_fitLanding=m_candidate.landing;
            // Report only work actually performed when Forward was released.
            // Invert Guided so a long frame cannot charge the abandoned crossing.
            float low=0.f, high=1.f;
            const float completed=distance/m_candidate.distances.back();
            for(int i=0;i<16;++i)
            { const float mid=(low+high)*.5f; if(Guided(mid)<completed) low=mid; else high=mid; }
            m_elapsed=m_duration*(low+high)*.5f;
        }
        else if(m_candidate.topTime>0.f) m_candidate.route="over";
        Fvector velocity = {0.f,0.f,0.f};
        // Onto a top the stride carries on only while Forward is held (it is a stop otherwise).
        if (strcmp(m_candidate.route,"top") != 0 || m_forwardHeld)
        {
            // Preserve approach direction and a restrained running carry-through.
            // Only horizontal approach contributes; positive jump velocity is ignored.
            const float forward = _max(0.f, m_candidate.velocity.dotproduct(m_candidate.direction));
            const float carry=(forward+clampr((forward-2.f)*.12f,0.f,.6f))*
                m_options.momentum;
            velocity=m_candidate.direction;velocity.mul(carry);
        }
        Cancel("complete");
        Movement()->SetVelocity(velocity);
    }
}

void CActorParkour::Cancel(LPCSTR reason)
{
    m_reason = reason; m_candidate.valid = false;
    if (!m_active) return;
    ParkourLogDiagnostics(*this,"end");
    EndCamera();
    m_aimReady = false;
    // A slip never owned the body: keep its fall instead of freezing it mid-air.
    // A slip never owned the body, and a performed move only steered it: physics keeps
    // what it has (a fall, the end of a roll).
    const bool passive = m_missing || m_perform;
    m_reaching=m_missing=m_perform=false;
    m_active = false; m_hands = false;
    m_restorePending = Movement() && Movement()->BoxID() != m_savedBox;
    if (Movement() && Movement()->BoxID() != 0) m_actor->mstate_real |= mcCrouch;
    if (Movement())
    {
        if (!passive) Movement()->SetVelocity(0.f, 0.f, 0.f);
        Movement()->SetApplyGravity(m_savedGravity);
    }
    UpdateStance();
}

void CActorParkour::UpdateStance()
{
    if (m_active || !m_restorePending || !Movement() || !m_actor->g_Alive()) return;
    if (Sweep(m_actor->Position(), m_actor->Position(), m_savedBox))
    {
        ApplyStance(m_savedBox); m_restorePending = false;
    }
}

void CActorParkour::Reset()
{
    EndCamera();
    if (m_active && Movement()) Movement()->SetApplyGravity(m_savedGravity);
    m_active = m_hands = m_restorePending = false;
    m_reaching=m_handConfirmed=m_missing=m_perform=false;
    m_elapsed=0.f;
    StopClip(); m_clip = nullptr;
    m_candidate = Candidate(); m_reason = "idle";
}

CActorParkour::BodyPose CActorParkour::Pose() const
{
    if (!OwnsMovement()) return bodyNone;
    const auto& c = m_candidate;
    // Without hands, a low top is stepped onto with the lead foot; a low fence or
    // rail is hopped over.
    if (!m_hands && c.rise <= m_options.vaultHeight) return strcmp(c.route,"top") ? bodyHop : bodyStepUp;
    // Hanging and pulling up the legs stay straight; they only walk up a climb's
    // face when there is a wall under the edge to put the feet on.
    const float below = c.lip.y - m_actor->Position().y;
    const bool crouched = Movement() && Movement()->BoxID() != 0;
    if (m_reaching) return m_catchDrop < 0.f ? bodyStep : bodyHang; // a wall kick steps up the face
    if (below > .55f && !crouched)
        return m_wallBelow && c.rise > m_options.mantleHeight ? bodyStep : bodyHang;
    // Knees come up to sit onto the edge, then walk in crouched on a top.
    if (below > -.05f || strcmp(c.route,"top")) return bodyTuck;
    return bodyWalk;
}

namespace
{
    void RotateBone(Fmatrix& bone, const Fvector& from, const Fvector& to, float weight)
    {
        Fvector a = from, b = to;
        if (a.square_magnitude() < EPS_S || b.square_magnitude() < EPS_S) return;
        a.normalize(); b.normalize();
        Fvector axis; axis.crossproduct(a, b);
        const float sine = axis.magnitude(), cosine = a.dotproduct(b);
        if (sine < 1e-4f) return;
        axis.div(sine);
        Fmatrix rotation; rotation.rotation(axis, atan2f(sine, cosine)*weight);
        rotation.transform_dir(bone.i); rotation.transform_dir(bone.j); rotation.transform_dir(bone.k);
    }
}

void CActorParkour::HookBody(bool hook)
{
    IKinematics* model = m_body;
    if (!model) return;
    if (!hook)
    {
        for (auto& arm : m_bodyArms)
            for (u16 bone : arm.bones)
                if (bone != BI_NONE && model->LL_GetBoneInstance(bone).callback() == BodyCallback)
                    model->LL_GetBoneInstance(bone).reset_callback();
        for (auto& leg : m_bodyLegs)
            for (u16 bone : leg.bones)
                if (bone != BI_NONE && model->LL_GetBoneInstance(bone).callback() == LegCallback)
                    model->LL_GetBoneInstance(bone).reset_callback();
        if (m_rootBone != BI_NONE && model->LL_GetBoneInstance(m_rootBone).callback() == RootCallback)
            model->LL_GetBoneInstance(m_rootBone).reset_callback();
        m_rootBone = BI_NONE;
        m_eyeBones[0] = m_eyeBones[1] = BI_NONE; m_bodyEyeHeight = 0.f;
        StopClip();
        for (auto& arm : m_bodyArms) arm = BodyArm();
        for (auto& leg : m_bodyLegs) leg = BodyLeg();
        m_bodyArmLength = m_bodyShoulderHeight = m_bodyHipHeight = m_bodyShoulderWidth = 0.f;
        return;
    }
    for (auto& arm : m_bodyArms) arm = BodyArm();
    m_bodyArmLength = m_bodyShoulderHeight = m_bodyHipHeight = m_bodyShoulderWidth = 0.f;
    Fvector shoulderAt[2] = {{0.f,0.f,0.f}, {0.f,0.f,0.f}};
    float shoulderSum = 0.f, armMin = flt_max;
    int measured = 0;
    xr_vector<Fmatrix> bind;
    model->LL_GetBindTransform(bind);
    const LPCSTR names[2][5] = {
        {"bip01_l_upperarm","bip01_l_forearm","bip01_l_hand","bip01_l_finger1","bip01_l_finger0"},
        {"bip01_r_upperarm","bip01_r_forearm","bip01_r_hand","bip01_r_finger1","bip01_r_finger0"}};
    for (int side = 0; side < 2; ++side)
    {
        BodyArm& arm = m_bodyArms[side];
        u16 ids[5];
        for (int i = 0; i < 5; ++i) ids[i] = model->LL_BoneID(names[side][i]);
        if (ids[0] == BI_NONE || ids[1] == BI_NONE || ids[2] == BI_NONE) continue;
        // Child offsets in each bone's own frame, measured on the bind pose, so
        // no bone axis convention is assumed.
        auto local = [&](u16 bone, u16 child, Fvector& offset)
        {
            Fmatrix inverse; inverse.invert(bind[bone]);
            inverse.transform_tiny(offset, bind[child].c);
            return offset.magnitude();
        };
        arm.upper = local(ids[0], ids[1], arm.axis[0]);
        arm.lower = local(ids[1], ids[2], arm.axis[1]);
        if (arm.upper < .05f || arm.lower < .05f) continue;
        shoulderSum += bind[ids[0]].c.y; armMin = _min(armMin, arm.upper + arm.lower); ++measured;
        shoulderAt[side] = bind[ids[0]].c;
        arm.hand = ids[3] != BI_NONE && ids[4] != BI_NONE &&
            local(ids[2], ids[3], arm.axis[2]) > .01f && local(ids[2], ids[4], arm.thumb) > .01f;
        for (int part = 0; part < 3; ++part)
        {
            if (part == 2 && !arm.hand) break;
            m_bodyBones[side][part] = {this, side, part};
            // Never take over a bone another system already drives.
            auto& instance = model->LL_GetBoneInstance(ids[part]);
            if (instance.callback() && instance.callback() != BodyCallback) break;
            instance.set_callback(bctCustom, BodyCallback, &m_bodyBones[side][part]);
            arm.bones[part] = ids[part];
        }
    }
    // Shoulder joint height and shoulder-to-wrist length (the grip target is the
    // wrist) from the bind pose. An implausible skeleton keeps the MCM reach.
    if (measured)
    {
        const float shoulder = shoulderSum/measured;
        if (shoulder > 1.f && shoulder < 1.9f && armMin > .4f && armMin < .95f)
        { m_bodyShoulderHeight = shoulder; m_bodyArmLength = armMin; }
    }
    // Legs: thigh and calf are solved; the foot keeps its animated angle.
    const LPCSTR legNames[2][3] = {
        {"bip01_l_thigh","bip01_l_calf","bip01_l_foot"},
        {"bip01_r_thigh","bip01_r_calf","bip01_r_foot"}};
    for (int side = 0; side < 2; ++side)
    {
        BodyLeg& leg = m_bodyLegs[side];
        leg = BodyLeg();
        u16 ids[3];
        for (int i = 0; i < 3; ++i) ids[i] = model->LL_BoneID(legNames[side][i]);
        if (ids[0] == BI_NONE || ids[1] == BI_NONE || ids[2] == BI_NONE) continue;
        auto local = [&](u16 bone, u16 child, Fvector& offset)
        {
            Fmatrix inverse; inverse.invert(bind[bone]);
            inverse.transform_tiny(offset, bind[child].c);
            return offset.magnitude();
        };
        leg.upper = local(ids[0], ids[1], leg.axis[0]);
        leg.lower = local(ids[1], ids[2], leg.axis[1]);
        if (leg.upper < .1f || leg.lower < .1f) continue;
        for (int part = 0; part < 2; ++part)
        {
            auto& instance = model->LL_GetBoneInstance(ids[part]);
            if (instance.callback() && instance.callback() != LegCallback) break;
            m_legBones[side][part] = {this, side, part};
            instance.set_callback(bctCustom, LegCallback, &m_legBones[side][part]);
            leg.bones[part] = ids[part];
        }
    }
    // The clip root: placing the whole authored body on the real edge.
    m_rootBone = model->LL_BoneID("bip01");
    if (m_rootBone != BI_NONE)
    {
        auto& instance = model->LL_GetBoneInstance(m_rootBone);
        if (instance.callback() && instance.callback() != RootCallback) m_rootBone = BI_NONE;
        else instance.set_callback(bctCustom, RootCallback, this);
    }
    // Knee height sets what is simply stepped onto; foot -> toe shows which way the
    // skeleton faces, so elbows and knees bend the right way whatever the export.
    m_bodyKneeHeight = 0.f; m_bodyForward = 1.f;
    const u16 calf = model->LL_BoneID("bip01_l_calf");
    if (calf != BI_NONE && bind[calf].c.y > .25f && bind[calf].c.y < .8f) m_bodyKneeHeight = bind[calf].c.y;
    const u16 foot = model->LL_BoneID("bip01_l_foot"), toe = model->LL_BoneID("bip01_l_toe0");
    if (foot != BI_NONE && toe != BI_NONE && _abs(bind[toe].c.z - bind[foot].c.z) > .02f)
        m_bodyForward = bind[toe].c.z > bind[foot].c.z ? 1.f : -1.f;
    // Eyes: the first-person camera height (eye level) and the head a clip moves.
    // The bind pose stands straighter than the upright idle, about 2 cm.
    m_eyeBones[0] = model->LL_BoneID("eye_left"); m_eyeBones[1] = model->LL_BoneID("eye_right");
    m_neckBone = model->LL_BoneID("bip01_neck"); m_headBone = model->LL_BoneID("bip01_head");
    m_bodyEyeHeight = 0.f;
    if (m_eyeBones[0] != BI_NONE && m_eyeBones[1] != BI_NONE)
    {
        const float eye = (bind[m_eyeBones[0]].c.y + bind[m_eyeBones[1]].c.y)*.5f - .02f;
        if (eye > _max(1.2f, m_bodyShoulderHeight) && eye < 2.1f) m_bodyEyeHeight = eye;
    }
    // Hip height sets the vault class; shoulder width sets where the hands go.
    const u16 pelvis = model->LL_BoneID("bip01_pelvis");
    if (m_bodyArmLength > 0.f && pelvis != BI_NONE && bind[pelvis].c.y > .6f && bind[pelvis].c.y < m_bodyShoulderHeight - .2f)
        m_bodyHipHeight = bind[pelvis].c.y;
    if (measured == 2)
    {
        const float width = shoulderAt[0].distance_to(shoulderAt[1]);
        if (width > .2f && width < .6f) m_bodyShoulderWidth = width;
    }
}

void CActorParkour::AttachBody(IKinematics* model)
{
    // A new or rebuilt visual: remember it, and hook it only while enabled.
    m_body = model;
    for (auto& arm : m_bodyArms) arm = BodyArm();
    m_bodyArmLength = m_bodyShoulderHeight = 0.f;
    if (m_enabled) HookBody(true);
}

void CActorParkour::DetachBody(IKinematics* model)
{
    if (!model || model != m_body) return;
    HookBody(false);
    m_body = nullptr;
}

void CActorParkour::SetEnabled(bool enabled)
{
    if (enabled == m_enabled) return;
    if (!enabled)
    {
        if (m_active) Cancel("disabled");
        m_restorePending = false;
        m_bodyLean = 0.f;
    }
    m_enabled = enabled;
    HookBody(enabled);
}

void CActorParkour::BodyCallback(CBoneInstance* bone)
{
    const auto* link = static_cast<const BodyBone*>(bone->callback_param());
    const BodyArm& arm = link->owner->m_bodyArms[link->side];
    if (arm.weight <= .001f) return;
    // Bone callbacks receive model-space matrices that already include the parent.
    // Targets are world space, converted with the transform being rendered now,
    // so a body moving up the wall cannot drag the hands off the edge.
    Fmatrix toModel; toModel.invert(link->owner->m_actor->XFORM());
    Fmatrix& m = bone->mTransform;
    Fvector current;
    m.transform_dir(current, arm.axis[link->part]);
    if (link->part == 2)
    {
        // Fingers forward over the edge, thumb toward the other hand: palm down.
        Fvector fingers, thumb, inward;
        toModel.transform_dir(fingers, arm.fingers);
        RotateBone(m, current, fingers, arm.weight);
        toModel.transform_dir(inward, arm.inward);
        m.transform_dir(fingers, arm.axis[2]); fingers.normalize_safe();
        m.transform_dir(thumb, arm.thumb);
        thumb.mad(fingers, -thumb.dotproduct(fingers));
        inward.mad(fingers, -inward.dotproduct(fingers));
        RotateBone(m, thumb, inward, arm.weight);
        return;
    }
    Fvector target, desired;
    toModel.transform_tiny(target, arm.target);
    if (link->part == 1) desired.sub(target, m.c);
    else
    {
        // Two-bone solve. The elbow bends outward and down, away from the body.
        Fvector toTarget; toTarget.sub(target, m.c);
        float distance = toTarget.magnitude();
        if (distance < EPS) return;
        toTarget.div(distance);
        const float a = arm.upper, b = arm.lower;
        distance = clampr(distance, _abs(a - b) + .001f, a + b - .001f);
        // Elbows out and back: hanging they point down/out, pressing up over the
        // edge they go back like a dip, never forward or flared up.
        const float back = -link->owner->m_bodyForward;
        Fvector pole; pole.set(m.c.x >= 0.f ? .6f : -.6f, -.5f, .8f*back);
        pole.mad(toTarget, -pole.dotproduct(toTarget));
        if (pole.square_magnitude() < 1e-6f) return;
        pole.normalize();
        const float along = (a*a - b*b + distance*distance)/(2.f*distance);
        desired.mul(toTarget, along);
        desired.mad(pole, sqrtf(_max(0.f, a*a - along*along)));
    }
    RotateBone(m, current, desired, arm.weight);
}

void CActorParkour::SelectClip(bool airborne)
{
    // Kind from the route, height band and start (ground or air) by the nearest
    // authored clip; vaults by what is in hand and how much is carried.
    const Candidate& c = m_candidate;
    const bool top = !strcmp(c.route, "top"), window = !strcmp(c.route, "window");
    u8 kind;
    if (top) kind = !m_hands && StepUp(c) ? Clip::step : (c.rise > 1.6f ? Clip::climb : Clip::mantle);
    else if (window) kind = Clip::window;
    else if (!m_hands && c.rise <= .65f) kind = Clip::hop;
    else kind = c.rise > 1.45f ? Clip::over : Clip::vault;
    CInventory& inventory = m_actor->inventory();
    const bool armed = smart_cast<CWeapon*>(inventory.ActiveItem()) != nullptr;
    const float maxWeight = m_actor->MaxCarryWeight();
    const float load = maxWeight > EPS ? clampr(inventory.TotalWeight()/maxWeight, 0.f, 1.f) : 0.f;
    const u8 style = armed ? Clip::oneHand : (load > m_options.heavyLoad ? Clip::steps : Clip::speed);
    const Clip* best = nullptr;
    float bestScore = flt_max;
    for (const Clip& clip : g_clips)
    {
        if (clip.kind != kind) continue;
        float score = _abs(clip.height - c.rise);
        // In the air, `rise` is what is left to the feet. Clips for that start fit
        // best; a ground clip of the right height is closer than a much taller air
        // clip (a low top reached in a jump is not knelt onto). On the ground an
        // air clip never fits: it starts with the feet off the ground.
        if (clip.start == Clip::air && !airborne) score += 2.f;
        else if (clip.start == Clip::ground && airborne) score += .25f;
        if (kind == Clip::climb && airborne && clip.wall != m_wallBelow) score += 1.f;
        if (clip.style != Clip::anyStyle && clip.style != style) score += armed ? 3.f : .45f;
        if (score < bestScore) { best = &clip; bestScore = score; }
    }
    m_clip = best;
    if (m_clip) PrepareClip();
}

void CActorParkour::PrepareClip()
{
    const Candidate& c = m_candidate;
    // Lead with the leg on the edge's side; straight on, alternate.
    Fvector right = {c.direction.z, 0.f, -c.direction.x}, toLip;
    toLip.sub(c.lip, m_startFeet);
    const float lateral = toLip.dotproduct(right);
    m_clipMirror = m_clip->mirror && (lateral < -.06f || (lateral <= .06f && (m_clipCount++ & 1)));
    m_clipOrigin.identity();
    m_clipOrigin.k.set(c.direction.x, 0.f, c.direction.z); m_clipOrigin.k.normalize_safe();
    m_clipOrigin.j.set(0.f, 1.f, 0.f);
    m_clipOrigin.i.crossproduct(m_clipOrigin.j, m_clipOrigin.k);
    m_clipOrigin.c = m_startFeet;
    m_clipShift.set(lateral, c.lip.y - m_startFeet.y - m_clip->height,
        toLip.dotproduct(m_clipOrigin.k) - m_clip->face);
}

void CActorParkour::ClipFrames(float& from, float& contact, float& end) const
{
    // A scripted clip may skip its set-off (lead) and be done before its last frame
    // (finish): the reach plays [from, contact], the guided move [contact, end].
    end = m_clip->finish ? clampr(float(m_clip->finish), 1.f, float(m_clip->frames)) : float(m_clip->frames);
    from = m_clip->contact ? _min(float(m_clip->lead), _max(0.f, m_clip->contact - 2.f)) : _min(float(m_clip->lead), end*.5f);
    contact = _max(float(m_clip->contact), from);
    end = _max(end, contact + 1.f);
}

float CActorParkour::ClipTime() const
{
    // Reaching plays the clip up to the hands' contact; the guided move the rest.
    float from, contact, end;
    ClipFrames(from, contact, end);
    if (m_reaching) return (from + (contact - from)*clampr(m_reachTime/_max(EPS, m_clipReach), 0.f, 1.f))/ClipFps;
    return (contact + (end - contact)*Progress())/ClipFps;
}

void CActorParkour::StopClip()
{
    m_clipPlaying = m_clipFading = false;
    m_clipWeight = 0.f;
    for (CBlend*& blend : m_clipBlends) blend = nullptr;
}

bool CActorParkour::ClipEye(Fvector& eye) const
{
    if (!m_clipPlaying || !m_body || m_eyeBones[0] == BI_NONE || m_eyeBones[1] == BI_NONE) return false;
    m_body->CalculateBones(TRUE);
    eye.add(m_body->LL_GetTransform(m_eyeBones[0]).c, m_body->LL_GetTransform(m_eyeBones[1]).c).mul(.5f);
    m_actor->XFORM().transform_tiny(eye);
    if (!_valid(eye)) return false;
    if (m_clipCamera == 2 && m_neckBone != BI_NONE && m_headBone != BI_NONE && Device.dwFrame != m_eyeFrame)
    {
        // The neck-to-head line's turn about the move's right axis, unwrapped: a full
        // forward roll is one turn of the view.
        Fvector up;
        up.sub(m_body->LL_GetTransform(m_headBone).c, m_body->LL_GetTransform(m_neckBone).c);
        m_actor->XFORM().transform_dir(up);
        const float raw = atan2f(up.dotproduct(m_clipOrigin.k), up.y);
        if (!m_pitchValid) { m_pitchRaw = raw; m_clipPitch = 0.f; m_pitchValid = true; }
        float step = raw - m_pitchRaw;
        while (step > PI) step -= PI_MUL_2;
        while (step < -PI) step += PI_MUL_2;
        m_clipPitch += step; m_pitchRaw = raw;
    }
    // A light low-pass (30 ms): the view follows the head, not each jolt of a bone.
    if (Device.dwFrame != m_eyeFrame)
    {
        m_eyeFrame = Device.dwFrame;
        const float follow = m_eyeSmoothValid ? 1.f - expf(-_max(0.f, Device.fTimeDelta)/.03f) : 1.f;
        m_eyeSmooth.lerp(m_eyeSmooth, eye, follow); m_eyeSmoothValid = true;
    }
    eye = m_eyeSmooth;
    return true;
}

Fvector CActorParkour::SteadyCameraPush(const Fvector& free, const Fvector& pushed, float dt)
{
    Fvector push; push.sub(pushed, free);
    if (ClipWeight() <= 0.f || !_valid(push)) { m_cameraPush.set(0.f, 0.f, 0.f); return pushed; }
    if (push.square_magnitude() >= m_cameraPush.square_magnitude()) m_cameraPush = push;
    else m_cameraPush.lerp(m_cameraPush, push, 1.f - expf(-_max(0.f, dt)/.25f));
    Fvector steady; steady.add(free, m_cameraPush);
    return steady;
}

float CActorParkour::CameraFactor(float stock) const
{
    if (!m_enabled || !m_eyeLevel || m_bodyEyeHeight <= 0.f || !Movement()) return stock;
    constexpr float cameraShift = .05f; // CActor::CameraHeight
    Fvector standing; Movement()->Boxes()[0].getsize(standing);
    const float usable = standing.y - cameraShift;
    // Never above the top of the collision box (the camera would see through ceilings).
    return usable > EPS ? _min(m_bodyEyeHeight, standing.y - .04f)/usable : stock;
}

float CActorParkour::ClipMix(IKinematicsAnimated* animated) const
{
    // The clip's share of the mix X-Ray computes (weights normalized per part); a
    // blend the animation system already freed or reused is no longer the clip's.
    float clip = 0.f, total = 0.f;
    for (u16 part = 0; part < MAX_PARTS; ++part)
    {
        if (!m_clipBlends[part]) continue;
        for (u32 i = 0, count = animated->LL_PartBlendsCount(part); i < count; ++i)
        {
            const CBlend* blend = animated->LL_PartBlend(part, i);
            if (!blend) continue;
            total += blend->blendAmount;
            if (blend == m_clipBlends[part] && blend->motionID == m_clipMotion) clip += blend->blendAmount;
        }
    }
    return total > EPS ? clampr(clip/total, 0.f, 1.f) : 0.f;
}

void CActorParkour::UpdateClip(float dt)
{
    // Mixing rates in and out of the stock cycles (1/seconds), chosen per move.
    const float BlendIn = m_blendIn, BlendOut = m_blendOut;
    IKinematicsAnimated* animated = m_actor->Visual() ? m_actor->Visual()->dcast_PKinematicsAnimated() : nullptr;
    if (!m_enabled || !animated || m_rootBone == BI_NONE) { StopClip(); return; }
    const bool moving = m_clip && OwnsMovement();
    if (moving && (!m_clipPlaying || m_clipFading))
    {
        string64 name;
        xr_strcpy(name, m_clip->name);
        if (m_clipMirror) xr_strcat(name, "_m");
        const MotionID motion = animated->ID_Cycle_Safe(name);
        if (!motion.valid()) { m_clip = nullptr; StopClip(); return; } // the mod's motions are not installed
        for (u16 part = 0; part < MAX_PARTS; ++part)
            m_clipBlends[part] = animated->partitions().part(part) ?
                animated->LL_PlayCycle(part, motion, TRUE, BlendIn, BlendIn, 0.f, TRUE, nullptr, nullptr) : nullptr;
        m_clipMotion = motion;
        m_clipPlaying = true; m_clipFading = false; m_clipPlayOut = false;
        m_eyeSmoothValid = m_pitchValid = false; m_clipPitch = 0.f;
    }
    if (!m_clipPlaying || !m_clip) return;
    // The move is over: a finished move plays the clip's remaining frames (the settle)
    // at the move's pace while the stock cycles mix back in; a stopped or cancelled
    // one holds its pose while it mixes out.
    if (!moving && !m_clipFading)
    {
        m_clipFading = true;
        m_clipPlayOut = m_reason && !xr_strcmp(m_reason, "complete");
    }
    m_clipWeight = ClipMix(animated);
    if (m_clipFading && m_clipWeight <= 0.f) { StopClip(); return; }
    if (!m_clipFading) m_clipTime = ClipTime();
    else if (m_clipPlayOut && _valid(dt) && dt > 0.f)
        m_clipTime = _min(m_clipTime + dt*m_clipRate, m_clip->frames/ClipFps);
    // The clip is authored against its own edge. Before the hands land the feet stay on
    // the real ground (the edge offset is eased in along the approach and, vertically,
    // between contact and push-off); an airborne catch is at the real edge from its
    // first frame. After the push-off it eases onto the real landing.
    const float frame = m_clipTime*ClipFps;
    float from, contact, end;
    ClipFrames(from, contact, end);
    if (m_clipPerformed)
    {
        // At the actor, shifted back by the distance the move should have covered by
        // now, so the clip's own travel lands on it (and stays with it when blocked).
        m_clipWorld = m_clipOrigin;
        m_clipWorld.c.mad(m_actor->Position(), m_clipOrigin.k, -m_performDistance*Smooth(Progress()));
    }
    float along = 1.f, lift = 1.f;
    if (m_clip->start != Clip::air)
    {
        if (m_clip->contact)
        {
            along = Smooth(clampr((frame - from)/_max(1.f, m_clip->contact - from), 0.f, 1.f));
            lift = Smooth(clampr((frame - m_clip->contact)/_max(1.f, float(m_clip->release - m_clip->contact)), 0.f, 1.f));
        }
        else
        {
            along = Smooth(clampr((frame - from)/_max(1.f, (end - from)*.4f), 0.f, 1.f));
            lift = Smooth(clampr((frame - from)/_max(1.f, (end - from)*.5f), 0.f, 1.f));
        }
    }
    Fvector shift = m_clipShift;
    if (m_clip->fit && !m_clipPerformed)
    {
        // The clip's feet end where the move really leaves the actor: the far ground's
        // height (a vault, a window into a raised floor, the release point of a drop) and,
        // on a top, where the feet stop, instead of where the clip put them.
        const float start = m_clip->contact ? _max(float(m_clip->release), contact) : (from + end)*.5f;
        const float fit = Smooth(clampr((frame - start)/_max(1.f, end - start), 0.f, 1.f));
        Fvector toLanding; toLanding.sub(m_fitLanding, m_clipOrigin.c);
        shift.y += clampr(toLanding.y - m_clip->land - m_clipShift.y, -1.2f, 1.2f)*fit;
        if (m_clip->ahead >= 0.f)
            shift.z += clampr(toLanding.dotproduct(m_clipOrigin.k) - m_clip->ahead - m_clipShift.z, -.4f, .4f)*fit;
    }
    if (!m_clipPerformed)
    {
        m_clipWorld = m_clipOrigin;
        m_clipWorld.c.mad(m_clipOrigin.i, shift.x*along);
        m_clipWorld.c.mad(m_clipOrigin.k, shift.z*along);
        m_clipWorld.c.y += shift.y*lift;
    }
    // Time is the move's, not the blend's own clock.
    for (CBlend* blend : m_clipBlends)
        if (blend && blend->motionID == m_clipMotion && blend->blend_state() != CBlend::eFREE_SLOT)
        {
            blend->speed = 0.f;
            blend->timeCurrent = clampr(m_clipTime, 0.f, _max(0.f, blend->timeTotal - END_EPS));
            if (m_clipFading) { blend->set_falloff_state(); blend->blendFalloff = BlendOut; }
        }
}

void CActorParkour::ClipAdjust(Fmatrix& adjust) const
{
    // Bone poses mix linearly by weight, so the root moves between the actor's own
    // transform and the clip's by the same weight: either pose stays where it belongs
    // in the world while they cross-fade. Made against the transform the bones are
    // placed with right now (the actor may have moved since the clip's update), so the
    // body and the eyes never shake with the guided movement.
    Fmatrix toModel, full;
    toModel.invert(m_actor->XFORM());
    full.mul_43(toModel, m_clipWorld);
    Fquaternion rotation, none, blended;
    rotation.set(full); none.identity();
    blended.slerp(none, rotation, m_clipWeight);
    adjust.rotation(blended);
    adjust.c.mul(full.c, m_clipWeight);
}

void CActorParkour::RootCallback(CBoneInstance* bone)
{
    const auto* self = static_cast<const CActorParkour*>(bone->callback_param());
    if (!self->m_clipPlaying) return;
    Fmatrix adjust;
    self->ClipAdjust(adjust);
    bone->mTransform.mulA_43(adjust);
}

void CActorParkour::UpdateBody(float dt)
{
    if (!m_enabled || !m_body || !_valid(dt) || dt <= 0.f) return;
    UpdateClip(dt);
    const Fvector up = {0.f, 1.f, 0.f};
    Fvector right; right.set(m_candidate.direction.z, 0.f, -m_candidate.direction.x);
    // Hands go on the edge for every move except a low hop (no HUD hands either).
    const bool grip = m_active && (m_hands || m_candidate.rise > m_options.vaultHeight);
    // Reach up fast, hold the edge planted until the push-off, then let go.
    const float reach = m_reaching ? Smooth(clampr(m_reachTime/.2f, 0.f, 1.f)) : 1.f;
    // With a clip the arms are animated; the IK only pins its planted hands onto the
    // real contacts, from two frames before they land until they let go.
    float clipHold = 0.f;
    const bool clip = ClipPlaying() && m_clip;
    if (clip && m_clip->contact)
    {
        const float frame = ClipTime()*ClipFps;
        clipHold = Smooth(clampr((frame - (m_clip->contact - 2))/2.f, 0.f, 1.f))*
            (1.f - Smooth(clampr((frame - m_clip->release)/2.f, 0.f, 1.f)));
    }
    bool reached = true;
    for (int side = 0; side < 2; ++side)
    {
        BodyArm& arm = m_bodyArms[side];
        const bool holding = grip && arm.bones[1] != BI_NONE && m_contactValid[side];
        if (holding)
        {
            // Palm flat on the top, fingers forward.
            const Fvector& facing = m_handDirection[side];
            arm.target = GripPoint(side);
            arm.fingers = facing; arm.fingers.mad(up, -.1f); arm.fingers.normalize_safe();
            arm.inward = right;
            if (side == 1) arm.inward.invert();
        }
        const float goal = !holding ? 0.f : clip ? clipHold : reach*(1.f - HandRelease(side));
        arm.weight += (goal - arm.weight)*(1.f - expf(-dt/(goal > arm.weight ? .05f : .12f)));
        if ((m_handMask & (1 << side)) && (!holding || arm.weight < .9f)) reached = false;
    }
    // The same posture the route was validated with (FitBody, PlanGrip).
    const float lean = OwnsMovement() && !clip && !m_perform ?
        PostureLean(m_candidate, m_candidate.lip.y - m_actor->Position().y, grip) : 0.f;
    m_bodyLean += (lean - m_bodyLean)*(1.f - expf(-dt/.12f));
    if (clip)
        for (auto& leg : m_bodyLegs) { leg.weight *= expf(-dt/.08f); leg.planted = false; }
    else
        UpdateLegs(dt);
    // Without HUD parkour hands (third person, or no hands clip playing) the body's
    // grip times the pull; otherwise the rendered hands confirm their landing.
    const bool hudHands = m_actor->cam_active == eacFirstEye && g_player_hud && g_player_hud->ParkourMotionPlaying();
    if (m_reaching && m_hands && !m_missing && (reached || clip) && !hudHands)
        ConfirmHandContact(true);
}

bool CActorParkour::StepUp(const Candidate& c) const
{
    // A top no higher than about the knee is stepped onto, not climbed with hands.
    const float knee = m_bodyKneeHeight > 0.f ? m_bodyKneeHeight : .5f;
    return !strcmp(c.route, "top") && c.rise <= knee*1.15f;
}

float CActorParkour::PostureLean(const Candidate& c, float below, bool grip) const
{
    // Hanging, the chest leans in to the wall; pressing up over the edge the torso
    // bends further forward, most with the chest at the edge, upright on top.
    // Under a low lintel/ceiling the body stays upright throughout.
    if (c.upright) return 0.f;
    const float press = Smooth(clampr(1.f - _abs(below - .85f)/.75f, 0.f, 1.f));
    if (!grip) return .25f*press;
    return below > .85f ? HangLean + (PressLean - HangLean)*press : PressLean*press;
}

float CActorParkour::ShoulderAt(u32 box) const
{
    // Shoulder height above the feet in a stance, lowered with the stance height.
    const Fbox& stand = Movement()->Boxes()[0];
    const Fbox& shape = Movement()->Boxes()[box];
    const float scale = (shape.max.y - shape.min.y)/_max(EPS, stand.max.y - stand.min.y);
    if (m_bodyShoulderHeight > 0.f) return m_bodyShoulderHeight*scale;
    constexpr float shoulderBelowEye = .20f, cameraShift = .05f;
    return (CameraFactor(m_actor->m_fCamHeightFactor)*(stand.max.y - stand.min.y - cameraShift) - shoulderBelowEye)*scale;
}

Fvector CActorParkour::GripPoint(int side) const
{
    // The wrist just behind and above the palm contact, fingers over the edge.
    Fvector wrist = m_contact[side];
    wrist.mad(m_handDirection[side], -.04f);
    wrist.y += .02f;
    return wrist;
}

void CActorParkour::FitBody(Candidate& c)
{
    // The head as the body really carries it, leaning in while hanging and over
    // the edge while pressing up. Where that would hit a lintel or ceiling, the
    // body stays upright; the capsule sweep has already cleared the upright head.
    c.upright = false;
    for (size_t i = 0; i < c.path.size(); ++i)
    {
        const float lean = PostureLean(c, c.lip.y - c.path[i].y, true);
        if (lean <= .02f) continue;
        const Fbox& shape = Movement()->Boxes()[c.pathBoxes[i]];
        Fvector head = c.path[i];
        head.y += shape.max.y - shape.min.y - HeadRadius - (1.f - cosf(lean))*LeanPivot;
        head.mad(c.direction, sinf(lean)*LeanPivot);
        if (!SphereClear(head, HeadRadius)) { c.upright = true; return; }
    }
}

void CActorParkour::PlanGrip()
{
    // Each hand holds until its arm can no longer reach the grip from where the
    // body is on the path (straight down after pressing up, or left behind as it
    // moves on) and pushes off there: hands never stretch or let go early.
    const auto& c = m_candidate;
    if (m_bodyArmLength <= 0.f || c.path.size() < 2 || c.distances.back() <= 0.f) return;
    const Fvector right = {c.direction.z, 0.f, -c.direction.x};
    const float halfWidth = m_bodyShoulderWidth*.5f;
    bool held = false;
    float fraction = -1.f;
    for (size_t i = 0; i < c.path.size() && fraction < 0.f; ++i)
    {
        const float lean = PostureLean(c, c.lip.y - c.path[i].y, true);
        Fvector center = c.path[i];
        center.y += ShoulderAt(c.pathBoxes[i]) - (1.f - cosf(lean))*ShoulderPivot;
        center.mad(c.direction, sinf(lean)*ShoulderPivot);
        bool reach = true;
        for (int side = 0; side < 2; ++side)
        {
            if (!m_contactValid[side]) continue;
            Fvector shoulder = center;
            shoulder.mad(right, side == 0 ? -halfWidth : halfWidth);
            if (shoulder.distance_to(GripPoint(side)) > m_bodyArmLength) reach = false;
        }
        if (reach) held = true;
        else if (held) fraction = c.distances[i]/c.distances.back();
    }
    if (fraction < 0.f) return;
    // Path fraction to move progress (Update advances Guided(progress) of the path).
    float low = 0.f, high = 1.f;
    for (int i = 0; i < 16; ++i)
    { const float mid = (low + high)*.5f; if (Guided(mid) < fraction) low = mid; else high = mid; }
    m_contactEnd = clampr((low + high)*.5f, m_contactFull + .08f, 1.f);
    m_contactRelease = _max(m_contactFull + .02f, m_contactEnd - .12f);
}

void CActorParkour::LegCallback(CBoneInstance* bone)
{
    const auto* link = static_cast<const BodyBone*>(bone->callback_param());
    const BodyLeg& leg = link->owner->m_bodyLegs[link->side];
    if (leg.weight <= .001f) return;
    Fmatrix toModel; toModel.invert(link->owner->m_actor->XFORM());
    Fmatrix& m = bone->mTransform;
    Fvector target, current, desired;
    toModel.transform_tiny(target, leg.at);
    m.transform_dir(current, leg.axis[link->part]);
    if (link->part == 1) desired.sub(target, m.c);
    else
    {
        // Two-bone solve. The knee bends forward (toward the wall or edge), slightly out.
        Fvector toTarget; toTarget.sub(target, m.c);
        float distance = toTarget.magnitude();
        if (distance < EPS) return;
        toTarget.div(distance);
        const float a = leg.upper, b = leg.lower;
        distance = clampr(distance, _abs(a - b) + .001f, a + b - .001f);
        Fvector pole; pole.set(m.c.x >= 0.f ? .15f : -.15f, 0.f, link->owner->m_bodyForward);
        pole.mad(toTarget, -pole.dotproduct(toTarget));
        if (pole.square_magnitude() < 1e-6f) return;
        pole.normalize();
        const float along = (a*a - b*b + distance*distance)/(2.f*distance);
        desired.mul(toTarget, along);
        desired.mad(pole, sqrtf(_max(0.f, a*a - along*along)));
    }
    RotateBone(m, current, desired, leg.weight);
}

void CActorParkour::UpdateLegs(float dt)
{
    // Feet only go where the body has real footing:
    //  - climb with a wall below: one foot pushes on the face until its leg is
    //    straight, then lets go (open air: the legs hang);
    //  - mantle: the knee comes up and the lead foot steps onto the edge; climb:
    //    the lead knee rests on the edge first; the other foot follows once the
    //    hips are up, then the body stands (the hands let go when they can no
    //    longer reach, see PlanGrip);
    //  - low top: the lead foot steps up, the trailing one stays down until the
    //    hips have moved over, then follows;
    //  - vault: one foot on the top, the other leg swings over to the far side.
    const BodyPose pose = Pose();
    const auto& c = m_candidate;
    const Fvector up = {0.f, 1.f, 0.f}, down = {0.f, -1.f, 0.f};
    const Fvector right = {c.direction.z, 0.f, -c.direction.x};
    const Fvector feet = m_actor->Position();
    const bool top = !strcmp(c.route, "top");
    const bool push = pose == bodyStep;
    const bool onto = (pose == bodyTuck && top) || pose == bodyStepUp;
    const bool over = pose == bodyTuck && !top;
    const bool climb = m_hands && c.rise > m_options.mantleHeight;
    float hipHeight = 0.f;
    if (push || onto || over)
    {
        const Fbox& stand = Movement()->Boxes()[0];
        const Fbox& shape = Movement()->Boxes()[Movement()->BoxID()];
        hipHeight = (m_bodyHipHeight > 0.f ? m_bodyHipHeight : .95f)*
            (shape.max.y - shape.min.y)/_max(EPS, stand.max.y - stand.min.y);
    }
    // Footing on the top just past the edge, under one foot (ankle above the sole).
    auto footingOnTop = [&](float lateral, Fvector& at)
    {
        Fvector from = c.lip, hit, normal;
        from.mad(c.direction, .2f); from.mad(right, lateral); from.y += .3f;
        if (!Ray(from, down, .6f, hit, normal) || normal.y < .7f) return false;
        at = hit; at.y += .08f;
        return true;
    };
    // Move a foot to a new hold with a short lifted swing instead of jumping there.
    auto place = [&](BodyLeg& leg, const Fvector& hold, bool onTop)
    {
        if (leg.planted && leg.hold.distance_to(hold) < .05f) return;
        leg.from = leg.planted ? leg.at : hold;
        leg.swing = leg.planted ? 0.f : 1.f;
        leg.hold = hold; leg.planted = true; leg.onTop = onTop;
    };
    for (int side = 0; side < 2; ++side)
    {
        BodyLeg& leg = m_bodyLegs[side];
        const float lateral = side == 0 ? -.12f : .12f;
        float goal = 0.f;
        if (leg.bones[1] != BI_NONE && (push || onto || over))
        {
            Fvector hip = feet;
            hip.y += hipHeight; hip.mad(right, lateral);
            const float length = leg.upper + leg.lower;
            Fvector hold;
            if (push)
            {
                // One push foot (the left), planted once, released when its leg is straight.
                if (side == 0 && !leg.done)
                {
                    if (!leg.planted)
                    {
                        Fvector from = hip, hit, normal;
                        from.y -= length*.75f; from.mad(c.direction, -.2f);
                        if (from.y < c.lip.y - .2f && Ray(from, c.direction, 1.f, hit, normal) && _abs(normal.y) < .5f)
                        { hit.mad(c.direction, -.1f); place(leg, hit, false); } // toes on the face
                        else leg.done = true;
                    }
                    if (leg.planted && hip.distance_to(leg.hold) > length*.97f) leg.done = true;
                    if (!leg.done) goal = 1.f;
                }
            }
            else if (onto)
            {
                // The lead (right) leg goes up first; the other once the hips are over
                // the edge. Climbing, the lead knee comes onto the edge (kneeling) until
                // the body is up, then its foot. Stepping up, the trailing foot stays
                // where it stood until the hips are over.
                const bool lead = side == 1;
                const bool hipsUp = hip.y > c.lip.y + .45f*length;
                const bool kneel = lead && climb && feet.y < c.lip.y - .1f;
                if (kneel && (leg.kneel || footingOnTop(lateral, hold)))
                {
                    // Thigh toward the knee rest, shin back and down over the edge.
                    if (!leg.kneel) { leg.knee = hold; leg.knee.y -= .02f; }
                    Fvector thigh, shin = c.direction;
                    thigh.sub(leg.knee, hip); thigh.normalize_safe();
                    shin.invert(); shin.y = -.45f; shin.normalize();
                    Fvector foot = hip;
                    foot.mad(thigh, leg.upper); foot.mad(shin, leg.lower);
                    if (!leg.kneel) { place(leg, foot, false); leg.kneel = true; }
                    else leg.hold = foot;
                    goal = 1.f;
                }
                else if (!kneel && (lead || hipsUp) && (leg.onTop || footingOnTop(lateral, hold)))
                {
                    if (!leg.onTop) { place(leg, hold, true); leg.kneel = false; }
                    if (hip.distance_to(leg.hold) <= length*.98f) goal = 1.f;
                }
                else if (!lead && pose == bodyStepUp)
                {
                    if (!leg.planted) { hold = m_startFeet; hold.mad(right, lateral); hold.y += .08f; place(leg, hold, false); }
                    if (hip.distance_to(leg.hold) <= length*.98f) goal = 1.f;
                }
            }
            else if (over)
            {
                // Vault: the left foot steps on the top while the right leg swings
                // over it to the far side, knee up; each is let go once the hips
                // are past it.
                Fvector along; along.sub(feet, c.lip); along.y = 0.f;
                const float past = along.dotproduct(c.direction);
                if (side == 0 && past < .1f)
                {
                    hold = c.lip; hold.mad(c.direction, .03f); hold.mad(right, lateral); hold.y += .08f;
                    place(leg, hold, true);
                    if (hip.distance_to(hold) <= length*.98f) goal = 1.f;
                }
                else if (side == 1 && past < .35f)
                {
                    hold = hip; hold.mad(c.direction, .3f);
                    hold.y = _max(c.lip.y + .15f, hip.y - length*.7f);
                    leg.planted = true; leg.swing = 1.f; leg.hold = leg.from = hold; leg.onTop = false;
                    if (hip.distance_to(hold) <= length*.98f) goal = 1.f;
                }
            }
            if (leg.planted)
            {
                leg.swing = _min(1.f, leg.swing + dt/.2f);
                const float s = Smooth(leg.swing), arc = sinf(PI*s);
                leg.at.lerp(leg.from, leg.hold, s);
                leg.at.mad(up, .12f*arc); // the moving foot lifts on its way
            }
        }
        else leg.planted = leg.onTop = leg.kneel = false;
        leg.weight += (goal - leg.weight)*(1.f - expf(-dt/.08f));
    }
}
