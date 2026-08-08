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

BOOL g_legs_enabled = FALSE;

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

    IRenderVisual* raw = ::Render->model_Create(model.c_str());
    if (!raw)
    {
        warn_once("failed to create model [%s]", model.c_str());
        return false;
    }

    IKinematics* K = smart_cast<IKinematics*>(raw);
    if (!K)
    {
        ::Render->model_Delete(raw);
        warn_once("model [%s] is not a skeleton", model.c_str());
        return false;
    }

    m_model = K;
    m_visual_name = model;

    if (pSettings->line_exist(sect, "legs_fwd_offset"))
        m_fwd_offset = pSettings->r_float(sect, "legs_fwd_offset");
    else
        m_fwd_offset = std::nullopt;

    return true;
}

// clean up later
float legs_spine_offset_y = 0.1f;
void player_legs_controller::copy_bones_from_actor(CActor* actor, bool isShadowPass)
{
    if (!actor || !m_model)
        return;

    IKinematics* actor_K = actor->Visual()->dcast_PKinematics();
    if (!actor_K)
        return;

    if (!isShadowPass)
    {
        actor_K->CalculateBones(TRUE);
        m_model->CalculateBones_Invalidate();
        m_model->CalculateBones(TRUE);
    }

    u16 legs_root = m_model->LL_GetBoneRoot();
    CBoneInstance& root_bi = m_model->LL_GetBoneInstance(legs_root);
    root_bi.mTransform.identity();
    root_bi.mRenderTransform.mul_43(root_bi.mTransform,
        m_model->LL_GetData(legs_root).m2b_transform);

    u16 bone_count = m_model->LL_BoneCount();
    if (bone_count == actor_K->LL_BoneCount())
    {
        for (u16 i = 0; i < bone_count; ++i)
        {
            m_model->LL_GetTransform(i).set(actor_K->LL_GetTransform(i));
            m_model->LL_GetTransform_R(i).set(actor_K->LL_GetTransform_R(i));
        }
    }
    else
    {
        for (auto& [bonename, ID] : *m_model->LL_Bones())
        {
            auto BoneID = actor_K->LL_BoneID(bonename);
            if (BoneID != BI_NONE)
            {
                m_model->LL_GetTransform(ID).set(actor_K->LL_GetTransform(BoneID));
                m_model->LL_GetTransform_R(ID).set(actor_K->LL_GetTransform_R(BoneID));
            }
        }
    }

    if (!isShadowPass)
    {
        if (auto BoneID = m_model->LL_BoneID("bip01_spine"); BoneID != BI_NONE)
        {
            auto& BoneInstance = m_model->LL_GetData(BoneID);
            auto& transform = m_model->LL_GetTransform(BoneInstance.GetParentID());
            transform.c.y += legs_spine_offset_y;
            m_model->Bone_Calculate(&BoneInstance, &transform);
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
    
}

float legs_fwd_offset = -0.5f;
BOOL legs_attach_to_camera = TRUE;
extern int showActorBody;
extern xr_unordered_set<CDemoRecord*> pDemoRecords;
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

    copy_bones_from_actor(actor, isShadowPass);

    m_legs_transform.set(actor->XFORM());
    if (legs_attach_to_camera && pDemoRecords.empty())
        m_legs_transform.c.set(Device.vCameraPosition.x, m_legs_transform.c.y, Device.vCameraPosition.z);       

    Fvector fwd = m_legs_transform.k;
    fwd.y = 0.f;
    fwd.normalize_safe();

    float offset = m_fwd_offset.has_value() ? m_fwd_offset.value() : legs_fwd_offset;
    m_legs_transform.c.mad(fwd, offset);

    // Move actor's XFORM for correct shadow placement
    actor->XFORMShadow.translate_over(m_legs_transform.c);
}

void player_legs_controller::render()
{
    if (!m_model)
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

    ::Render->set_Transform(&m_legs_transform);
    ::Render->add_Visual(visual);
}
