#include "stdafx.h"
#include "player_hud_legs.h"
#include "player_hud.h"
#include "actor.h"
#include "inventory_item.h"
#include "Inventory.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"

extern BOOL g_legs_enabled;

void player_legs_controller::destroy()
{
    if (!m_model)
        return;

    IRenderVisual* v = m_model->dcast_RenderVisual();
    if (v)
        ::Render->model_Delete(v);

    m_model = nullptr;
    m_visual_name = "";
}

bool player_legs_controller::resolve_config(CActor* actor, shared_str& out_section)
{
    PIItem outfit = actor->inventory().ItemFromSlot(OUTFIT_SLOT);
    shared_str current_outfit = outfit
        ? outfit->object().cNameSect()
        : shared_str("");

    if (m_last_outfit_sect != current_outfit)
    {
        m_config_warned = false;
        m_last_outfit_sect = current_outfit;
    }

    if (outfit)
        return resolve_outfit_config(current_outfit, out_section);
    else
        return resolve_default_config(out_section);
}

bool player_legs_controller::resolve_outfit_config(const shared_str& outfit_sect,
    shared_str& out_section)
{
    if (pSettings->line_exist(outfit_sect, "legs_visual_sect"))
    {
        shared_str candidate = pSettings->r_string(outfit_sect, "legs_visual_sect");
        if (pSettings->section_exist(candidate))
        {
            out_section = candidate;
            return true;
        }
        warn_once("legs_visual_sect [%s] referenced by outfit [%s] does not exist",
            candidate.c_str(), outfit_sect.c_str());
        return false;
    }

    string256 auto_sect;
    xr_sprintf(auto_sect, "%s_legs", outfit_sect.c_str());
    if (pSettings->section_exist(auto_sect))
    {
        out_section = auto_sect;
        return true;
    }

    if (pSettings->line_exist(outfit_sect, "legs_visual"))
    {
        out_section = outfit_sect;
        return true;
    }

    warn_once("no legs config for outfit [%s]", outfit_sect.c_str());
    return false;
}

bool player_legs_controller::resolve_default_config(shared_str& out_section)
{
    if (pSettings->section_exist("actor_legs_default"))
    {
        out_section = "actor_legs_default";
        return true;
    }
    warn_once("section [actor_legs_default] not found, legs disabled");
    return false;
}

void player_legs_controller::warn_once(const char* fmt, ...)
{
    if (m_config_warned) return;
    m_config_warned = true;

    string512 buf;
    va_list args;
    va_start(args, fmt);
    vsprintf_s(buf, sizeof(buf), fmt, args);
    va_end(args);

    Msg("! [player_legs] %s", buf);
}

bool player_legs_controller::ensure_model(const shared_str& legs_section)
{
    LPCSTR key = nullptr;

    if (pSettings->line_exist(legs_section, "legs_visual"))
        key = "legs_visual";
    else if (pSettings->line_exist(legs_section, "visual"))
        key = "visual";

    if (!key)
    {
        warn_once("section [%s] has no 'legs_visual' or 'visual' field", legs_section.c_str());
        destroy();
        return false;
    }

    shared_str new_visual = pSettings->r_string(legs_section, key);

    if (m_model && m_visual_name == new_visual)
        return true;

    destroy();

    IRenderVisual* raw = ::Render->model_Create(new_visual.c_str());
    if (!raw)
    {
        warn_once("failed to create model [%s]", new_visual.c_str());
        return false;
    }

    IKinematics* K = smart_cast<IKinematics*>(raw);
    if (!K)
    {
        ::Render->model_Delete(raw);
        warn_once("model [%s] is not a skeleton", new_visual.c_str());
        return false;
    }

    m_model = K;
    m_visual_name = new_visual;

    m_fwd_offset = READ_IF_EXISTS(pSettings, r_float, legs_section, "legs_fwd_offset", -0.55f);

    return true;
}

// clean up later
void player_legs_controller::copy_bones_from_actor(CActor* actor)
{
    if (!actor || !m_model)
        return;

    IKinematics* actor_K = actor->Visual()->dcast_PKinematics();
    if (!actor_K)
        return;

    actor_K->CalculateBones(TRUE);

    m_model->CalculateBones_Invalidate();
    m_model->CalculateBones(TRUE);

    u16 legs_root = m_model->LL_GetBoneRoot();
    CBoneInstance& root_bi = m_model->LL_GetBoneInstance(legs_root);
    root_bi.mTransform.identity();
    root_bi.mRenderTransform.mul_43(root_bi.mTransform,
        m_model->LL_GetData(legs_root).m2b_transform);

    u16 bone_count = m_model->LL_BoneCount();
    for (u16 i = 0; i < bone_count; ++i)
    {
        if (i == legs_root)
            continue;

        LPCSTR bone_name = m_model->LL_BoneName_dbg(i);
        u16 actor_bone_id = actor_K->LL_BoneID(bone_name);

        if (actor_bone_id == BI_NONE)
            continue;

        CBoneInstance& src = actor_K->LL_GetBoneInstance(actor_bone_id);
        CBoneInstance& dst = m_model->LL_GetBoneInstance(i);

        dst.mTransform.set(src.mTransform);
        dst.mRenderTransform.mul_43(dst.mTransform,
            m_model->LL_GetData(i).m2b_transform);
    }

    for (u16 i = 0; i < bone_count; ++i)
    {
        if (i == legs_root)
            continue;

        LPCSTR bone_name = m_model->LL_BoneName_dbg(i);
        u16 actor_bone_id = actor_K->LL_BoneID(bone_name);

        if (actor_bone_id == BI_NONE)
        {
            CBoneInstance& dst = m_model->LL_GetBoneInstance(i);
            dst.mTransform.identity();
            dst.mRenderTransform.mul_43(dst.mTransform,
                m_model->LL_GetData(i).m2b_transform);
        }
    }
}

void player_legs_controller::update(CActor* actor)
{
    if (!g_legs_enabled || !actor)
    {
        destroy();
        return;
    }

    if (actor->Holder() != nullptr)
    {
        destroy();
        return;
    }

    shared_str legs_section;
    if (!resolve_config(actor, legs_section))
    {
        destroy();
        return;
    }

    if (!ensure_model(legs_section))
        return;

    copy_bones_from_actor(actor);
}

void player_legs_controller::render(IDSGraphManager* DM)
{
    if (!g_legs_enabled || !m_model)
        return;

    CActor* actor = Actor();
    if (!actor)
        return;

    u32 move_state = actor->MovingState();
    if (move_state & mcClimb)
        return;

    IRenderVisual* visual = m_model->dcast_RenderVisual();
    if (!visual)
        return;

    m_legs_transform.set(actor->XFORM());

    Fvector fwd;
    fwd.set(m_legs_transform.k);
    fwd.y = 0.f;
    fwd.normalize_safe();
    m_legs_transform.c.mad(fwd, m_fwd_offset);

    DM->add_Dynamic(visual, &m_legs_transform);
}

void player_hud::update_legs(const Fmatrix& cam_trans)
{
    m_legs_controller.update(g_actor);
}

void player_hud::render_legs(IDSGraphManager* DM)
{
    m_legs_controller.render(DM);
}

void player_hud::delete_legs_model()
{
    m_legs_controller.destroy();
}