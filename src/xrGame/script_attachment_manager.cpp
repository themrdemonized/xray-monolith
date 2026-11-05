#include "stdafx.h"
#include "script_attachment_manager.h"
#include "player_hud.h"
#include "actor.h"
#include "ui\UIScriptWnd.h"
#include "ActorEffector.h"
#include "../xrPhysics/PhysicsShell.h"
#include "../xrPhysics/PHCollideValidator.h"
#include "CharacterPhysicsSupport.h"
#include "../xrPhysics/PHElement.h"
#include "physics_shell_scripted.h"
#include "debug_renderer.h"
#include "../Layers/xrRender/LightTrack.h"

static void update_visbox_attachment(IKinematics* k)
{
	script_attachment* att = static_cast<script_attachment*>(k->GetUpdateCallbackParam());
	if (!att) return;

	if (k->NeedUCalc())
	{
		for (auto& pair : *att->GetAttachments())
		{
			script_attachment* child_att = pair.second;
			if (child_att->GetType() != att->GetType()) continue;

			Fbox Box = child_att->Box();

			Fmatrix offset;
			offset.mul(child_att->BoneTransform(k), child_att->GetOffset());
			Box.xform(offset);

			// Update Box
			Fbox& kbox = const_cast<Fbox&>(k->GetBox());
			kbox.merge(Box);

			// Update Sphere
			Fsphere& kshere = k->dcast_RenderVisual()->getVisData().sphere;
			kbox.getsphere(kshere.P, kshere.R);
		}
	}

	if (psDeviceFlags2.test(rsDBGDrawVisBoxes))
	{
	Fmatrix box, cent;
	cent.translate(k->dcast_RenderVisual()->getVisData().sphere.P);
	box.mul(att->GetTransform(), cent);

	const Fbox& kbox = k->GetBox();
	Fvector radius;
	kbox.getradius(radius);
		Level().debug_renderer().draw_obb(box, radius, D3DCOLOR_XRGB(150, 0, 150), att->GetType() == eSA_HUD);
	}
}

extern IC bool RAYvsOBB(const Fmatrix& IM, const Fvector& b_hsize, const Fvector& S, const Fvector& D, float& R, BOOL bCull);
extern IC bool RAYvsSPHERE(const Fsphere& s_sphere, const Fvector& S, const Fvector& D, float& R, BOOL bCull);
extern IC bool RAYvsCYLINDER(const Fcylinder& c_cylinder, const Fvector& S, const Fvector& D, float& R, BOOL bCull);

CCF_Attachment_Skeleton::CCF_Attachment_Skeleton(script_attachment* O) : CCF_Skeleton(O)
{
	IRenderVisual* pVisual = O->Visual();
	IKinematics* K = PKinematics(pVisual);
	VERIFY3(K, "Can't create skeleton without Kinematics.", *O->cNameVisual());
	bv_box.set(pVisual->getVisData().box);
	bv_box.getsphere(bv_sphere.P, bv_sphere.R);
	vis_mask = 0;
	dwFrame = 0;
	dwFrameTL = 0;
}

void CCF_Attachment_Skeleton::BuildState()
{
	dwFrame = Device.dwFrame;
	IRenderVisual* pVisual = owner_attachment->Visual();
	IKinematics* K = PKinematics(pVisual);
	K->CalculateBones();
	const Fmatrix& L2W = owner_attachment->XFORM();

	if (vis_mask != K->LL_GetBonesVisible())
	{
		vis_mask = K->LL_GetBonesVisible();
		elements.clear_not_free();
		bv_box.set(pVisual->getVisData().box);
		bv_box.getsphere(bv_sphere.P, bv_sphere.R);
		for (u16 i = 0; i < K->LL_BoneCount(); i++)
		{
			if (!K->LL_GetBoneVisible(i)) continue;
			SBoneShape& shape = K->LL_GetData(i).shape;
			if (SBoneShape::stNone == shape.type) continue;
			if (shape.flags.is(SBoneShape::sfNoPickable)) continue;
			elements.push_back(SElement(i, shape.type));
		}
	}

	CDebugRenderer& render = Level().debug_renderer();

	for (ElementVecIt I = elements.begin(); I != elements.end(); I++)
	{
		if (!I->valid()) continue;
		SBoneShape& shape = K->LL_GetData(I->elem_id).shape;
		Fmatrix ME, T, TW;
		const Fmatrix& Mbone = K->LL_GetTransform(I->elem_id);

		switch (I->type)
		{
		case SBoneShape::stBox:
		{
			const Fobb& B = shape.box;
			B.xform_get(ME);

			I->b_hsize.set(B.m_halfsize);
			// prepare matrix World to Element
			T.mul_43(Mbone, ME); // model space
			TW.mul_43(L2W, T); // world space
			bool b = I->b_IM.invert_b(TW);
			// check matrix validity
			if (!b)
			{
				Msg("! ERROR: invalid bone xform . Bone disabled.");
				Msg("! ERROR: bone_id=[%d], world_pos[%f,%f,%f]", I->elem_id, VPUSH(TW.c));
				Msg("visual name %s", owner_attachment->GetModelScript());
				Msg("object name %s", owner_attachment->GetName());
				I->elem_id = u16(-1); //. hack - disable invalid bone
				continue;
			}

			if (psDeviceFlags2.test(rsDBGDrawAttCForms))
				render.draw_obb(TW, I->b_hsize, D3DCOLOR_XRGB(150, 0, 150), false);
		}
		break;
		case SBoneShape::stSphere:
		{
			const Fsphere& S = shape.sphere;
			Mbone.transform_tiny(I->s_sphere.P, S.P);
			L2W.transform_tiny(I->s_sphere.P);
			I->s_sphere.R = S.R;

			if (psDeviceFlags2.test(rsDBGDrawAttCForms))
				render.draw_ellipse(Fmatrix().mul(Fmatrix().translate(I->s_sphere.P), Fmatrix().scale(I->s_sphere.R, I->s_sphere.R, I->s_sphere.R)), D3DCOLOR_XRGB(150, 0, 150), false);
		}
		break;
		case SBoneShape::stCylinder:
		{
			const Fcylinder& C = shape.cylinder;
			Mbone.transform_tiny(I->c_cylinder.m_center, C.m_center);
			L2W.transform_tiny(I->c_cylinder.m_center);
			Mbone.transform_dir(I->c_cylinder.m_direction, C.m_direction);
			L2W.transform_dir(I->c_cylinder.m_direction);
			I->c_cylinder.m_height = C.m_height;
			I->c_cylinder.m_radius = C.m_radius;

			if (psDeviceFlags2.test(rsDBGDrawAttCForms))
				render.draw_cylinder(Fmatrix().translate(I->c_cylinder.m_center), I->c_cylinder.m_height, I->c_cylinder.m_radius, D3DCOLOR_XRGB(150, 0, 150), false);
		}
		break;
		}
	}
}

void CCF_Attachment_Skeleton::BuildTopLevel()
{
	dwFrameTL = Device.dwFrame;
	IRenderVisual* K = owner_attachment->Visual();
	vis_data& vis = K->getVisData();
	Fbox& B = vis.box;
	bv_box.min.average(B.min);
	bv_box.max.average(B.max);
	bv_box.grow(0.05f);
	bv_sphere.P.average(vis.sphere.P);
	bv_sphere.R += vis.sphere.R;
	bv_sphere.R *= 0.5f;
	VERIFY(_valid(bv_sphere));
}

BOOL CCF_Attachment_Skeleton::_RayQuery(const collide::ray_defs& Q, collide::rq_results& R)
{
	if (dwFrameTL != Device.dwFrame) BuildTopLevel();

	Fsphere w_bv_sphere;
	owner_attachment->XFORM().transform_tiny(w_bv_sphere.P, bv_sphere.P);
	w_bv_sphere.R = bv_sphere.R;

	float tgt_dist = Q.range;
	float aft[2];
	int quant;
	Fsphere::ERP_Result res = w_bv_sphere.intersect(Q.start, Q.dir, tgt_dist, quant, aft);
	if ((Fsphere::rpNone == res) || ((Fsphere::rpOriginOutside == res) && (aft[0] > tgt_dist))) return FALSE;

	if (dwFrame != Device.dwFrame) BuildState();
	else
	{
		IKinematics* K = PKinematics(owner_attachment->Visual());
		if (K->LL_GetBonesVisible() != vis_mask)
		{
			// Model changed between ray-picks
			dwFrame = Device.dwFrame - 1;
			BuildState();
		}
	}

	BOOL bHIT = FALSE;
	for (ElementVecIt I = elements.begin(); I != elements.end(); I++)
	{
		if (!I->valid())continue;
		bool res = false;
		float range = Q.range;
		switch (I->type)
		{
		case SBoneShape::stBox:
			res = RAYvsOBB(I->b_IM, I->b_hsize, Q.start, Q.dir, range, Q.flags & CDB::OPT_CULL);
			break;
		case SBoneShape::stSphere:
			res = RAYvsSPHERE(I->s_sphere, Q.start, Q.dir, range, Q.flags & CDB::OPT_CULL);

			break;
		case SBoneShape::stCylinder:
			res = RAYvsCYLINDER(I->c_cylinder, Q.start, Q.dir, range, Q.flags & CDB::OPT_CULL);
			break;
		}
		if (res)
		{
			bHIT = TRUE;
			R.append_result(owner, range, I->elem_id, Q.flags & CDB::OPT_ONLYNEAREST);
			if (Q.flags & CDB::OPT_ONLYFIRST) break;
		}
	}
	return bHIT;
}

