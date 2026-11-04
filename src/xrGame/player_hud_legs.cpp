#include "stdafx.h"
#include "player_hud_legs.h"
#include "player_hud.h"
#include "actor.h"
#include "inventory_item.h"
#include "Inventory.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "../xrEngine/FDemoRecord.h"
#include "../xrEngine/CameraBase.h"
#include "ai_space.h"
#include "script_engine.h"

BOOL g_legs_enabled = FALSE;
float legs_fwd_offset = -0.5f;
BOOL legs_attach_to_camera = TRUE;
extern int showActorBody;
extern xr_unordered_set<CDemoRecord*> pDemoRecords;

void player_legs_controller::destroy()
{
    for (IKinematics** model : { &m_model, &m_shadow_model })
    {
        if (!*model)
            continue;

        if (IRenderVisual* v = (*model)->dcast_RenderVisual())
            ::Render->model_Delete(v);

        *model = nullptr;
    }

    m_visual_name = "";
    m_clip_hidden = false;
}

bool player_legs_controller::resolve_config(CActor* actor, shared_str& sect, shared_str& model)
{
    PIItem outfit = actor->inventory().ItemFromSlot(OUTFIT_SLOT);
    shared_str current_outfit = outfit
        ? outfit->object().cNameSect()
        : shared_str("actor");

    if (m_last_outfit_sect == current_outfit)
    {
        sect = m_last_outfit_sect;
        model = m_last_model;
        return true;
    }

    m_last_outfit_sect = current_outfit;

    if (outfit)
    {
        if (pSettings->line_exist(m_last_outfit_sect, "legs_visual"))
        {
            sect = m_last_outfit_sect;
            m_last_model = pSettings->r_string(m_last_outfit_sect, "legs_visual");
            model = m_last_model;
            return true;
        }

        if (pSettings->line_exist(m_last_outfit_sect, "actor_visual"))
        {
            sect = m_last_outfit_sect;
            m_last_model = pSettings->r_string(m_last_outfit_sect, "actor_visual");
            model = m_last_model;
            return true;
        }

        warn_once("outfit [%s] has no legs_visual or actor_visual, fallback to default", m_last_outfit_sect.c_str());
    }

    // default
    if (pSettings->line_exist("actor", "legs_visual"))
    {
        sect = m_last_outfit_sect;
        m_last_model = pSettings->r_string(m_last_outfit_sect, "legs_visual");
        model = m_last_model;
        return true;
    }

    if (pSettings->line_exist(m_last_outfit_sect, "visual"))
    {
        sect = m_last_outfit_sect;
        m_last_model = pSettings->r_string(m_last_outfit_sect, "visual");
        model = m_last_model;
        return true;
    }

    warn_once("actor has no legs_visual or visual");

    return false;
}

void player_legs_controller::warn_once(const char* fmt, ...)
{
    string512 buf;
    va_list args;
    va_start(args, fmt);
    vsprintf_s(buf, sizeof(buf), fmt, args);
    va_end(args);

    xr_string s = make_string("! [player_legs] %s", buf).c_str();
    static xr_unordered_flat_set<xr_string> warnings;
    if (warnings.find(s) == warnings.end())
    {
        warnings.insert(s);
        Msg(s.c_str());
    }
}

bool player_legs_controller::ensure_model(const shared_str& sect, const shared_str& model)
{
    if (m_model && m_visual_name == model)
        return true;

    destroy();

    // Separate instance for the shadow pass: the full body shadow must not touch the hidden bones
    // of the main model, which is drawn again after shadows (emissive, forward passes)
    for (IKinematics** target : { &m_model, &m_shadow_model })
    {
        IRenderVisual* raw = ::Render->model_Create(model.c_str());
        if (!raw)
        {
            destroy();
            warn_once("failed to create model [%s]", model.c_str());
            return false;
        }

        IKinematics* K = smart_cast<IKinematics*>(raw);
        if (!K)
        {
            ::Render->model_Delete(raw);
            destroy();
            warn_once("model [%s] is not a skeleton", model.c_str());
            return false;
        }

        *target = K;
    }

    m_visual_name = model;

    if (pSettings->line_exist(sect, "legs_fwd_offset"))
        m_fwd_offset = pSettings->r_float(sect, "legs_fwd_offset");
    else
        m_fwd_offset = std::nullopt;

    ::luabind::functor<void> funct;
    if (ai().script_engine().functor("_G.player_legs_controller__OnCreateLegs", funct))
        funct(sect.c_str());

    return true;
}

