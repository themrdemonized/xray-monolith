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
    else if (m_actor->cam_active != eacFirstEye || (m_actor->MovingState() & mcClimb)) m_reason = "ladder_or_camera";
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

float CActorParkour::ViewScore(const Fvector& point) const
{
    auto* camera = m_actor->cameras[eacFirstEye];
    if (!camera) return flt_max;
    Fvector to; to.sub(point, camera->vPosition);
    if (to.square_magnitude() < EPS_S) return 0.f;
    to.normalize();
    Fvector forward = camera->vDirection; forward.normalize_safe();
    Fvector right = camera->Right(); right.normalize_safe();
    Fvector up = camera->Up(); up.normalize_safe();
    const float z = to.dotproduct(forward);
    if (z <= .02f) return flt_max;
    const float horizontal = atan2f(_abs(to.dotproduct(right)), z);
    const float vertical = atan2f(to.dotproduct(up), z);
    const float hn = horizontal / _max(.01f, m_options.viewHorizontal);
    const float vn = vertical >= 0.f ? vertical / _max(.01f, m_options.viewUp) :
        -vertical / _max(.01f, m_options.viewDown);
    return hn * 2.f + vn;
}

bool CActorParkour::InViewWindow(const Fvector& point) const
{
    auto* camera = m_actor->cameras[eacFirstEye];
    if (!camera) return false;
    Fvector to; to.sub(point, camera->vPosition);
    if (to.square_magnitude() < EPS_S) return true;
    to.normalize();
    Fvector forward = camera->vDirection; forward.normalize_safe();
    Fvector right = camera->Right(); right.normalize_safe();
    Fvector up = camera->Up(); up.normalize_safe();
    const float z = to.dotproduct(forward);
    if (z <= .02f) return false;
    const float horizontal = atan2f(_abs(to.dotproduct(right)), z);
    const float vertical = atan2f(to.dotproduct(up), z);
    // A literal camera-space box: narrow left/right, taller upward and shallower
    // downward. Do not ellipse-shrink the corners; high ledges should remain valid
    // anywhere inside the user's intended screen detection area.
    return horizontal <= m_options.viewHorizontal && vertical <= m_options.viewUp &&
        vertical >= -m_options.viewDown;
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
    Fvector forward;
    if (m_active && m_candidate.direction.square_magnitude() > EPS_S)
        forward = m_candidate.direction; // lock the accepted approach during freelook
    else if (m_actor->cameras[eacFirstEye])
    {
        forward = m_actor->cameras[eacFirstEye]->vDirection;
        forward.y = 0.f;
        if (forward.square_magnitude() < EPS_S) forward = BodyForward();
        else forward.normalize();
    }
    else forward = BodyForward();
    Fvector right = {forward.z, 0.f, -forward.x};
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

bool CActorParkour::Supported(const Fvector& feet, float radius)
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

    auto sample = [&](float ox, float oz, float scale)->bool
    {
        const float dx = ox * scale;
        const float dz = oz * scale;
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
    const float inner = .045f;
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
    // Clear a raised far-side floor before crossing its front edge, rather than
    // cutting diagonally through it from the lower window sill.
    points[2] = points[1]; points[2].y = _max(_max(top, c.origin.y), landing.y + .03f);
    points[3].y = _max(points[2].y, landing.y);
    // Releasing gravity requires the complete body to be beyond the edge.
    // Validate the falling column without adding it to the guided trajectory.
    if (!Sweep(points[3], landing, box)) return false;
    // Guide only the lift and crossing. Once clear, the real physics controller
    // owns the descent, including a drop on the far side of a fence/window.
    c.freeRelease = landing.y < points[3].y - .12f;
    // Keep the grab and initial pull in the starting stance. Only tuck during
    // the final lift; a wider crouch still has to clear the nearby wall.
    const u32 approachBox = initialBox == u32(-1) ? Movement()->BoxID() : initialBox;
    if (!Sweep(points[0], points[0], approachBox))
    { if (blockedApproach) *blockedApproach=true; return false; }
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
    float previousTopY = c.lip.y;
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
            if (!continuous) farEdge = true;
            else previousTopY = hit.y;
        }
        if (!walkable) continue;

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
        if ((!onTop && d > m_options.maxDepth + radius) ||
            (onTop ? topFound : overFound)) continue;
        if (!onTop && !windowOpening && c.rise > m_options.vaultHeight &&
            !m_options.forwardControl && !m_options.allowClimbVault) continue;
        if (!Supported(hit, radius)) continue;
        Candidate trial = c;
        trial.route = onTop ? "top" : (windowOpening ? "window" : "over");
        trial.action = (onTop ? (c.rise > m_options.mantleHeight ? "climb" : "mantle") :
            (c.rise <= m_options.vaultHeight ? "vault" : "climb_vault"));
        // Windows belong to Vault at every height. High open climb-overs need
        // both Climb and Vault; a disabled route must not hide another valid one.
        if (windowOpening ? (onTop || !m_options.allowVault) :
            onTop ? (c.rise > m_options.mantleHeight ? !m_options.allowClimb : !m_options.allowMantle) :
            (!m_options.allowVault || (c.rise > m_options.vaultHeight &&
                (m_options.forwardControl && c.rise<=m_options.mantleHeight ? !m_options.allowMantle : !m_options.allowClimb)))) continue;
        if (c.obstacleHeight+EPS_S < (onTop ? m_options.minMantleHeight : m_options.minVaultHeight) &&
            (!onTop || c.rise <= m_options.mantleHeight)) continue;
        bool fits = false;
        // Prefer the requested top stance; test only real configured body shapes.
        const u32 tiers[] = {m_options.crouchAtTop ? 1u : 0u, m_options.crouchAtTop ? 0u : 1u, 2u};
        for (u32 box : tiers)
        {
            if (blockedApproach[box]) continue;
            // Contact skin is not extra body clearance through a window frame.
            // Each stance must fit its real shoulder width between the jambs.
            if(2.f*Radius(box)>openingWidth+EPS_S) continue;
            if (Build(trial, hit, box, &blockedApproach[box])) { fits = true; break; }
        }
        if (!fits) continue;
        if (onTop) { topCandidate = trial; topFound = true; }
        else { overCandidate = trial; overFound = true; }
        if (topFound && overFound) break;
    }
    if (m_options.forwardControl && !windowOpening && c.rise > m_options.vaultHeight)
    {
        // A climb/mantle always has a safe top endpoint. Holding Forward may
        // continue across only after that endpoint, never replace it with a drop.
        if (!topFound) return false;
        c = topCandidate;
        if (overFound && m_options.forwardHeld && topCandidate.box==overCandidate.box)
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
    else if (topFound && c.airborne && m_options.airborneTop) c = topCandidate;
    else if (overFound && (!topFound || m_options.preferVault)) c = overCandidate;
    else if (topFound) c = topCandidate;
    else return false;
    return true;
}