script_attachment::script_attachment(LPCSTR name, LPCSTR model_name): ISpatial(g_SpatialSpace)
{
	m_name = name;
	m_physics_shell = nullptr;
	m_parent_attachment = nullptr;
	m_parent_object = nullptr;
	m_toplevel_parent = nullptr;
	m_parent_level = false;
	m_bUsePHShapes = false;
	m_ph_frozen = false;
    m_motion_end_cb = nullptr;
	m_ph_contact_callback_func = nullptr;
	m_script_ui = nullptr;
	m_script_ui_func = 0;
	m_script_ui_mat = Fidentity;
	m_script_ui_offset[0].set(0, 0, 0);
	m_script_ui_offset[1].set(0, 0, 0);
	m_script_ui_offset[2].set(1, 1, 1);
	m_script_ui_offset[3].set(0, 0, 0);
	m_script_ui_scale.set(1, 1);
	m_script_ui_bone = 0;
	m_script_light = nullptr;
	m_script_light_bone = 0;
	m_parent_bone = 0;
	m_offset = Fidentity;
	renderable.visual = nullptr;
	renderable.xform = Fidentity;
    m_previous_matrix = Fidentity;
	m_attachment_offset[0].set(0, 0, 0);
	m_attachment_offset[1].set(0, 0, 0);
	m_attachment_offset[2].set(0, 0, 0);
	m_attachment_offset[3].set(1, 1, 1);
	m_custom_fov = 0.f;
	m_bStopAtEndAnimIsRunning = false;
	m_anim_end = 0;
	m_type = eSA_World;
	m_last_upd_frame = 0;
	m_current_motion = "idle";
	m_model_name = "";
	m_userdata = nullptr;
    m_bInstanceMultirender = false;
	m_model_instance_owner = nullptr;
	m_model_instance_owner_hud = nullptr;
    m_bCamCollision = true;
    m_bVisible = true;
	spatial.type |= STYPE_RENDERABLE;
	LoadModel(model_name);
	PlayMotion("idle", false);
	collidable.model = xr_new<CCF_Attachment_Skeleton>(this);
}

script_attachment* script_attachment::AddAttachment(LPCSTR name, LPCSTR model_name)
{
	script_attachment* att = xr_new<script_attachment>(name, model_name);
	R_ASSERT(att);
	RemoveAttachment(name);
	att->SetParent(this);
	return att;
}

void script_attachment::RemoveAttachment(script_attachment* child)
{
	if (!child || child->m_parent_attachment != this) return;
	RemoveAttachment(child->GetName());
}

void script_attachment::spatial_register()
{
	renderable.xform.transform_tiny(spatial.sphere.P, Visual()->getVisData().sphere.P);
	spatial.sphere.R = Visual()->getVisData().sphere.R * GetScaleMax();
	ISpatial::spatial_register();
}

void script_attachment::spatial_unregister()
{
	ISpatial::spatial_unregister();
}

void script_attachment::spatial_move()
{
	if (!spatial.node_ptr) return;
	renderable.xform.transform_tiny(spatial.sphere.P, Visual()->getVisData().sphere.P);
	spatial.sphere.R = Visual()->getVisData().sphere.R * GetScaleMax();
	ISpatial::spatial_move();
}

void script_attachment::spatial_update(float eps_P, float eps_R)
{
	if (!spatial.node_ptr) return;

	bool bUpdate = false;
	if (PositionStack.empty())
	{
		bUpdate = true;
		PositionStack.push_back(SavedPosition());
		PositionStack.back().dwTime = Device.dwTimeGlobal;
		PositionStack.back().vPosition = Position();
	}
	else
	{
		if (PositionStack.back().vPosition.similar(Position(), eps_P))
			PositionStack.back().dwTime = Device.dwTimeGlobal;
		else
		{
			bUpdate = true;
			if (PositionStack.size() < 4)
			{
				PositionStack.push_back(SavedPosition());
			}
			else
			{
				PositionStack[0] = PositionStack[1];
				PositionStack[1] = PositionStack[2];
				PositionStack[2] = PositionStack[3];
			}
			PositionStack.back().dwTime = Device.dwTimeGlobal;
			PositionStack.back().vPosition = Position();
		}
	}

	if (bUpdate || !fsimilar(Radius(), spatial.sphere.R, eps_R) || !Center().similar(spatial.sphere.P, eps_P))
		spatial_move();
}

void script_attachment::spatial_SetVisible(bool enable)
{
    if (enable)
    {
        m_bVisible = true;
		spatial.type |= STYPE_RENDERABLE;
    }
    else
    {
        m_bVisible = false;
		spatial.type &= ~STYPE_RENDERABLE;
    }
}

bool script_attachment::spatial_GetVisible()
{
	return spatial.type & STYPE_RENDERABLE;
}

void script_attachment::spatial_SetCollidable(bool enable)
{
	if (enable)
		spatial.type |= STYPE_COLLIDEABLE;
	else
		spatial.type &= ~STYPE_COLLIDEABLE;
}

bool script_attachment::spatial_GetCollidable()
{
	return spatial.type & STYPE_COLLIDEABLE;
}

void script_attachment::spatial_SetObstacle(bool enable)
{
	if (enable)
	{
		spatial.type |= STYPE_OBSTACLE;
		spatial.type |= STYPE_VISIBLEFORAI;
	}
	else
	{
		spatial.type &= ~STYPE_OBSTACLE;
		spatial.type &= ~STYPE_VISIBLEFORAI;
	}
}

bool script_attachment::spatial_GetObstacle()
{
	return spatial.type & STYPE_OBSTACLE;
}

void script_attachment::renderable_Render()
{
    if (!m_bVisible) return;
	Visual()->getVisData().hom_frame = Device.dwFrame;

    ::Render->set_Object(this);
	::Render->set_CustomFOV(m_custom_fov);
	::Render->set_Transform(&renderable.xform);
	::Render->add_Visual(Visual());
	::Render->set_CustomFOV(0.f);
    ::Render->set_Object(nullptr);

	RenderInstanceAttachments();

	for (auto& pair : m_children)
		pair.second->renderable_Render();
	}

void script_attachment::RenderUI()
	{
		for (auto& pair : m_children)
		pair.second->RenderUI();

	if (m_script_ui)
		{
		if (::Render->get_generation() == ::Render->GENERATION_R1)
		{
			g_pGamePersistent->AttachmentUIsToRender.push_back(this);
			return;
		}
			
		IUIRender::ePointType bk;

		bk = UI().m_currentPointType;
		UI().m_currentPointType = IUIRender::pttLIT;
		UIRender->CacheSetCullMode(IUIRender::cmNONE);

		Fmatrix LM;
		Fmatrix ui_bone;
		if (Kinematics()->LL_BoneCount() > m_script_ui_bone)
			ui_bone = Kinematics()->LL_GetTransform(m_script_ui_bone);
		else
			ui_bone = Kinematics()->LL_GetTransform(Kinematics()->LL_GetBoneRoot());

		LM.mul(renderable.xform, ui_bone);
		LM.mulB_43(m_script_ui_mat);
		UIRender->CacheSetXformWorld(LM);
		m_script_ui->Draw();

		UIRender->CacheSetCullMode(IUIRender::cmCCW);
		UI().m_currentPointType = bk;
	}
}

void script_attachment::RenderInstanceAttachments()
{
	if (!m_model_instance_owner && !m_model_instance_owner_hud) return;

    script_attachment_type typ = GetType() == eSA_World ? eSA_World : eSA_HUD;
	CGameObject* owner = m_model_instance_owner ? m_model_instance_owner : &m_model_instance_owner_hud->object();

	for (auto& pair : *owner->GetAttachments())
	{
		script_attachment* att = pair.second;
        if (att->GetType() == typ)
        {
            att->UpdateXFORM(Kinematics(), &XFORM());
            att->renderable_Render();
		}
	}
}

void script_attachment::UpdateInstanceAttachments()
{
	if (!m_model_instance_owner && !m_model_instance_owner_hud) return;

    script_attachment_type typ = GetType() == eSA_World ? eSA_World : eSA_HUD;
    CGameObject* owner = m_model_instance_owner ? m_model_instance_owner : &m_model_instance_owner_hud->object();

	for (auto& pair : *owner->GetAttachments())
	{
		script_attachment* att = pair.second;
        if (att->GetType() == typ) att->UpdateXFORM(Visual()->dcast_PKinematics(), &XFORM());
	}
}

void script_attachment::Update()
{
	if (m_last_upd_frame == Device.dwFrame) return;
	m_last_upd_frame = Device.dwFrame;

	if (m_script_ui)
		m_script_ui->Update();

	if (!m_model_instance_owner && !m_model_instance_owner_hud && m_bStopAtEndAnimIsRunning && Device.dwTimeGlobal >= m_anim_end)
	{
		m_bStopAtEndAnimIsRunning = false;

        if (m_motion_end_cb)
        {
            (*m_motion_end_cb)();
            m_motion_end_cb = nullptr;
        }
        else
		PlayMotion("idle");
	}

	UpdateBoneCallbacks();

	IKinematicsAnimated* ka = Visual()->dcast_PKinematicsAnimated();
	if (ka) ka->UpdateTracks();
	Kinematics()->CalculateBones_Invalidate();
	Kinematics()->CalculateBones(TRUE);

	for (auto& pair : m_children)
	{
		pair.second->Update();
	}
}