// TODO : clean up later
float legs_spine_offset_y = 0.1f;
// In a parkour clip, parts of the legs model this close to the camera are folded away.
static constexpr float LegsNearEyes = .38f;
void player_legs_controller::copy_bones_from_actor(CActor* actor)
{
    if (!actor || !m_model || !m_shadow_model)
        return;

    IKinematics* actor_K = actor->Visual()->dcast_PKinematics();
    if (!actor_K)
        return;

    actor_K->CalculateBones(TRUE);

    // Full body for shadows
    m_shadow_model->CopyBonesFrom(actor_K);

    // Legs only for the main pass
    m_model->CopyBonesFrom(actor_K);

    // A parkour clip places the whole body in the world and the camera rides its
    // eyes: draw it as it is, from the waist down, with nothing shifted.
    const bool clip = actor->Parkour().ClipWeight() > .5f;
    if (clip != m_clip_hidden)
    {
        if (clip)
        {
            m_clip_visible = m_model->LL_GetBonesVisible();
            if (auto chest = m_model->LL_BoneID("bip01_spine2"); chest != BI_NONE)
                m_model->LL_SetBoneVisible(chest, false, true);
        }
        else
            m_model->LL_SetBonesVisible(m_clip_visible);
        m_clip_hidden = clip;
    }

    if (auto BoneID = m_model->LL_BoneID("bip01_spine"); !clip && BoneID != BI_NONE)
    {
        auto& BoneInstance = m_model->LL_GetData(BoneID);
        auto& transform = m_model->LL_GetTransform(BoneInstance.GetParentID());
        transform.c.y += legs_spine_offset_y;
        m_model->Bone_Calculate(&BoneInstance, &transform);
    }

    // In a clip the body is drawn where it really is, right under the eyes, and leans
    // over the edge: fold everything from the lower ribs up into the waist (a zero-scale
    // pose), so no torso shows under the camera whatever the model's visibility groups.
    if (actor->Parkour().ClipWeight() > 0.f)
    {
        const u16 ribs = m_model->LL_BoneID("bip01_spine1");
        if (ribs != BI_NONE)
        {
            Fmatrix fold;
            fold.identity();
            fold.i.set(0.f, 0.f, 0.f); fold.j.set(0.f, 0.f, 0.f); fold.k.set(0.f, 0.f, 0.f);
            fold.c.set(m_model->LL_GetTransform(ribs).c);
            for (u16 id = 0, count = m_model->LL_BoneCount(); id < count; ++id)
                for (u16 bone = id; bone != BI_NONE; bone = m_model->LL_GetData(bone).GetParentID())
                    if (bone == ribs)
                    {
                        CBoneInstance& instance = m_model->LL_GetBoneInstance(id);
                        instance.mTransform = fold;
                        instance.mRenderTransform = fold;
                        break;
                    }
        }
        // Whatever comes up to the eyes (knees tucked in a roll, a squat after a hard
        // landing, a thigh pulled over an edge) folds away too, with everything below
        // it: the camera would see its polygons from inside.
        const u16 count = m_model->LL_BoneCount();
        xr_vector<u8> nearEyes(count, 0);
        for (u16 id = 0; id < count; ++id)
        {
            Fvector world;
            actor->XFORM().transform_tiny(world, m_model->LL_GetTransform(id).c);
            nearEyes[id] = world.distance_to(Device.vCameraPosition) < LegsNearEyes;
        }
        for (u16 id = 0; id < count; ++id)
        {
            u16 top = BI_NONE;
            for (u16 bone = id; bone != BI_NONE; bone = m_model->LL_GetData(bone).GetParentID())
                if (nearEyes[bone]) top = bone;
            if (top == BI_NONE) continue;
            Fmatrix fold;
            fold.identity();
            fold.i.set(0.f, 0.f, 0.f); fold.j.set(0.f, 0.f, 0.f); fold.k.set(0.f, 0.f, 0.f);
            fold.c.set(m_model->LL_GetTransform(top).c);
            CBoneInstance& instance = m_model->LL_GetBoneInstance(id);
            instance.mTransform = fold;
            instance.mRenderTransform = fold;
        }
    }

    static LPCSTR bonesToHide[] = { "bip01_neck", "bip01_l_upperarm", "bip01_r_upperarm" };
    for (const auto& bone : bonesToHide)
    {
        u16 bone_id = m_model->LL_BoneID(bone);
        if (bone_id != BI_NONE)
        {
            m_model->LL_SetBoneVisible(bone_id, false, true);
        }
    }
}

void player_legs_controller::update(CActor* actor, bool isShadowPass)
{
    actor->XFORMShadow.set(actor->XFORM());

    if (!g_legs_enabled || showActorBody != 0 || !actor)
    {
        destroy();
        return;
    }

    if (actor->Holder() != nullptr)
    {
        destroy();
        return;
    }

    shared_str sect;
    shared_str model;
    if (!resolve_config(actor, sect, model))
    {
        destroy();
        return;
    }

    if (!ensure_model(sect, model))
        return;

    // Bones of both models are set up once per frame, outside of the shadow pass
    if (!isShadowPass)
        copy_bones_from_actor(actor);

    m_legs_transform.set(actor->XFORM());
    if (legs_attach_to_camera && pDemoRecords.empty())
        m_legs_transform.c.set(Device.vCameraPosition.x, m_legs_transform.c.y, Device.vCameraPosition.z);

    Fvector fwd = m_legs_transform.k;
    fwd.y = 0.f;
    fwd.normalize_safe();

    float offset = m_fwd_offset.has_value() ? m_fwd_offset.value() : legs_fwd_offset;
    m_legs_transform.c.mad(fwd, offset);
    if (const float clip = actor->Parkour().ClipWeight(); clip > 0.f)
        m_legs_transform.c.lerp(m_legs_transform.c, actor->XFORM().c, clip);

    // Move actor's XFORM for correct shadow placement
    actor->XFORMShadow.translate_over(m_legs_transform.c);
}

void player_legs_controller::render(bool isShadowPass)
{
    IKinematics* model = isShadowPass ? m_shadow_model : m_model;
    if (!model)
        return;

    CActor* actor = Actor();
    if (!actor)
        return;

    u32 move_state = actor->MovingState();
    if (move_state & mcClimb)
        return;

    // A roll or hard landing tumbles the legs through the view: they are not drawn
    // until the body is up again (the shadow still shows the whole body).
    if (!isShadowPass && actor->Parkour().PerformedClip())
        return;

    IRenderVisual* visual = model->dcast_RenderVisual();
    if (!visual)
        return;

    ::Render->set_Transform(&m_legs_transform);
    ::Render->add_Visual(visual);
}