bool CActorParkour::Query(const Options& options)
{
    // Debug queries during a move must not erase the executing trajectory.
    if (m_active) { m_reason = "busy"; return false; }
    m_candidate = Candidate();
    m_options = options;
    if (!Available()) return false;

    auto* movement = Movement();
    if (movement->GetVelocity().y < -m_options.maxFall) { m_reason = "falling"; return false; }
    const Fvector feet = m_actor->Position();
    const bool airborne = Airborne();
    const float heightOrigin = m_options.hasHeightOrigin ? m_options.heightOrigin : feet.y;
    const float minimumRise = _max(.04f,m_options.minHeight-(feet.y-heightOrigin));

    auto* camera = m_actor->cameras[eacFirstEye];
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
    const float radius = Radius(0), range = radius + m_options.reach;
    const float columns[] = {0.f, .12f, -.12f, .24f, -.24f};

    Candidate best;
    float bestScore = flt_max;
    bool sawLedge = false;
    bool routeRejected = false;

    auto consider = [&](const Fvector& lip, float rise)
    {
        if (lip.y-heightOrigin+EPS_S < m_options.minHeight || rise < .04f || rise > m_options.maxHeight) return;
        Fvector delta; delta.sub(lip, feet); delta.y = 0.f;
        const float horizontalDistance = delta.magnitude();
        if (horizontalDistance > range + .04f) return;

        bool viewOk = InViewWindow(lip);
        bool pointBlankView = false;
        if (!viewOk && horizontalDistance <= radius + .24f)
        {
            // At point-blank range a high ledge can project almost straight above
            // the camera and fail a normal screen-space vertical test. Preserve
            // horizontal intent/facing, but do not let looking strongly downward
            // acquire a high ledge while hugging a wall.
            Fvector to = delta;
            if (to.square_magnitude() > EPS_S) to.normalize();
            const float horizontalFacing = to.square_magnitude() > EPS_S ? to.dotproduct(dir) : 1.f;
            const float minLookY = rise > 1.55f ? -.15f : -.70f;
            viewOk = horizontalFacing >= cosf(m_options.viewHorizontal) &&
                camera->vDirection.y > minLookY;
            pointBlankView = viewOk;
        }
        if (!viewOk) return;
        sawLedge = true;

        const float viewScore = pointBlankView ? .35f : ViewScore(lip);
        const float scoreFloor = _min(viewScore*.25f + horizontalDistance*2.f,
            viewScore + horizontalDistance*.08f);
        if (scoreFloor >= bestScore) return;
        Candidate trial;
        trial.origin = feet;
        trial.lip = lip;
        trial.rise = rise;
        trial.obstacleHeight = lip.y-heightOrigin;
        trial.direction = dir;
        trial.velocity = movement->GetVelocity();
        trial.airborne = airborne;
        if (!FindLanding(trial)) { routeRejected = true; return; }

        // In a window, prefer the near sill over raised flooring beyond it.
        // Looking down into the opening must not move the grab to the far side.
        const float score = (!strcmp(trial.route,"window") ?
            viewScore*.25f + delta.magnitude()*2.f : viewScore + delta.magnitude()*.08f) +
            (ContactReady(trial) && InGrabWindow(trial.lip)?0.f:3.f);
        if (score < bestScore)
        {
            bestScore = score;
            best = trial;
        }
    };

    // Wall-backed ledges/fences: find the blocked->clear transition in several
    // horizontal columns, then validate the actual top and full body route.
    for (float lateral : columns)
    {
        bool blockedBelow = false;
        Fvector face, faceNormal;
        for (float h = _max(.04f, minimumRise - .04f);
             h <= m_options.maxHeight + .12f; h += .08f)
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
        for (float d = edgeStep; d <= range + .001f; d += edgeStep)
        {
            Fvector probe, lip, normal;
            probe.mad(feet, right, lateral); probe.mad(dir, d);
            probe.y += m_options.maxHeight + .05f;
            if (!Ray(probe, down, m_options.maxHeight - minimumRise + .18f, lip, normal) ||
                normal.y < cosf(m_options.maxSlope)) continue;
            const float rise = lip.y - feet.y;
            if (lip.y-heightOrigin+EPS_S < m_options.minHeight || rise < .04f || rise > m_options.maxHeight) continue;

            Fvector behindProbe, behindHit, behindNormal;
            behindProbe.mad(feet, right, lateral);
            behindProbe.mad(dir, _max(0.f, d - edgeStep * 1.5f));
            behindProbe.y = probe.y;
            const bool behindSurface = Ray(behindProbe, down,
                m_options.maxHeight + m_options.maxDrop + .20f, behindHit, behindNormal) &&
                behindNormal.y >= cosf(m_options.maxSlope);
            if (behindSurface && behindHit.y > lip.y - edgeDrop) continue;

            consider(lip, rise);
        }
    }

    if (bestScore != flt_max)
    {
        SmoothPath(best);
        best.valid = true;
        m_candidate = best;
        m_queryFrame = Device.dwFrame;
        m_reason = "ready";
        return true;
    }

    m_reason = routeRejected ? "no_clear_route" : (sawLedge ? "outside_route" : "no_ledge");
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

bool CActorParkour::InGrabWindow(const Fvector& point) const
{
    if (m_options.grabHorizontal <= 0.f || m_options.grabVertical <= 0.f) return true;
    auto* camera = m_actor->cameras[eacFirstEye];
    if (!camera) return false;
    Fvector to; to.sub(point,camera->vPosition);
    const float forward = to.dotproduct(camera->vDirection);
    return forward > EPS &&
        _abs(to.dotproduct(camera->Right())) <= forward * tanf(m_options.grabHorizontal) &&
        _abs(to.dotproduct(camera->Up())) <= forward * tanf(m_options.grabVertical);
}

bool CActorParkour::ContactReady(const Candidate& c) const
{
    auto* movement = Movement();
    if (!movement) return false;

    const Fvector feet = m_actor->Position();
    Fvector horizontal; horizontal.sub(c.lip, feet); horizontal.y = 0.f;
    const float centerDistance = horizontal.magnitude();
    const float bodyRadius = Radius(0);
    const float gap = _max(0.f, centerDistance - bodyRadius);

    Fvector velocity = movement->GetVelocity();
    const float toward = velocity.dotproduct(c.direction);

    // Normal vault/mantle/climb can be pre-approved while approaching, but guided
    // movement must not begin until the ledge is inside plausible arm reach.
    // Vertical reach is measured from the current feet position, so a jump can
    // naturally bring a very tall configured ledge into reach without increasing
    // scripted climb speed.
    const float verticalReach = m_options.armReachHeight;
    // This only permits the reach animation. The actual arm chains still have
    // to reach both contacts before physics can be taken over.
    const float horizontalReach = m_options.reach;
    if (gap > horizontalReach || centerDistance > m_options.armExtension ||
        c.lip.y - feet.y > verticalReach) return false;

    // Do not reverse a clear fly-by. Small drift is tolerated because contact with
    // the wall often changes velocity just before the grab.
    return toward > -1.50f;
}

bool CActorParkour::Start(float seconds, bool hands)
{
    if (m_active) { m_reason = "busy"; return false; }
    if (!m_candidate.valid || Device.dwFrame - m_queryFrame > 2 ||
        m_actor->Position().distance_to(m_candidate.origin) > .05f)
    { m_reason = "stale_query"; m_candidate.valid = false; return false; }
    if (!Available()) return false;
    if (!_valid(seconds) || seconds <= 0.f) { m_reason = "invalid_duration"; return false; }

    // Querying is passive. Keep ordinary jump/gravity until the current body is
    // actually close enough to put hands on the approved ledge.
    if (!InGrabWindow(m_candidate.lip)) { m_reason = "outside_grab_box"; return false; }
    if (!ContactReady(m_candidate)) { m_reason = "not_in_reach"; return false; }

    if (!Supported(m_candidate.landing, Radius(0))) { m_reason = "landing_changed"; return false; }
    const LPCSTR section = "item_anm_ledge_grabbing";
    if (pSettings->section_exist(section))
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
    m_hands=hands;
    m_contactValid[0] = m_contactValid[1] = false;
    CaptureHands();
    if (!HandContactsReady())
    {m_hands=false;m_reason="no_hand_support";return false;}
    for (size_t i = 1; i < m_candidate.path.size(); ++i)
        if (!Sweep(m_candidate.path[i - 1], m_candidate.path[i], m_candidate.pathBoxes[i]))
        { m_hands=false; m_reason = "path_changed"; return false; }
    m_reaching=m_hands;
    m_handConfirmed=false;m_reachTime=0.f;
    m_catchDrop=m_catchElapsed=m_catchSpeed=m_catchSlack=0.f;
    if (!m_restorePending) m_savedBox = Movement()->BoxID();
    if(!m_reaching) ApplyStance(m_candidate.pathBoxes[1]);
    m_savedGravity = Movement()->AppliesGravity();
    if(!m_reaching) Movement()->SetApplyGravity(FALSE);
    if(!m_reaching) Movement()->SetVelocity(0.f, 0.f, 0.f);
    m_elapsed = 0.f; m_duration = seconds;
    m_forwardHeld=m_options.forwardHeld;
    m_active = true; m_candidate.valid = false;
    m_lastPosition = m_actor->Position();
    if (!m_reaching)
    {
        m_actor->mstate_wishful &= ~mcJump;
        m_actor->mstate_real &= ~(mcJump | mcFall | mcSprint);
    }
    BeginCamera();
    m_reason = "moving";
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
    const float t = Progress();
    return (m_handConfirmed ? 1.f : Smooth(clampr((t - m_contactStart) / (m_contactFull - m_contactStart), 0.f, 1.f))) *
        (1.f - Smooth(clampr((t - m_contactRelease) / (m_contactEnd - m_contactRelease), 0.f, 1.f)));
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

void CActorParkour::ConfirmHandContact(bool ready, float slack)
{
    if (!m_active || !m_reaching || !ready || m_handConfirmed) return;
    m_handConfirmed = true;
    m_catchSpeed = _max(0.f,-Movement()->GetVelocity().y);
    m_catchSlack = _valid(slack) ? _max(0.f,slack) : 0.f;
    // Both rendered hands have touched. Stop physics from carrying a real catch
    // out of reach before the next controller update consumes it.
    Movement()->SetApplyGravity(FALSE);
    Movement()->SetVelocity(0.f,0.f,0.f);
}

void CActorParkour::Update(float dt)
{
    if (!m_active) return;
    if (!Available()) { Cancel(m_reason); return; }
    if (!_valid(dt) || dt <= 0.f) return;

    if(m_reaching)
    {
        m_reachTime+=dt;
        // A confirmed rendered contact wins over a late update/timeout. Do not
        // throw away an actual catch because the next physics tick has fallen.
        if(!m_handConfirmed && m_reachTime>m_options.reachTimeout)
        {Cancel("contact_missed");return;}
        if(!m_handConfirmed) return; // Normal movement/gravity still own the body.
        const bool pull = strcmp(m_candidate.action,"vault") != 0 && strcmp(m_candidate.route,"window") != 0;
        const float drop = pull ? _min(m_catchSlack,_min(m_options.catchMaxDrop,
            m_catchSpeed*m_options.catchDropPerSpeed)) : 0.f;
        m_catchOrigin=m_actor->Position();
        Candidate refreshed=m_candidate;
        refreshed.origin=m_actor->Position();refreshed.rise=refreshed.lip.y-refreshed.origin.y;
        refreshed.topTime=0.f;
        m_options.forwardHeld=m_forwardHeld;
        if (drop > EPS)
        {
            Fvector bottom=refreshed.origin;bottom.y-=drop;
            if (Sweep(refreshed.origin,bottom,Movement()->BoxID()))
            {
                Candidate dipped=refreshed;dipped.origin=bottom;dipped.rise+=drop;
                if (FindLanding(dipped)) { refreshed=dipped;m_catchDrop=drop; }
            }
        }
        if(m_catchDrop <= 0.f && !FindLanding(refreshed)) {Cancel("path_changed");return;}
        SmoothPath(refreshed);
        const float pace=m_duration/m_candidate.distances.back();
        m_duration=refreshed.distances.back()*pace;
        m_candidate=refreshed;m_reaching=false;m_lastPosition=m_actor->Position();
        ApplyStance(m_candidate.pathBoxes[1]);
        Movement()->SetApplyGravity(FALSE);Movement()->SetVelocity(0.f,0.f,0.f);
        m_actor->mstate_wishful &= ~mcJump;
        m_actor->mstate_real &= ~(mcJump | mcFall | mcSprint);
    }

    if (m_actor->Position().distance_to(m_lastPosition) > .4f)
    { Cancel("external_move"); return; }

    if (m_catchDrop > 0.f && m_catchElapsed < m_options.catchBrakeTime)
    {
        m_catchElapsed=_min(m_options.catchBrakeTime,m_catchElapsed+dt);
        const float u=m_catchElapsed/m_options.catchBrakeTime;
        Fvector position=m_catchOrigin;
        position.y-=m_catchDrop*(2.f*u-u*u);
        if (!Sweep(m_lastPosition,position,Movement()->BoxID())) { Cancel("obstructed");return; }
        Movement()->SetPosition(position);m_actor->Position()=position;
        m_actor->spatial_move();m_lastPosition=position;
        return;
    }

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
        if (strcmp(m_candidate.route,"top") != 0)
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
    EndCamera();
    const bool passive = m_reaching;
    m_reaching=false;
    m_active = false; m_hands = false;
    m_restorePending = Movement() && Movement()->BoxID() != m_savedBox;
    if (Movement() && Movement()->BoxID() != 0) m_actor->mstate_real |= mcCrouch;
    if (Movement())
    {
        if(!passive) Movement()->SetVelocity(0.f, 0.f, 0.f);
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
    m_reaching=m_handConfirmed=false;
    m_elapsed=0.f;
    m_candidate = Candidate(); m_reason = "idle";
}