void script_attachment::UpdateXFORM(IKinematics* model, Fmatrix* mat)
{
    m_previous_matrix = XFORM();

    if (m_parent_level)
    {
        if (m_physics_shell && m_physics_shell->isActive() && !m_ph_frozen)
        {
            m_physics_shell->GetGlobalTransformDynamic(&XFORM());
            m_attachment_offset[0] = Position();
            XFORM().getHPB(m_attachment_offset[1]);
            m_attachment_offset[1].mul(180.f / PI);

            OnUpdateXFORM();
            spatial_update(.25f, .25f);
        }

        return;
    }

    if (!model || (m_bone_callbacks[0] && m_bone_callbacks[0]->m_bone_id != BI_NONE))
        renderable.xform = *mat;
    else
        renderable.xform.mul_43(*mat, BoneTransform(model));

    renderable.xform.mulB_43(m_offset);

    OnUpdateXFORM();

    for (auto& pair : m_children)
    {
        pair.second->UpdateXFORM(Visual()->dcast_PKinematics(), &XFORM());
    }
}

void script_attachment::OnUpdateXFORM()
{
    if (m_script_light)
    {
        Fmatrix LM;
        Fmatrix light_bone;
        if (Kinematics()->LL_BoneCount() > m_script_light_bone)
            light_bone = Kinematics()->LL_GetTransform(m_script_light_bone);
        else
            light_bone = Kinematics()->LL_GetTransform(Kinematics()->LL_GetBoneRoot());

        LM.mul(renderable.xform, light_bone);
        m_script_light->SetXFORM(LM);
    }

    UpdateInstanceAttachments();
}

void script_attachment::ForceUpdateXFORM(bool recursive)
{
    m_previous_matrix = XFORM();

	if (m_parent_level)
	{
		if (m_physics_shell && m_physics_shell->isActive() && !m_ph_frozen)
		{
			m_physics_shell->GetGlobalTransformDynamic(&XFORM());
			m_attachment_offset[0] = Position();
			XFORM().getHPB(m_attachment_offset[1]);
			m_attachment_offset[1].mul(180.f / PI);

			spatial_update(.25f, .25f);
		}
	}
    else if (m_parent_object)
	{
		CActor* actor = smart_cast<CActor*>(m_parent_object);
		CHudItem* itm = smart_cast<CHudItem*>(m_parent_object);

		IKinematics* model = nullptr;
		Fmatrix* matrix = nullptr;

		switch (GetType())
		{
		case eSA_HUD:
			if (actor)
			{
				// Left arm
				if (GetParentBone() < 21)
				{
					model = g_player_hud->m_model_2->dcast_PKinematics();
					matrix = &g_player_hud->m_transform_2;
				}
				// Right arm
				else
				{
					model = g_player_hud->m_model->dcast_PKinematics();
					matrix = &g_player_hud->m_transform;
				}
			}
			else if (itm)
			{
				model = itm->HudItemData()->m_model;
				matrix = itm->m_instance_holder_hud ? &itm->m_instance_holder_hud->GetTransform() : &itm->HudItemData()->m_item_transform;
			}
			else
			{
				matrix = &Fidentity;
			}

			if (!model || (m_bone_callbacks[0] && m_bone_callbacks[0]->m_bone_id != BI_NONE))
				renderable.xform = *matrix;
			else
				renderable.xform.mul_43(*matrix, BoneTransform(model));
			break;
		case eSA_World:
			model = m_parent_object->Visual() ? m_parent_object->Visual()->dcast_PKinematics() : nullptr;
			matrix = m_parent_object->m_instance_holder ? &m_parent_object->m_instance_holder->GetTransform() : & m_parent_object->XFORM();

			if (!model || (m_bone_callbacks[0] && m_bone_callbacks[0]->m_bone_id != BI_NONE))
				renderable.xform = *matrix;
			else
				renderable.xform.mul_43(*matrix, BoneTransform(model));
			break;
		case eSA_CamAttached:
			if (actor) actor->Cameras().camera_Matrix(renderable.xform);
			break;
		default:
			renderable.xform.identity();
		}

		renderable.xform.mulB_43(m_offset);
	}
	else if (m_parent_attachment)
	{
		renderable.xform.mul_43(m_parent_attachment->XFORM(), BoneTransform(m_parent_attachment->Visual()->dcast_PKinematics()));
		renderable.xform.mulB_43(m_offset);
	}
    else
    {
        renderable.xform.identity();
        renderable.xform.mulB_43(m_offset);
    }

    if (recursive)
        for (auto& att : m_children)
            att.second->ForceUpdateXFORM(true);
}

void script_attachment::RefreshVisBoxChain(u16 type)
{
	// Parent visual callbacks consume child boxes, so refresh the hierarchy bottom-up.
	for (script_attachment* attachment = this; attachment; attachment = attachment->m_parent_attachment)
	{
		IKinematics* kinematics = attachment->Kinematics();
		if (!kinematics)
			continue;

		kinematics->CalculateBones_Invalidate();
		kinematics->CalculateBones(TRUE);
	}

	if (m_toplevel_parent)
		m_toplevel_parent->RefreshAttachmentVisBox(type);
}

void script_attachment::UpdateBoneCallbacks()
	{
		for (auto& pair : m_bone_callbacks)
		{
			script_attachment_bone_cb* cb = pair.second;
			if (!cb) continue;
			
			if (cb->m_func)
			{
				cb->m_mat.set((*(cb->m_func))(
				Kinematics()->LL_GetBoneInstance(cb->m_attachment_bone_id).mTransformHidden, //Transform without bone callback modifier
				Kinematics()->LL_BoneName_dbg(pair.first)));

				continue;
			}
			
			Fmatrix& target = cb->m_mat;
			u16 bone = cb->m_bone_id;

			if (m_parent_object)
			{
				if (GetType() == eSA_HUD)
				{
					CHudItem* itm = smart_cast<CHudItem*>(m_parent_object);
					if (itm)
					{
						if (bone >= itm->HudItemData()->m_model->LL_BoneCount())
							target = Fidentity;
						else
							target = itm->HudItemData()->m_model->LL_GetTransform(bone);

						continue;
					}

					CActor* act = smart_cast<CActor*>(m_parent_object);
					if (act)
					{
						if (bone >= g_player_hud->m_model->dcast_PKinematics()->LL_BoneCount())
							target = Fidentity;
						else
							target = (bone < 21) ?
							g_player_hud->m_model_2->dcast_PKinematics()->LL_GetTransform(bone) :
							g_player_hud->m_model->dcast_PKinematics()->LL_GetTransform(bone);

						continue;
					}
				}

				if (bone >= m_parent_object->Visual()->dcast_PKinematics()->LL_BoneCount())
					target = Fidentity;
				else
					target = m_parent_object->Visual()->dcast_PKinematics()->LL_GetTransform(bone);

				continue;
			}

			if (m_parent_attachment)
			{
			if (bone >= m_parent_attachment->Kinematics()->LL_BoneCount())
					target = Fidentity;
				else
				target = m_parent_attachment->Kinematics()->LL_GetTransform(bone);
		}
	}
}

void script_attachment::AttachLight(AttachmentScriptLight* light)
{ 
	R_ASSERT(light);
	m_script_light = light;
}

AttachmentScriptLight* script_attachment::DetachLight()
{
	if (!m_script_light) return nullptr;
	AttachmentScriptLight* ret = m_script_light;
	m_script_light = nullptr;
	return ret;
}

AttachmentScriptLight* script_attachment::GetLight()
{
	if (!m_script_light) return nullptr;
	return m_script_light;
}

void script_attachment::RecalcOffset()
{
	const Fvector& position = m_attachment_offset[0];
	Fvector rotation = m_attachment_offset[1];
	const Fvector& origin = m_attachment_offset[2];

	rotation.mul(PI / 180.f);

	if (!!origin.x || !!origin.y || !!origin.z)
	{
		m_offset.translate(-origin.x, -origin.y, -origin.z);
		m_offset.mulA_43(Fmatrix().setHPB(rotation));
		if (!m_parent_level) m_offset.mulA_43(Fmatrix().scale(m_attachment_offset[3]));
		m_offset.mulA_43(Fmatrix().translate(position));
	}
	else
	{
		m_offset.setHPB(rotation);
		m_offset.translate_over(position);
		if (!m_parent_level) m_offset.mulB_43(Fmatrix().scale(m_attachment_offset[3]));
	}

	if (m_parent_level)
	{
		XFORM().set(m_offset);

		if (m_physics_shell)
		{
			m_physics_shell->SetGlTransformDynamic(XFORM());
			m_physics_shell->mXFORM.set(XFORM());
			m_physics_shell->Activate(XFORM(), 0, XFORM());
			m_physics_shell->Enable();

			if (m_ph_frozen)
			{
				u16 max_elements = m_physics_shell->get_ElementsNumber();
				for (u16 i = 0; i < max_elements; ++i)
				{
					m_physics_shell->get_ElementByStoreOrder(i)->Fix();
				}
			}

			m_physics_shell->GetGlobalTransformDynamic(&XFORM());
		}

		spatial_move();
	}

	// Recreate physics shapes if they were previously attached to the parent object
	if (m_toplevel_parent)
	{
		OnParentDestroyPhysicsShell();
		OnParentCreatePhysicsShell();
	}

	RefreshVisBoxChain(m_type);
}

void script_attachment::SetPosition(float x, float y, float z)
{
	m_attachment_offset[0].set(x, y, z);
	RecalcOffset();
}

void script_attachment::SetRotation(float x, float y, float z)
{
	m_attachment_offset[1].set(x, y, z);
	RecalcOffset();
}

void script_attachment::SetScale(Fvector scale)
{
    m_attachment_offset[3].set(scale);

    if (m_parent_level)
    {
        if (m_physics_shell)
        {
            m_physics_shell->SetScale(m_attachment_offset[3]);
            RecalcOffset();
            m_physics_shell->Enable();
        }

        return;
    }

    RecalcOffset();
}

void script_attachment::SetOrigin(float x, float y, float z)
{
	m_attachment_offset[2].set(x, y, z);
	RecalcOffset();
}

void script_attachment::SetParent(script_attachment* att)
{
	if (!att)
		return;

	if (m_parent_attachment)
	{
		if (m_parent_attachment == att)
			return;

        if (m_toplevel_parent) OnParentDestroyPhysicsShell();
		m_parent_attachment->RemoveChild(GetName());
	}

	if (m_parent_object)
		m_parent_object->remove_child(GetName());

	if (m_parent_level)
	{
		Level().remove_child(GetName());

	spatial_unregister();
	}
	
	m_parent_level = false;
	m_parent_object = nullptr;
	m_parent_attachment = att;
	m_parent_attachment->AddChild(GetName(), this);
	SetTopLevelParent(m_parent_attachment->GetTopLevelParent());
	OnParentCreatePhysicsShell();
}

void script_attachment::SetParent(CGameObject* obj)
{
	if (!obj)
		return;

	if (m_toplevel_parent) OnParentDestroyPhysicsShell();

	if (m_parent_attachment)
		m_parent_attachment->RemoveChild(GetName());

	if (m_parent_object)
	{
		if (m_parent_object == obj)
			return;

		m_parent_object->remove_child(GetName());
	}
	
	if (m_parent_level)
	{
		Level().remove_child(GetName());

	spatial_unregister();
	}

	m_parent_level = false;
	m_parent_object = obj;
	m_parent_attachment = nullptr;
	m_parent_object->add_attachment(GetName(), this);
	SetTopLevelParent(m_parent_object);
	OnParentCreatePhysicsShell();
}

void script_attachment::SetParent(CScriptGameObject* obj)
{
	if (!obj || !&obj->object())
		return;

	SetParent(&obj->object());
}

void script_attachment::SetParentLevel()
{
	if (m_parent_level)
		return;

	if (m_toplevel_parent) OnParentDestroyPhysicsShell();

	if (m_parent_attachment)
		m_parent_attachment->RemoveChild(GetName());

	if (m_parent_object)
		m_parent_object->remove_child(GetName());

	m_parent_level = true;
	m_parent_object = nullptr;
	m_parent_attachment = nullptr;
	SetTopLevelParent(nullptr);
	Level().add_attachment(GetName(), this);
	spatial_register();
	spatial_move();
	CreatePhysicsShell();

    CROS_impl* impl = smart_cast<CROS_impl*>(renderable_ROS());
    CROS_impl* ac_impl = smart_cast<CROS_impl*>(g_actor ? g_actor->renderable_ROS() : nullptr);
    if (!impl || !ac_impl) return;

    // Initialize the attachment with the actor's current lighting.
    // Only copy lighting sources (without track and lights) to avoid double memory free when level is destroyed
    impl->dwFrameSmooth = ac_impl->dwFrameSmooth;
    impl->hemi_value = ac_impl->hemi_value;
    impl->hemi_smooth = ac_impl->hemi_smooth;
    impl->sun_value = ac_impl->sun_value;
    impl->sun_smooth = ac_impl->sun_smooth;
    impl->approximate = ac_impl->approximate;
    CopyMemory(impl->hemi_cube, ac_impl->hemi_cube, sizeof(impl->hemi_cube));
    CopyMemory(impl->hemi_cube_smooth, ac_impl->hemi_cube_smooth, sizeof(impl->hemi_cube_smooth));
}

void script_attachment::SetTopLevelParent(CGameObject* obj)
{
	m_toplevel_parent = obj;

	for (auto& pair : m_children)
		pair.second->SetTopLevelParent(obj);
}

extern bool has_physics_collision_shapes(IKinematics& K);

void script_attachment::CreatePhysicsShell()
{
	if (m_physics_shell)
	{
		m_physics_shell->Deactivate();
		xr_delete(m_physics_shell);
	}

	if (!has_physics_collision_shapes(*Kinematics()))
	{
		Msg("[Script Attachment]: Failed to create physics shell!");
		return;
	}

	m_physics_shell = P_create_Shell();
	m_physics_shell->build_FromKinematics(Kinematics());
	m_physics_shell->mXFORM.set(XFORM());
	m_physics_shell->Activate(XFORM(), 0, XFORM());
	m_physics_shell->SetAirResistance();
	m_physics_shell->SetScale(m_attachment_offset[3]);

	if (m_ph_frozen)
	{
		u16 max_elements = m_physics_shell->get_ElementsNumber();
		for (u16 i = 0; i < max_elements; ++i)
		{
			m_physics_shell->get_ElementByStoreOrder(i)->Fix();
		}
	}
	
	m_physics_shell->collide_class_bits().set(CPHCollideValidator::ETypeFlags::cbNCStatic, m_ph_frozen); //static collision

    for (auto& elem : m_physics_shell->Elements())
    {
        for (int i = 0; i < elem->numberOfGeoms(); ++i)
        {
            CODEGeom* geom = elem->Geom(i);
            if (!geom) continue;

            dGeomID id = geom->geometry();
            if (id && dGeomGetUserData(id))
                dGeomUserDataSetPhysicsRefAttachment(id, this);
        }
    }
}

void script_attachment::SetPHFreeze(bool freeze)
{
	if (!m_physics_shell) return;
	if (m_ph_frozen == freeze) return;

	m_ph_frozen = freeze;

	u16 max_elements = m_physics_shell->get_ElementsNumber();
	for (u16 i = 0; i < max_elements; ++i)
	{
		if (m_ph_frozen)
			m_physics_shell->get_ElementByStoreOrder(i)->Fix();
		else
			m_physics_shell->get_ElementByStoreOrder(i)->ReleaseFixed();
	}

	m_physics_shell->collide_class_bits().set(CPHCollideValidator::ETypeFlags::cbNCStatic, m_ph_frozen); //static collision
}

cphysics_shell_scripted* script_attachment::get_physics_shell() const
{
	if (!m_physics_shell) return nullptr;
	return get_script_wrapper<cphysics_shell_scripted>(*m_physics_shell);
}

void script_attachment::SetPHContactCallback(LPCSTR func)
{
	if (!m_physics_shell) return;

	if (!func || !xr_strlen(func))
	{
		m_physics_shell->set_ObjectContactCallback(nullptr);
		xr_delete(m_ph_contact_callback_func);
		return;
	}

	m_ph_contact_callback_func = xr_new<::luabind::functor<bool>>();
	if (ai().script_engine().functor(func, *m_ph_contact_callback_func))
	{
		m_physics_shell->set_ObjectContactCallback(ObjectContactCallback);
	}
	else
	{
		m_physics_shell->set_ObjectContactCallback(nullptr);
		xr_delete(m_ph_contact_callback_func);
	}
}

void script_attachment::SetPHContactCallback(const ::luabind::functor<bool>& func)
{
	xr_delete(m_ph_contact_callback_func);

	if (!func)
	{
		m_physics_shell->set_ObjectContactCallback(nullptr);
		return;
	}

	m_ph_contact_callback_func = xr_new<::luabind::functor<bool>>(func);
	m_physics_shell->set_ObjectContactCallback(ObjectContactCallback);
}

void script_attachment::ObjectContactCallback(bool& do_colide, bool bo1, dContact& c, SGameMtl* material_1,
	SGameMtl* material_2)
{
	dxGeomUserData* l_pUD1 = nullptr;
	dxGeomUserData* l_pUD2 = nullptr;
	l_pUD1 = PHRetrieveGeomUserData(bo1 ? c.geom.g1 : c.geom.g2);
	l_pUD2 = PHRetrieveGeomUserData(bo1 ? c.geom.g2 : c.geom.g1);

	SGameMtl* material = 0;
	script_attachment* l_this = l_pUD1 ? smart_cast<script_attachment*>(l_pUD1->ph_ref_attachment) : nullptr;
	CGameObject* l_other = l_pUD2 ? smart_cast<CGameObject*>(l_pUD2->ph_ref_object) : nullptr;

    if (!l_this) return;
	if (!l_this->m_ph_contact_callback_func) return;

	Fvector normal;
	if (bo1)
	{
        normal.set(*(Fvector*)&c.geom.normal);
        material = material_2;
	}
	else
	{
        normal.invert(*(Fvector*)&c.geom.normal);
        material = material_1;
	}

	Fvector velocity;
	l_this->m_physics_shell->get_LinearVel(velocity);

	do_colide = (*l_this->m_ph_contact_callback_func)(l_this, l_this->XFORM().c, velocity, l_other ? l_other->lua_game_object() : nullptr, *material->m_Name, !!material->Flags.test(SGameMtl::flPassable), normal);
}

extern Flags32 psDeviceFlags2;

void script_attachment::DBGDrawPhysicsShell()
{
	if (!m_physics_shell) return;

	if (psDeviceFlags2.test(rsDBGDrawPHShapes))
	{
		for (auto& elem : m_physics_shell->Elements())
		{
			Fmatrix cent_mat;
			cent_mat.translate(elem->mass_Center());
			cent_mat.mulB_43(Fmatrix().scale(0.025, 0.025, 0.025));
			Level().debug_renderer().draw_ellipse(cent_mat, color_xrgb(213, 61, 165), false);

			for (int i = 0; i < elem->numberOfGeoms(); ++i)
			{
				CODEGeom* geom = elem->Geom(i);
				if (!geom) continue;

				CBoxGeom* box = smart_cast<CBoxGeom*>(geom);
				CSphereGeom* sphere = smart_cast<CSphereGeom*>(geom);
				CCylinderGeom* cylinder = smart_cast<CCylinderGeom*>(geom);

				if (box)
				{
					Fmatrix xform;
					Fvector sz;
					box->get_Box(xform, sz);
					sz.div(2);
					Level().debug_renderer().draw_obb(xform, sz, color_xrgb(255, 0, 0), false);
					continue;
				}

				if (sphere)
				{
					Fmatrix xform;
					sphere->get_xform(xform);
					float rad = sphere->radius();
					xform.mulB_43(Fmatrix().scale(rad, rad, rad));
					Level().debug_renderer().draw_ellipse(xform, color_xrgb(0, 0, 255), false);
					continue;
				}

				if (cylinder)
				{
					Fmatrix xform;
					cylinder->get_Box(xform, Fvector());
					Level().debug_renderer().draw_cylinder(xform, cylinder->height(), cylinder->radius(), color_xrgb(255, 0, 255), false);
					continue;
				}
			}
		}
	}

	if (psDeviceFlags2.test(rsDBGDrawPHBones))
	{
		for (const auto& bone : *Kinematics()->LL_Bones())
		{
			Fmatrix bone_mat;
			bone_mat.mul_43(XFORM(), Kinematics()->LL_GetTransform(bone.second));
			bone_mat.mulB_43(Fmatrix().scale(0.025, 0.025, 0.025));
			Level().debug_renderer().draw_ellipse(bone_mat, color_xrgb(0, 255, 0), false);
		}
	}
}

::luabind::object script_attachment::GetParent()
{
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());
	table["object"] = m_parent_object ? m_parent_object->lua_game_object() : nullptr;
	table["attachment"] = m_parent_attachment ? m_parent_attachment : nullptr;
	table["level"] = m_parent_level;
	return table;
}

script_attachment* script_attachment::AddChild(LPCSTR name, script_attachment* att)
{
	R_ASSERT(att);
	RemoveChild(name, true);
	m_children.emplace(mk_pair(name, att));
	RefreshVisBoxChain(att->GetType());
	return att;
}

script_attachment* script_attachment::GetChild(LPCSTR name)
{
		auto& pair = m_children.find(name);
		if (pair != m_children.end())
			return pair->second;

	return nullptr;
}

void script_attachment::RemoveChild(LPCSTR name, bool destroy)
{
	auto& pair = m_children.find(name);
	if (pair == m_children.end())
		return;
	
	script_attachment* attachment = pair->second;
	const u16 attachment_type = attachment->GetType();

	attachment->OnParentDestroyPhysicsShell();

	if (destroy)
		xr_delete(attachment);

	m_children.erase(pair);
	RefreshVisBoxChain(attachment_type);
}

void script_attachment::SetType(u16 type)
{
	const u16 new_type = type < eSA_undefined ? type : eSA_World;
	if (m_type == new_type)
		return;

	const u16 old_type = m_type;
	m_type = new_type;
	RefreshVisBoxChain(old_type);
	RefreshVisBoxChain(new_type);
}

void script_attachment::IterateAttachments(::luabind::functor<bool> functor)
{
	for (auto& pair : m_children)
		if (functor(pair.first.c_str(), pair.second) == true)
			return;
}

Fmatrix& script_attachment::BoneTransform(IKinematics* model)
{
	if (model->LL_BoneCount() > m_parent_bone)
		return model->LL_GetTransform(m_parent_bone);

	return model->LL_GetTransform(model->LL_GetBoneRoot());
}

u32 script_attachment::PlayMotionCB(LPCSTR name, bool mixin, float speed, const ::luabind::functor<void>& motion_end_cb)
{
    u32 length = PlayMotion(name, mixin, speed);
    if (length != 0 && motion_end_cb)
        m_motion_end_cb = xr_new<::luabind::functor<void>>(motion_end_cb);

    return length;
}

u32 script_attachment::PlayMotionCB(LPCSTR name, bool mixin, float speed, LPCSTR motion_end_cb)
{
    u32 length = PlayMotion(name, mixin, speed);
    if (length != 0 && motion_end_cb != 0 && xr_strlen(motion_end_cb) != 0)
    {
        m_motion_end_cb = xr_new<::luabind::functor<void>>();
        if (!ai().script_engine().functor(motion_end_cb, *m_motion_end_cb))
            xr_delete(m_motion_end_cb);
    }

    return length;
}

u32 script_attachment::PlayMotion(LPCSTR name, bool mixin, float speed)
{
	IKinematicsAnimated* k = Kinematics()->dcast_PKinematicsAnimated();

	if (!k)
		return 0;

	MotionID M2 = k->ID_Cycle_Safe(name);
	if (!M2.valid())
		M2 = k->ID_Cycle_Safe("idle");

	if (!M2.valid())
		return 0;

	u16 pc = k->partitions().count();
	for (u16 pid = 0; pid < pc; ++pid)
		CBlend* B = k->PlayCycle(pid, M2, mixin, 0, 0, 0, speed);

	const CMotionDef* md;
	u32 length = motion_length(M2, md, speed);

	if (length > 0)
	{
		m_bStopAtEndAnimIsRunning = true;
		m_anim_end = Device.dwTimeGlobal + length;
	}
	else
		m_bStopAtEndAnimIsRunning = false;

	m_current_motion = name;
    xr_delete(m_motion_end_cb);

	k->UpdateTracks();
	Kinematics()->CalculateBones_Invalidate();
	Kinematics()->CalculateBones(true);

	return length;
}

u32 script_attachment::motion_length(const MotionID& M, const CMotionDef*& md, float speed)
{
	IKinematicsAnimated* k = Kinematics()->dcast_PKinematicsAnimated();

	if (!k)
		return 0;

	md = k->LL_GetMotionDef(M);
	VERIFY(md);
	if (md->flags & esmStopAtEnd)
	{
		CMotion* motion = k->LL_GetRootMotion(M);
		return iFloor(0.5f + 1000.f * motion->GetLength() / (md->Dequantize(md->speed) * speed));
	}
	return 0;
}

void script_attachment::AddToParentShell(CGameObject* obj)
{
	if (GetType() != eSA_World) return;

	CPhysicsShellHolder* holder = obj->cast_physics_shell_holder();
	if (!holder) return;
	if (!has_physics_collision_shapes(*Kinematics())) return;

	CCharacterPhysicsSupport* phsupport = holder->character_physics_support();
	CPhysicsShell* shell = phsupport ? phsupport->physics_shell() : holder->m_pPhysicsShell;
	if (!shell || !shell->isActive()) return;

	// Force update transform if adding physics shapes right after net_spawn
	if (obj->spawn_time() == Device.dwFrame)
		shell->InterpolateGlobalTransform(&holder->XFORM());

	ForceUpdateXFORM();

	// Physics shell matrix has to be unscaled or the resulting physics shapes will have incorrect rotations
	Fmatrix mtx = XFORM();
	mtx.i.normalize();
	mtx.j.normalize();
	mtx.k.normalize();

	Fvector new_scale;
    new_scale.set(1, 1, 1);

	script_attachment* parent = m_parent_attachment;

	while (parent)
	{
		new_scale.mul(parent->GetScale());
		parent = parent->m_parent_attachment;
	}

	new_scale.mul(GetScale());

	CPhysicsShell* attachment_shell = P_create_Shell();
	attachment_shell->build_FromKinematics(Kinematics());
	attachment_shell->mXFORM.set(mtx);
	attachment_shell->Activate(true, true);
	attachment_shell->SetAirResistance();

	CPhysicsElement* attachment_bone = shell->get_PhysicsParrentElement(m_parent_bone);
	if (!attachment_bone)
	{
		destroy_physics_shell(attachment_shell);
		return;
	}

    GEOM_STORAGE temp_geoms;

    for (CPHElement* elem : attachment_shell->Elements())
    {
        u16 geom_num = elem->numberOfGeoms();
        for (u16 i = 0; i < geom_num; ++i)
        {
            temp_geoms.push_back(elem->geometry(i));
            m_attachment_geoms.push_back(elem->geometry(i));
        }

        Fvector center = elem->local_mass_Center();

        for (auto* geom : temp_geoms)
        {
            CBoxGeom* box = smart_cast<CBoxGeom*>(geom);
            CSphereGeom* sphere = smart_cast<CSphereGeom*>(geom);
            CCylinderGeom* cylinder = smart_cast<CCylinderGeom*>(geom);

            if (box)
            {
                Fvector half_size;
                box->get_size(half_size);
                half_size.mul(new_scale.x, new_scale.z, new_scale.y);
                box->set_size(half_size);
            }

            if (sphere)
            {
                sphere->set_radius(sphere->radius() * GetScaleMax());
            }

            if (cylinder)
            {
                cylinder->set_radius(cylinder->radius() * fmaxf(m_attachment_offset[3].x, m_attachment_offset[3].y));
                cylinder->set_height(cylinder->height() * m_attachment_offset[3].z);
            }

            Fvector scaled_pos = geom->local_center();
            scaled_pos.mul(new_scale);
            scaled_pos.sub(center);
            
            dGeomSetPosition(geom->geom(), scaled_pos.x, scaled_pos.y, scaled_pos.z);

            elem->remove_geom(geom);
            geom->set_bone_id(attachment_bone->m_SelfID);
            attachment_bone->add_geom(geom);
        }

        temp_geoms.clear_not_free();
    }

    destroy_physics_shell(attachment_shell);
    shell->Enable();
}

void script_attachment::RemoveFromParentShell(CGameObject* obj)
{
	if (GetType() != eSA_World) return;
	if (!m_attachment_geoms.size()) return;

	CPhysicsShellHolder* holder = obj->cast_physics_shell_holder();
	if (!holder) return;

	CCharacterPhysicsSupport* phsupport = holder->character_physics_support();
	CPhysicsShell* shell = phsupport ? phsupport->physics_shell() : holder->m_pPhysicsShell;
	if (!shell || !shell->isActive()) return;

	CPhysicsElement* attachachment_bone = shell->get_PhysicsParrentElement(m_parent_bone);
	if (attachachment_bone)
	{
		for (auto* geom : m_attachment_geoms)
		{
			attachachment_bone->remove_geom(geom);
			geom->destroy();
			xr_delete(geom);
		}
	}

	m_attachment_geoms.clear();
}

void script_attachment::OnParentCreatePhysicsShell()
{
	if (!m_toplevel_parent) return;
	if (m_bUsePHShapes) AddToParentShell(m_toplevel_parent);

	for (auto& pair : m_children)
		pair.second->OnParentCreatePhysicsShell();
}

void script_attachment::OnParentDestroyPhysicsShell()
{
	if (!m_toplevel_parent) return;
	RemoveFromParentShell(m_toplevel_parent);

	for (auto& pair : m_children)
		pair.second->OnParentDestroyPhysicsShell();
}

void script_attachment::SetUsePHShapes(bool b)
{
	if (m_bUsePHShapes == b || !m_toplevel_parent) return;

	m_bUsePHShapes = b;

	if (m_bUsePHShapes)
		AddToParentShell(m_toplevel_parent);
	else
		RemoveFromParentShell(m_toplevel_parent);
}

void script_attachment::SetBoneVisible(u16 bone_id, bool bVisibility, bool bRecursive)
{
	if (Kinematics()->LL_BoneCount() > bone_id)
	{
		bool bVisibleNow = Kinematics()->LL_GetBoneVisible(bone_id);
		if (bVisibleNow != bVisibility)
		{
			Kinematics()->LL_SetBoneVisible(bone_id, bVisibility, TRUE);
			RefreshVisBoxChain(m_type);

			if (m_physics_shell) CreatePhysicsShell();
			if (m_toplevel_parent && m_bUsePHShapes)
			{
				RemoveFromParentShell(m_toplevel_parent);
				AddToParentShell(m_toplevel_parent);
			}
		}
	}
}

bool script_attachment::GetBoneVisible(u16 bone_id)
{
	if (Kinematics()->LL_BoneCount() > bone_id)
		return Kinematics()->LL_GetBoneVisible(bone_id);

	return false;
}

void script_attachment::SetParentBone(LPCSTR bone)
{
	u16 bone_id = 0;

	if (m_parent_object) bone_id = m_parent_object->lua_game_object()->bone_id(bone, m_type == eSA_HUD);
	else if (m_parent_attachment) bone_id = m_parent_attachment->bone_id(bone);

	SetParentBone(bone_id);
}

void script_attachment::SetParentBone(u16 bone_id)
	{
	if (m_parent_bone == bone_id) return;

	// Recreate physics shapes if they were previously attached to the parent object
	if (m_toplevel_parent)
	{
		OnParentDestroyPhysicsShell();
		m_parent_bone = bone_id;
		OnParentCreatePhysicsShell();
	}

	m_parent_bone = bone_id;
	RefreshVisBoxChain(m_type);
}

u16 script_attachment::bone_id(LPCSTR bone_name)
{
	u16 bone_id = BI_NONE;
	if (xr_strlen(bone_name))
		bone_id = Kinematics()->LL_BoneID(bone_name);

	return bone_id;
}

extern BOOL print_bone_warnings;

Fmatrix script_attachment::bone_transform(u16 bone_id, bool bPrev)
{
	if (bone_id == BI_NONE || bone_id >= Kinematics()->LL_BoneCount()) {
		if (strstr(Core.Params, "-dbg") && print_bone_warnings) {
			Msg("![bone_position] Incorrect bone_id provided for %s (%s), fallback to root bone", GetName(), GetModelScript());
			ai().script_engine().print_stack();
		}
		bone_id = Kinematics()->LL_GetBoneRoot();
	}

	Fmatrix matrix;
	matrix.mul_43(bPrev ? XFORM_prev() : XFORM(), Kinematics()->LL_GetTransform(bone_id));
	return matrix;
}

Fvector script_attachment::bone_position(u16 bone_id)
{
	return bone_transform(bone_id).c;
}

Fvector script_attachment::bone_direction(u16 bone_id)
{
	Fmatrix matrix = bone_transform(bone_id);
	Fvector res;
	matrix.getHPB(res);
	return res;
}

u16 script_attachment::bone_parent(u16 bone_id)
{
	if (bone_id == Kinematics()->LL_GetBoneRoot() || bone_id >= Kinematics()->LL_BoneCount()) return BI_NONE;

	CBoneData* data = &Kinematics()->LL_GetData(bone_id);
	u16 ParentID = data->GetParentID();
	return ParentID;
}

LPCSTR script_attachment::bone_name(u16 bone_id)
{
	if (bone_id == BI_NONE) return "";
	return (Kinematics()->LL_BoneName_dbg(bone_id));
}

// demonized: list all bones
::luabind::object script_attachment::list_bones()
{
	::luabind::object result = ::luabind::newtable(ai().script_engine().lua());

	for (const auto& bone : *Kinematics()->LL_Bones())
		result[bone.second] = bone.first.c_str();

	return result;
}

void set_skip_marker(IRenderVisual* vis, bool skip)
{
    R_ASSERT(vis);
    vis->getVisData().skip_marker = skip;
    for (auto* child : *vis->get_children())
        child->getVisData().skip_marker = skip;
}

void script_attachment::LoadModel(LPCSTR model_name, bool keep_bc)
{
	if (m_model_name == model_name)
		return;

	m_model_name = model_name;

	u16 count_prev = 0;

	if (renderable.visual)
	{
		count_prev = renderable.visual->dcast_PKinematics()->LL_BoneCount();
		if (m_toplevel_parent) OnParentDestroyPhysicsShell();
		::Render->model_Delete(renderable.visual);
	}
	
	renderable.visual = ::Render->model_Create(*m_model_name);
	R_ASSERT(renderable.visual);
	R_ASSERT(renderable.visual->dcast_PKinematics());

	if (m_toplevel_parent) OnParentCreatePhysicsShell();
	if (m_physics_shell) CreatePhysicsShell();

	spatial_move();

	// Bone Callbacks
	if (keep_bc && m_bone_callbacks.size() && count_prev <= renderable.visual->dcast_PKinematics()->LL_BoneCount())
	{
		for (auto& pair : m_bone_callbacks)
		{
			renderable.visual->dcast_PKinematics()->LL_GetBoneInstance(pair.first).set_callback(bctCustom, ScriptAttachmentBoneCallback, pair.second, pair.second->m_overwrite);
		}

		PlayMotion(*m_current_motion, false);
	}

	// Visibility Box Update
	renderable.visual->dcast_PKinematics()->SetUpdateCallback(update_visbox_attachment);
	renderable.visual->dcast_PKinematics()->SetUpdateCallbackParam(this);

    // Skip render marker check (allows rendering the same model multiple times)
    set_skip_marker(Visual(), true);

    // Make sure bones are at the correct position
    Kinematics()->CalculateBones_Invalidate();
    Kinematics()->CalculateBones(true);
	RefreshVisBoxChain(m_type);
}

void script_attachment::SetModelInstanceMultirender(bool val)
{
    if (m_bInstanceMultirender == val) return;

    m_bInstanceMultirender = val;
    if (m_model_instance_owner) set_skip_marker(m_model_instance_owner->Visual(), m_bInstanceMultirender);
    if (m_model_instance_owner_hud) set_skip_marker(m_model_instance_owner_hud->HudItemData()->m_model->dcast_RenderVisual(), m_bInstanceMultirender);
}

void script_attachment::ModelInstance(CScriptGameObject* obj, bool bHud)
{
	if (!obj)
	{
		ClearModelInstance();
		return;
	}
	
	CGameObject* go = &obj->object();
	CHudItem* itm = go->cast_hud_item();

	if (bHud && itm)
	{
		// Remove instance from previous hud holder
		script_attachment* holder = itm->m_instance_holder_hud;
		if (holder) holder->ClearModelInstance();

		itm->m_instance_holder_hud = this;
		m_model_instance_owner_hud = itm;
        set_skip_marker(m_model_instance_owner_hud->HudItemData()->m_model->dcast_RenderVisual(), m_bInstanceMultirender);
		return;
	}

	// Remove instance from previous holder
	script_attachment* holder = go->m_instance_holder;
	if (holder) holder->ClearModelInstance();

	go->m_instance_holder = this;
	m_model_instance_owner = go;
    set_skip_marker(m_model_instance_owner->Visual(), m_bInstanceMultirender);

	if (m_physics_shell) CreatePhysicsShell();
}

void script_attachment::ModelInstance(::luabind::object obj, bool bHud)
{
	if (!obj || obj.type() != LUA_TUSERDATA)
	{
		ClearModelInstance();
		return;
	}
	
	CScriptGameObject* _obj = ::luabind::object_cast<CScriptGameObject*>(obj);
	if (!_obj)
	{
		ClearModelInstance();
		return;
	}

	CGameObject* go = &_obj->object();
	CHudItem* itm = go->cast_hud_item();

	if (bHud && itm)
	{
		// Remove instance from previous hud holder
		script_attachment* holder = itm->m_instance_holder_hud;
		if (holder) holder->ClearModelInstance();

		itm->m_instance_holder_hud = this;
		m_model_instance_owner_hud = itm;
        set_skip_marker(m_model_instance_owner_hud->HudItemData()->m_model->dcast_RenderVisual(), m_bInstanceMultirender);
		return;
	}

	// Remove instance from previous holder
	script_attachment* holder = go->m_instance_holder;
	if (holder) holder->ClearModelInstance();

	go->m_instance_holder = this;
	m_model_instance_owner = go;
    set_skip_marker(m_model_instance_owner->Visual(), m_bInstanceMultirender);

	if (m_physics_shell) CreatePhysicsShell();
}

void script_attachment::ClearModelInstance()
{
	if (!m_model_instance_owner && !m_model_instance_owner_hud) return;

    if (m_model_instance_owner)
    {
        set_skip_marker(m_model_instance_owner->Visual(), false);
        m_model_instance_owner->m_instance_holder = nullptr;
    }

    if (m_model_instance_owner_hud)
    {
        set_skip_marker(m_model_instance_owner_hud->HudItemData()->m_model->dcast_RenderVisual(), false);
        m_model_instance_owner_hud->m_instance_holder_hud = nullptr;
    }

	m_model_instance_owner = nullptr;
	m_model_instance_owner_hud = nullptr;

	if (m_physics_shell) CreatePhysicsShell();
}

void script_attachment::SetName(LPCSTR name)
{
	if (m_parent_level)
	{
		// Remove old instance from parent (without destroying it)
		Level().remove_child(GetName(), false);

		auto& pair = Level().GetAttachments()->find(name);
		if (pair != Level().GetAttachments()->end())
		{
			// Attachment with name exists, replace it
			xr_delete(pair->second);
			pair->second = this;
		}
		else
		{
			// Attachment with name does not exist, add it
			Level().add_attachment(name, this);
		}
	}
	else if (m_parent_object)
	{
		// Remove old instance from parent (without destroying it)
		m_parent_object->remove_child(GetName(), false);

		auto& pair = m_parent_object->GetAttachments()->find(name);
		if (pair != m_parent_object->GetAttachments()->end())
		{
			// Attachment with name exists, replace it
			xr_delete(pair->second);
			pair->second = this;
		}
		else
		{
			// Attachment with name does not exist, add it
			m_parent_object->add_attachment(name, this);
		}
	}

	else if (m_parent_attachment)
	{
		// Remove old instance from parent (without destroying it)
		m_parent_attachment->RemoveChild(GetName(), false);

		auto& pair = m_parent_attachment->m_children.find(name);
		if (pair != m_parent_attachment->m_children.end())
		{
			// Attachment with name exists, replace it
			xr_delete(pair->second);
			pair->second = this;
		}
		else
		{
			// Attachment with name does not exist, add it
			m_parent_attachment->AddChild(name, this);
		}
	}

	m_name = name;
}

const ::luabind::object& script_attachment::GetUserdata() const
{
	if (!m_userdata)
	{
		const_cast<::luabind::object*>(m_userdata) = xr_new<::luabind::object>();
		*m_userdata = ::luabind::newtable(ai().script_engine().lua());
	}
	return *m_userdata;
}

void script_attachment::SetUserdata(::luabind::object obj)
{
	if (!obj || obj.type() == LUA_TNIL)
	{
		xr_delete(m_userdata);
		m_userdata = nullptr;
		return;
	}

	if (obj.type() != LUA_TTABLE)
	{
		Msg("![Script Attachment]: Trying to set userdata to wrong type! (Allowed types: table, nil)");
		return;
	}

	if (!m_userdata) m_userdata = xr_new<::luabind::object>();

	*m_userdata = obj;
}

void script_attachment::SetScriptUI(LPCSTR ui_func)
{
	if (!ui_func || !xr_strlen(ui_func))
	{
		m_script_ui_func = nullptr;
		m_script_ui = nullptr;
		return;
	}

	if (m_script_ui_func != nullptr && 0 == xr_strcmp(m_script_ui_func, ui_func)) return;

	::luabind::functor<CUIDialogWndEx*> funct;

	if (ai().script_engine().functor(ui_func, funct))
	{
		CUIDialogWndEx* ret = funct();
		CUIWindow* pScriptWnd = ret ? smart_cast<CUIWindow*>(ret) : (0);
		if (pScriptWnd)
		{
			m_script_ui_func = ui_func;
			m_script_ui = pScriptWnd;
		}
		else
			Msg("![Script Attachment]: Failed to load script UI [%s]!", ui_func);
	}
	else
		Msg("![Script Attachment]: Script UI functor [%s] does not exist!", ui_func);
}

void script_attachment::RecalcScriptUIOffset()
{
	Fvector& position = m_script_ui_offset[0];
	Fvector rotation = m_script_ui_offset[1];
	Fvector& scale = m_script_ui_offset[2];
	Fvector& origin = m_script_ui_offset[3];

	rotation.mul(PI / 180.f);

	if (!!origin.x || !!origin.y || !!origin.z)
	{
		m_script_ui_mat.translate(-origin.x, -origin.y, -origin.z);
		m_script_ui_mat.mulA_43(Fmatrix().setHPB(rotation));
		m_script_ui_mat.mulA_43(Fmatrix().scale(scale));
		m_script_ui_mat.mulA_43(Fmatrix().translate(position));
	}
	else
	{
		m_script_ui_mat.setHPB(rotation);
		m_script_ui_mat.translate_over(position);
		m_script_ui_mat.mulB_43(Fmatrix().scale(scale));
	}
}

void script_attachment::SetScriptUIPosition(float x, float y, float z)
{
	m_script_ui_offset[0].set(x, y, z);
	RecalcScriptUIOffset();
}

void script_attachment::SetScriptUIRotation(float x, float y, float z)
{
	m_script_ui_offset[1].set(x, y, z);
	RecalcScriptUIOffset();
}

void script_attachment::SetScriptUIScale(float x, float y, float z)
{
	m_script_ui_offset[2].set(x, y, z);
	RecalcScriptUIOffset();
}

void script_attachment::SetScriptUIOrigin(float x, float y, float z)
{
	m_script_ui_offset[3].set(x, y, z);
	RecalcScriptUIOffset();
}

void script_attachment::ScriptAttachmentBoneCallback(CBoneInstance* B)
{
	script_attachment_bone_cb* params = static_cast<script_attachment_bone_cb*>(B->callback_param());
	bool bVisible = params->m_attachment->GetBoneVisible(params->m_attachment_bone_id);
	Fmatrix& target = bVisible ? B->mTransform : B->mTransformHidden;
	target = params->m_mat;
}

void script_attachment::SetBoneCallback(u16 bone_id, LPCSTR parent_bone, bool overwrite)
{
	if (m_parent_object)
	{
		SetBoneCallback(bone_id, m_parent_object->lua_game_object()->bone_id(parent_bone, m_type == eSA_HUD), overwrite);
		return;
	}

	if (m_parent_attachment)
	{
		SetBoneCallback(bone_id, m_parent_attachment->bone_id(parent_bone), overwrite);
		return;
	}

	Msg("![SetBoneCallback]: Parent has no bone with name %i", parent_bone);
}

void script_attachment::SetBoneCallback(LPCSTR bone, LPCSTR parent_bone, bool overwrite)
{
	if (m_parent_object)
	{
		SetBoneCallback(bone_id(bone), m_parent_object->lua_game_object()->bone_id(parent_bone, m_type == eSA_HUD), overwrite);
		return;
	}

	if (m_parent_attachment)
	{
		SetBoneCallback(bone_id(bone), m_parent_attachment->bone_id(parent_bone), overwrite);
		return;
	}

	Msg("![SetBoneCallback]: Parent has no bone with name %s", parent_bone);
}

void script_attachment::SetBoneCallback(u16 bone_id, u16 parent_bone, bool overwrite)
{
	if (bone_id >= Kinematics()->LL_BoneCount())
	{
		Msg("![SetBoneCallback]: Attachment has no bone with id %i", bone_id);
		return;
	}

	if (m_bone_callbacks[bone_id])
	{
		Kinematics()->LL_GetBoneInstance(bone_id).reset_callback();
		m_bone_callbacks.erase(bone_id);
	}

	m_bone_callbacks[bone_id] = xr_new<script_attachment_bone_cb>(parent_bone, this, bone_id, overwrite);
	Kinematics()->LL_GetBoneInstance(bone_id).set_callback(bctCustom, ScriptAttachmentBoneCallback, m_bone_callbacks[bone_id], overwrite);
}

void script_attachment::SetBoneCallback(u16 bone_id, const ::luabind::functor<Fmatrix>& func, bool overwrite)
{
	if (bone_id >= Kinematics()->LL_BoneCount())
	{
		Msg("![SetBoneCallback]: Attachment has no bone with id %i", bone_id);
		return;
	}

	if (m_bone_callbacks[bone_id])
	{
		Kinematics()->LL_GetBoneInstance(bone_id).reset_callback();
		m_bone_callbacks.erase(bone_id);
	}

	m_bone_callbacks[bone_id] = xr_new<script_attachment_bone_cb>(func, this, bone_id, overwrite);
	CBoneInstance& bInst = Kinematics()->LL_GetBoneInstance(bone_id);
	bInst.set_callback(bctCustom, ScriptAttachmentBoneCallback, m_bone_callbacks[bone_id], overwrite);
	(m_bone_callbacks[bone_id]->m_mat).set(GetBoneVisible(bone_id) ? bInst.mTransformHidden : bInst.mTransform);
}

void script_attachment::RemoveBoneCallback(u16 bone_id)
{
	if (m_bone_callbacks[bone_id])
	{
		Kinematics()->LL_GetBoneInstance(bone_id).reset_callback();
		m_bone_callbacks.erase(bone_id);
	}
}

IRenderVisual* script_attachment::Visual() const
{
	if (!m_model_instance_owner && !m_model_instance_owner_hud)
		return renderable.visual;

	if (m_model_instance_owner_hud)
		return m_model_instance_owner_hud->HudItemData()->m_model->dcast_RenderVisual();

	return m_model_instance_owner->Visual();
}

IKinematics* script_attachment::Kinematics() const
{ 
	return Visual()->dcast_PKinematics();
}

const Fbox& script_attachment::Box()
{
	return Visual()->dcast_PKinematics()->GetBox();
}

bool script_attachment::has_shapes()
{
    for (const auto& bone : *Kinematics()->LL_Bones())
    {
        const IBoneData& bone_data = Kinematics()->GetBoneData(bone.second);
        const SBoneShape& shape = bone_data.get_shape();
        if (shape.type != SBoneShape::stNone) return true;
    }

    return false;
}

::luabind::object script_attachment::BoneShape(u16 id, bool local)
{
    ::luabind::object table = ::luabind::newtable(ai().script_engine().lua());

    if (id >= Kinematics()->LL_BoneCount())
    {
        table["error"] = "no bone";
        return table;
    }

    const IBoneData& bone_data = Kinematics()->GetBoneData(id);
    const SBoneShape& shape = bone_data.get_shape();

    if (shape.type == SBoneShape::stNone)
    {
        table["error"] = "no shape";
        return table;
    }

    Fmatrix bone_xform = Kinematics()->LL_GetTransform(id);
    bone_xform.mulA_43(XFORM());

    Fmatrix bone_xform_global = bone_xform;

    Fvector real_scale;
    real_scale.set(1, 1, 1);
    script_attachment* parent = m_parent_attachment;
    script_attachment* toplevel_parent_attachment = nullptr;

    while (parent)
    {
        toplevel_parent_attachment = parent;
        real_scale.mul(parent->GetScale());
        parent = parent->m_parent_attachment;
    }
    
    real_scale.mul(GetScale());

    if (local)
    {
        Fmatrix* obj_xform = nullptr;

        if (m_toplevel_parent)
        {
            if (GetType() == eSA_HUD)
            {
                CActor* act = smart_cast<CActor*>(m_toplevel_parent);
                CHudItem* itm = smart_cast<CHudItem*>(m_toplevel_parent);

                if (act)
                    obj_xform = (toplevel_parent_attachment ? toplevel_parent_attachment->GetParentBone() < 21 : GetParentBone() < 21) ? &g_player_hud->m_transform_2 : &g_player_hud->m_transform;
                if (itm)
                {
                    script_attachment* instance_holder = itm->m_instance_holder_hud;
                    if (instance_holder)
                        obj_xform = &instance_holder->GetTransform();
                    else
                        obj_xform = &itm->HudItemData()->m_item_transform;
                }
            }

            if (!obj_xform) obj_xform = &m_toplevel_parent->XFORM();
        }
        else // Level Attachment?
            obj_xform = toplevel_parent_attachment ? &toplevel_parent_attachment->XFORM() : &XFORM();

        Fmatrix inv_g = Fidentity;
        inv_g.invert(*obj_xform);
        bone_xform.mulA_43(inv_g);
    }

    switch (shape.type)
    {
    case SBoneShape::stBox:
    {
        Fobb box = shape.box;
        box.transform(box, bone_xform);

        // Scale and rotation correction if attachment scale is not 1
        if (real_scale.x != 1.f || real_scale.y != 1.f || real_scale.z != 1.f)
        {
            Fvector inv_scale;
            inv_scale.set(1, 1, 1);
            inv_scale.div(real_scale);

            box.m_halfsize.mul(real_scale.x, real_scale.z, real_scale.y);
            box.m_rotate.i.mul(inv_scale);
            box.m_rotate.j.mul(inv_scale);
            box.m_rotate.k.mul(inv_scale);
        }

        Fmatrix m_box;
        box.xform_get(m_box);

        table["type"] = "box";
        table["xform"] = m_box;
        table["obj"] = box;
        break;
    }
    case SBoneShape::stSphere:
    {
        Fsphere sphere = shape.sphere;
        bone_xform.transform_tiny(sphere.P);
        sphere.R *= fmaxf(real_scale.x, fmaxf(real_scale.y, real_scale.z));

        table["type"] = "sphere";
        table["xform"] = bone_xform;
        table["obj"] = sphere;
        break;
    }
    case SBoneShape::stCylinder:
    {
        Fcylinder cylinder = shape.cylinder;

        Fmatrix form;
        form._14 = 0;
        form._24 = 0;
        form._34 = 0;
        form._44 = 1;
        form.j.set(cylinder.m_direction);
        Fvector::generate_orthonormal_basis(form.j, form.k, form.i);
        form.c.set(cylinder.m_center);

        form.mulA_43(bone_xform);

        if (real_scale.x != 1.f || real_scale.y != 1.f || real_scale.z != 1.f)
        {
            Fvector inv_scale;
            inv_scale.set(1, 1, 1);
            inv_scale.div(real_scale);

            cylinder.m_height *= real_scale.z;
            cylinder.m_radius *= fmaxf(real_scale.x, real_scale.y);
            form.i.mul(inv_scale);
            form.j.mul(inv_scale);
            form.k.mul(inv_scale);
        }

        bone_xform.transform_tiny(cylinder.m_center);
        bone_xform.transform_dir(cylinder.m_direction);

        table["type"] = "cylinder";
        table["xform"] = form;
        table["obj"] = cylinder;
        
        break;
    }
    default: NODEFAULT;
    }

    return table;
}

Fvector script_attachment::Position() const
{
	return renderable.xform.c;
}

Fvector script_attachment::Center() const
{
	Fvector pos = Visual()->getVisData().sphere.P;

    // bandaid fix: pos can be very large (~1e38) so this check is necessary
    if (!_valid(pos) || fabsf(pos.x) > 1e4 || fabsf(pos.y) > 1e4 || fabsf(pos.z) > 1e4)
        pos.set(0.f, 0.f, 0.f);

    return pos;
}

float script_attachment::Radius() const
{
	return Visual()->getVisData().sphere.R;
}

::luabind::object script_attachment::GetShaders()
{
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());

	if (!Visual())
	{
		table["error"] = true;
		return table;
	}

	xr_vector<IRenderVisual*>* children = Visual()->get_children();
	xr_vector<IRenderVisual*>* children_invisible = Visual()->get_children_invisible();

	if (!children && !children_invisible)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = Visual()->getDebugShader();
		subtable["texture"] = Visual()->getDebugTexture();
		table[1] = subtable;
		return table;
	}

	if (children)
	{
	for (auto* child : *children)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShader();
		subtable["texture"] = child->getDebugTexture();
		table[child->getID()] = subtable;
	}
	}
	
	if (children_invisible)
	{
	for (auto* child : *children_invisible)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShader();
		subtable["texture"] = child->getDebugTexture();
		table[child->getID()] = subtable;
	}
	}

	return table;
}

::luabind::object script_attachment::GetDefaultShaders()
{
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());

	if (!Visual())
	{
		table["error"] = true;
		return table;
	}

	xr_vector<IRenderVisual*>* children = Visual()->get_children();
	xr_vector<IRenderVisual*>* children_invisible = Visual()->get_children_invisible();

	if (!children && !children_invisible)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = Visual()->getDebugShaderDef();
		subtable["texture"] = Visual()->getDebugTextureDef();
		table[1] = subtable;
		return table;
	}

	if (children)
	{
	for (auto* child : *children)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShaderDef();
		subtable["texture"] = child->getDebugTextureDef();
		table[child->getID()] = subtable;
	}
	}
	
	if (children_invisible)
	{
	for (auto* child : *children_invisible)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShaderDef();
		subtable["texture"] = child->getDebugTextureDef();
		table[child->getID()] = subtable;
	}
	}

	return table;
}

void script_attachment::SetShaderTexture(int id, LPCSTR shader, LPCSTR texture)
{
    if (!Visual()) return;
    xr_vector<IRenderVisual*>* children = Visual()->get_children();
    xr_vector<IRenderVisual*>* children_invisible = Visual()->get_children_invisible();

	if (!children && !children_invisible)
	{
        Visual()->SetShaderTexture(shader, texture);
		return;
	}

	if (id < 1)
    {
        if (children)
	{
		for (auto* child : *children)
		{
			child->SetShaderTexture(shader, texture);
		}
        }

        if (children_invisible)
        {
		for (auto* child : *children_invisible)
		{
			child->SetShaderTexture(shader, texture);
		}
        }
		return;
	}

    if (children)
    {
	for (auto* child : *children)
	{
		if (child->getID() != id) continue;
		child->SetShaderTexture(shader, texture);
		return;
	}
    }

    if (children_invisible)
    {
	for (auto* child : *children_invisible)
	{
		if (child->getID() != id) continue;
		child->SetShaderTexture(shader, texture);
		return;
	}
}
}

void script_attachment::ResetShaderTexture(int id)
{
    if (!Visual()) return;
    xr_vector<IRenderVisual*>* children = Visual()->get_children();
    xr_vector<IRenderVisual*>* children_invisible = Visual()->get_children_invisible();

	if (!children && !children_invisible)
	{
        Visual()->ResetShaderTexture();
		return;
	}

	if (id < 1)
    {
        if (children)
	{
		for (auto* child : *children)
		{
			child->ResetShaderTexture();
		}
        }

        if (children_invisible)
        {
		for (auto* child : *children_invisible)
		{
			child->ResetShaderTexture();
		}
        }
		return;
	}

    if (children)
    {
	for (auto* child : *children)
	{
		if (child->getID() != id) continue;
		child->ResetShaderTexture();
		return;
	}
    }

    if (children_invisible)
    {
	for (auto* child : *children_invisible)
	{
		if (child->getID() != id) continue;
		child->ResetShaderTexture();
		return;
	}
}
}
