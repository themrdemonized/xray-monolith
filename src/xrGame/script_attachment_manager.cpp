#pragma once
#include "stdafx.h"
#include "script_attachment_manager.h"
#include "player_hud.h"
#include "actor.h"
#include "ui\UIScriptWnd.h"

//#define DEBUG_VISBOX

#ifdef DEBUG_VISBOX
#include "debug_renderer.h"
#endif

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

#ifdef DEBUG_VISBOX
	Fmatrix box, cent;
	cent.translate(k->dcast_RenderVisual()->getVisData().sphere.P);
	box.mul(att->GetTransform(), cent);

	const Fbox& kbox = k->GetBox();
	Fvector radius;
	kbox.getradius(radius);
	CDebugRenderer& render = Level().debug_renderer();
	render.draw_obb(box, radius, D3DCOLOR_XRGB(150, 0, 150), att->GetType() == eSA_HUD);
#endif
}

script_attachment::script_attachment(LPCSTR name, LPCSTR model_name): ISpatial(g_SpatialSpace)
{
	m_name = name;
	m_kinematics = nullptr;
	m_parent_attachment = nullptr;
	m_parent_object = nullptr;
	m_parent_level = false;
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
	m_attachment_offset[0].set(0, 0, 0);
	m_attachment_offset[1].set(0, 0, 0);
	m_attachment_offset[2].set(1, 1, 1);
	m_attachment_offset[3].set(0, 0, 0);
	m_bStopAtEndAnimIsRunning = false;
	m_anim_end = 0;
	m_type = eSA_World;
	m_last_upd_frame = 0;
	m_current_motion = "idle";
	m_model_name = "";
	m_userdata = nullptr;
	spatial.type |= STYPE_RENDERABLE;
	LoadModel(model_name);
	PlayMotion("idle", false);
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
	renderable.xform.transform_tiny(spatial.sphere.P, renderable.visual ? renderable.visual->getVisData().sphere.P : Fvector{ 0,0,0 });
	Fvector& scale = m_attachment_offset[2];
	spatial.sphere.R = renderable.visual ? renderable.visual->getVisData().sphere.R * std::max(scale.x, std::max(scale.y, scale.z)) : 0.f;
	ISpatial::spatial_register();
}

void script_attachment::spatial_unregister()
{
	ISpatial::spatial_unregister();
}

void script_attachment::spatial_move()
{
	if (!spatial.node_ptr) return;
	renderable.xform.transform_tiny(spatial.sphere.P, renderable.visual->getVisData().sphere.P);
	Fvector& scale = m_attachment_offset[2];
	spatial.sphere.R = renderable.visual->getVisData().sphere.R * std::max(scale.x, std::max(scale.y, scale.z));
	ISpatial::spatial_move();
}

void script_attachment::renderable_Render()
{
	if (GetType() != eSA_World) return;

	if (m_script_light)
	{
		Fmatrix LM;
		Fmatrix light_bone;
		if (m_kinematics->LL_BoneCount() > m_script_light_bone)
			light_bone = m_kinematics->LL_GetTransform(m_script_light_bone);
		else
			light_bone = m_kinematics->LL_GetTransform(m_kinematics->LL_GetBoneRoot());

		LM.mul(renderable.xform, light_bone);
		m_script_light->SetXFORM(LM);
	}

	if (::Render->get_generation() == ::Render->GENERATION_R1)
		g_pGamePersistent->AttachmentUIsToRender.push_back(this);
	else
		RenderUI();

	IKinematicsAnimated* ka = renderable.visual->dcast_PKinematicsAnimated();
	if (ka) ka->UpdateTracks();
	m_kinematics->CalculateBones_Invalidate();
	m_kinematics->CalculateBones(TRUE);

	::Render->set_Transform(&renderable.xform);
	::Render->add_Visual(renderable.visual);

	if (m_children.size())
	{
		for (auto& pair : m_children)
		{
			pair.second->Render(m_kinematics, &renderable.xform);
		}
	}
}

void script_attachment::Render(IKinematics* model, Fmatrix* mat)
{
	if (!model || (m_bone_callbacks[0] && m_bone_callbacks[0]->m_bone_id != BI_NONE))
		renderable.xform = *mat;
	else
		renderable.xform.mul_43(*mat, BoneTransform(model));

	renderable.xform.mulB_43(m_offset);

	if (m_script_light)
	{
		Fmatrix LM;
		Fmatrix light_bone;
		if (m_kinematics->LL_BoneCount() > m_script_light_bone)
			light_bone = m_kinematics->LL_GetTransform(m_script_light_bone);
		else
			light_bone = m_kinematics->LL_GetTransform(m_kinematics->LL_GetBoneRoot());

		LM.mul(renderable.xform, light_bone);
		m_script_light->SetXFORM(LM);
	}

	IKinematicsAnimated* ka = renderable.visual->dcast_PKinematicsAnimated();
	if (ka || GetType() == eSA_CamAttached)
	{
		if (ka) ka->UpdateTracks();
		m_kinematics->CalculateBones_Invalidate();
		m_kinematics->CalculateBones(TRUE);
	}

	::Render->set_Transform(&renderable.xform);
	::Render->add_Visual(renderable.visual);

	if (m_children.size())
	{
		for (auto& pair : m_children)
		{
			pair.second->Render(m_kinematics, &renderable.xform);
		}
	}
}

void script_attachment::Update()
{
	if (m_last_upd_frame == Device.dwFrame) return;
	m_last_upd_frame = Device.dwFrame;

	if (m_script_ui)
		m_script_ui->Update();

	if (m_bStopAtEndAnimIsRunning && Device.dwTimeGlobal >= m_anim_end)
	{
		PlayMotion("idle");
		m_bStopAtEndAnimIsRunning = false;
	}

	if (m_bone_callbacks.size())
	{
		for (auto& pair : m_bone_callbacks)
		{
			script_attachment_bone_cb* cb = pair.second;
			if (!cb) continue;
			
			if (cb->m_func)
			{
				cb->m_mat.set((*(cb->m_func))(
					m_kinematics->LL_GetBoneInstance(cb->m_attachment_bone_id).mTransformHidden, //Transform without bone callback modifier
					m_kinematics->LL_BoneName_dbg(pair.first)));

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
				if (bone >= m_parent_attachment->m_kinematics->LL_BoneCount())
					target = Fidentity;
				else
					target = m_parent_attachment->m_kinematics->LL_GetTransform(bone);
			}
		}
	}

	if (m_children.size())
	{
		for (auto& pair : m_children)
		{
			pair.second->Update();
		}
	}
}

void script_attachment::RenderUI()
{
	if (m_script_ui)
	{
		IUIRender::ePointType bk;

		bk = UI().m_currentPointType;
		UI().m_currentPointType = IUIRender::pttLIT;
		UIRender->CacheSetCullMode(IUIRender::cmNONE);

		Fmatrix LM;
		Fmatrix ui_bone;
		if (m_kinematics->LL_BoneCount() > m_script_ui_bone)
			ui_bone = m_kinematics->LL_GetTransform(m_script_ui_bone);
		else
			ui_bone = m_kinematics->LL_GetTransform(m_kinematics->LL_GetBoneRoot());

		LM.mul(renderable.xform, ui_bone);
		LM.mulB_43(m_script_ui_mat);
		UIRender->CacheSetXformWorld(LM);
		m_script_ui->Draw();
		
		UIRender->CacheSetCullMode(IUIRender::cmCCW);
		UI().m_currentPointType = bk;
	}

	if (m_children.size())
	{
		for (auto& pair : m_children)
		{
			pair.second->RenderUI();
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
	Fvector& position = m_attachment_offset[0];
	Fvector rotation = m_attachment_offset[1];
	Fvector& scale = m_attachment_offset[2];
	Fvector& origin = m_attachment_offset[3];

	rotation.mul(PI / 180.f);

	if (!!origin.x || !!origin.y || !!origin.z)
	{
		m_offset.translate(-origin.x, -origin.y, -origin.z);
		m_offset.mulA_43(Fmatrix().setHPB(rotation));
		m_offset.mulA_43(Fmatrix().scale(scale));
		m_offset.mulA_43(Fmatrix().translate(position));
	}
	else
	{
		m_offset.setHPB(rotation);
		m_offset.translate_over(position);
		m_offset.mulB_43(Fmatrix().scale(scale));
	}

	if (m_parent_level)
	{
		renderable.xform = m_offset;
		spatial_move();
	}
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

void script_attachment::SetScale(float x, float y, float z)
{
	m_attachment_offset[2].set(x, y, z);
	RecalcOffset();
}

void script_attachment::SetOrigin(float x, float y, float z)
{
	m_attachment_offset[3].set(x, y, z);
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

		m_parent_attachment->RemoveChild(GetName());
	}

	if (m_parent_object)
		m_parent_object->remove_child(GetName());

	if (m_parent_level)
		Level().remove_child(GetName());

	spatial_unregister();
	m_parent_level = false;
	m_parent_object = nullptr;
	m_parent_attachment = att;
	m_parent_attachment->AddChild(GetName(), this);
}

void script_attachment::SetParent(CGameObject* obj)
{
	if (!obj)
		return;

	if (m_parent_attachment)
		m_parent_attachment->RemoveChild(GetName());

	if (m_parent_object)
	{
		if (m_parent_object == obj)
			return;

		m_parent_object->remove_child(GetName());
	}
	
	if (m_parent_level)
		Level().remove_child(GetName());

	spatial_unregister();
	m_parent_level = false;
	m_parent_object = obj;
	m_parent_attachment = nullptr;
	m_parent_object->add_attachment(GetName(), this);
}

void script_attachment::SetParent(CScriptGameObject* obj)
{
	if (!obj || !&obj->object())
		return;

	if (m_parent_attachment)
		m_parent_attachment->RemoveChild(GetName());

	if (m_parent_object)
	{
		if (m_parent_object == &obj->object())
			return;

		m_parent_object->remove_child(GetName());
	}

	if (m_parent_level)
		Level().remove_child(GetName());

	spatial_unregister();
	m_parent_level = false;
	m_parent_object = &obj->object();
	m_parent_attachment = nullptr;
	m_parent_object->add_attachment(GetName(), this);
}

void script_attachment::SetParentLevel()
{
	if (m_parent_level)
		return;

	if (m_parent_attachment)
		m_parent_attachment->RemoveChild(GetName());

	if (m_parent_object)
		m_parent_object->remove_child(GetName());

	m_parent_level = true;
	m_parent_object = nullptr;
	m_parent_attachment = nullptr;
	Level().add_attachment(GetName(), this);
	spatial_register();
	spatial_move();
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
	return att;
}

script_attachment* script_attachment::GetChild(LPCSTR name)
{
	if (m_children.size())
	{
		auto& pair = m_children.find(name);
		if (pair != m_children.end())
			return pair->second;
	}

	return nullptr;
}

void script_attachment::RemoveChild(LPCSTR name, bool destroy)
{
	auto& pair = m_children.find(name);
	if (pair == m_children.end())
		return;

	if (destroy)
		xr_delete(pair->second);

	m_children.erase(pair);
}

void script_attachment::IterateAttachments(::luabind::functor<bool> functor)
{
	if (!m_children.size())
		return;

	for (auto& pair : m_children)
		if (functor(pair.first.c_str(), pair.second) == true)
			return;
}

Fmatrix& script_attachment::BoneTransform(IKinematics* model)
{
	IKinematics* kin = model;

	if (kin->LL_BoneCount() > m_parent_bone)
		return kin->LL_GetTransform(m_parent_bone);

	return kin->LL_GetTransform(kin->LL_GetBoneRoot());
}

u32 script_attachment::PlayMotion(LPCSTR name, bool mixin, float speed)
{
	IKinematicsAnimated* k = renderable.visual->dcast_PKinematicsAnimated();

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

	k->UpdateTracks();
	m_kinematics->CalculateBones_Invalidate();
	m_kinematics->CalculateBones(true);

	return length;
}

u32 script_attachment::motion_length(const MotionID& M, const CMotionDef*& md, float speed)
{
	IKinematicsAnimated* k = renderable.visual->dcast_PKinematicsAnimated();

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

void script_attachment::SetBoneVisible(u16 bone_id, bool bVisibility, bool bRecursive)
{
	if (m_kinematics->LL_BoneCount() > bone_id)
	{
		bool bVisibleNow = m_kinematics->LL_GetBoneVisible(bone_id);
		if (bVisibleNow != bVisibility)
			m_kinematics->LL_SetBoneVisible(bone_id, bVisibility, TRUE);

		m_kinematics->CalculateBones_Invalidate();
		m_kinematics->CalculateBones(TRUE);
	}
}

bool script_attachment::GetBoneVisible(u16 bone_id)
{
	if (m_kinematics->LL_BoneCount() > bone_id)
		return m_kinematics->LL_GetBoneVisible(bone_id);

	return false;
}

void script_attachment::SetParentBone(LPCSTR bone)
{
	if (m_parent_object)
	{
		m_parent_bone = m_parent_object->lua_game_object()->bone_id(bone, m_type == eSA_HUD);
		return;
	}

	if (m_parent_attachment)
	{
		m_parent_bone = m_parent_attachment->bone_id(bone);
		return;
	}

	m_parent_bone = 0;
}

u16 script_attachment::bone_id(LPCSTR bone_name)
{
	u16 bone_id = BI_NONE;
	if (xr_strlen(bone_name))
		bone_id = m_kinematics->LL_BoneID(bone_name);

	return bone_id;
}

extern BOOL print_bone_warnings;

Fmatrix script_attachment::bone_transform(u16 bone_id)
{
	if (bone_id == BI_NONE || bone_id >= m_kinematics->LL_BoneCount()) {
		if (strstr(Core.Params, "-dbg") && print_bone_warnings) {
			Msg("![bone_position] Incorrect bone_id provided for %s (%s), fallback to root bone", GetName(), GetModelScript());
			ai().script_engine().print_stack();
		}
		bone_id = m_kinematics->LL_GetBoneRoot();
	}

	Fmatrix matrix;
	matrix.mul_43(renderable.xform, m_kinematics->LL_GetTransform(bone_id));
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
	if (bone_id == m_kinematics->LL_GetBoneRoot() || bone_id >= m_kinematics->LL_BoneCount()) return BI_NONE;

	CBoneData* data = &m_kinematics->LL_GetData(bone_id);
	u16 ParentID = data->GetParentID();
	return ParentID;
}

LPCSTR script_attachment::bone_name(u16 bone_id)
{
	if (bone_id == BI_NONE) return "";
	return (m_kinematics->LL_BoneName_dbg(bone_id));
}

// demonized: list all bones
::luabind::object script_attachment::list_bones()
{
	::luabind::object result = ::luabind::newtable(ai().script_engine().lua());

	auto bones = m_kinematics->LL_Bones();
	for (const auto& bone : *bones)
		result[bone.second] = bone.first.c_str();

	return result;
}

void script_attachment::LoadModel(LPCSTR model_name, bool keep_bc)
{
	if (m_model_name == model_name)
		return;

	m_model_name = model_name;

	u16 count_prev = 0;

	if (renderable.visual)
	{
		count_prev = m_kinematics->LL_BoneCount();
		::Render->model_Delete(renderable.visual);
	}
	
	renderable.visual = ::Render->model_Create(*m_model_name);
	R_ASSERT(renderable.visual);
	m_kinematics = renderable.visual->dcast_PKinematics();
	R_ASSERT(m_kinematics);

	spatial_move();

	// Bone Callbacks
	if (keep_bc && m_bone_callbacks.size() && count_prev <= m_kinematics->LL_BoneCount())
	{
		for (auto& pair : m_bone_callbacks)
		{
			m_kinematics->LL_GetBoneInstance(pair.first).set_callback(bctCustom, ScriptAttachmentBoneCallback, pair.second, pair.second->m_overwrite);
		}

		PlayMotion(*m_current_motion, false);
	}

	// Visibility Box Update
	m_kinematics->SetUpdateCallback(update_visbox_attachment);
	m_kinematics->SetUpdateCallbackParam(this);
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

	Msg("![SetBoneCallback]: Parent has no bone with name %i", parent_bone);
}

void script_attachment::SetBoneCallback(u16 bone_id, u16 parent_bone, bool overwrite)
{
	if (bone_id >= m_kinematics->LL_BoneCount())
	{
		Msg("![SetBoneCallback]: Attachment has no bone with id %i", bone_id);
		return;
	}

	if (m_bone_callbacks[bone_id])
	{
		m_kinematics->LL_GetBoneInstance(bone_id).reset_callback();
		m_bone_callbacks.erase(bone_id);
	}

	m_bone_callbacks[bone_id] = xr_new<script_attachment_bone_cb>(parent_bone, this, bone_id, overwrite);
	m_kinematics->LL_GetBoneInstance(bone_id).set_callback(bctCustom, ScriptAttachmentBoneCallback, m_bone_callbacks[bone_id], overwrite);
}

void script_attachment::SetBoneCallback(u16 bone_id, const ::luabind::functor<Fmatrix>& func, bool overwrite)
{
	if (bone_id >= m_kinematics->LL_BoneCount())
	{
		Msg("![SetBoneCallback]: Attachment has no bone with id %i", bone_id);
		return;
	}

	if (m_bone_callbacks[bone_id])
	{
		m_kinematics->LL_GetBoneInstance(bone_id).reset_callback();
		m_bone_callbacks.erase(bone_id);
	}

	m_bone_callbacks[bone_id] = xr_new<script_attachment_bone_cb>(func, this, bone_id, overwrite);
	CBoneInstance& bInst = m_kinematics->LL_GetBoneInstance(bone_id);
	bInst.set_callback(bctCustom, ScriptAttachmentBoneCallback, m_bone_callbacks[bone_id], overwrite);
	(m_bone_callbacks[bone_id]->m_mat).set(GetBoneVisible(bone_id) ? bInst.mTransformHidden : bInst.mTransform);
}

void script_attachment::RemoveBoneCallback(u16 bone_id)
{
	if (m_bone_callbacks[bone_id])
	{
		m_kinematics->LL_GetBoneInstance(bone_id).reset_callback();
		m_bone_callbacks.erase(bone_id);
	}
}

const Fbox& script_attachment::Box()
{
	return renderable.visual->dcast_PKinematics()->GetBox();
}

Fvector script_attachment::GetCenter()
{
	if (renderable.visual)
		return renderable.visual->getVisData().sphere.P;

	return { 0,0,0 };
}

::luabind::object script_attachment::GetShaders()
{
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());

	if (!renderable.visual)
	{
		table["error"] = true;
		return table;
	}

	xr_vector<IRenderVisual*>* children = renderable.visual->get_children();
	xr_vector<IRenderVisual*>* children_invisible = renderable.visual->get_children_invisible();

	if (!children && !children_invisible)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = renderable.visual->getDebugShader();
		subtable["texture"] = renderable.visual->getDebugTexture();
		table[1] = subtable;
		return table;
	}

	for (auto* child : *children)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShader();
		subtable["texture"] = child->getDebugTexture();
		table[child->getID()] = subtable;
	}

	for (auto* child : *children_invisible)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShader();
		subtable["texture"] = child->getDebugTexture();
		table[child->getID()] = subtable;
	}

	return table;
}

::luabind::object script_attachment::GetDefaultShaders()
{
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());

	if (!renderable.visual)
	{
		table["error"] = true;
		return table;
	}

	xr_vector<IRenderVisual*>* children = renderable.visual->get_children();
	xr_vector<IRenderVisual*>* children_invisible = renderable.visual->get_children_invisible();

	if (!children && !children_invisible)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = renderable.visual->getDebugShaderDef();
		subtable["texture"] = renderable.visual->getDebugTextureDef();
		table[1] = subtable;
		return table;
	}

	for (auto* child : *children)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShaderDef();
		subtable["texture"] = child->getDebugTextureDef();
		table[child->getID()] = subtable;
	}

	for (auto* child : *children_invisible)
	{
		::luabind::object subtable = ::luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShaderDef();
		subtable["texture"] = child->getDebugTextureDef();
		table[child->getID()] = subtable;
	}

	return table;
}

void script_attachment::SetShaderTexture(int id, LPCSTR shader, LPCSTR texture)
{
	if (!renderable.visual) return;
	xr_vector<IRenderVisual*>* children = renderable.visual->get_children();
	xr_vector<IRenderVisual*>* children_invisible = renderable.visual->get_children_invisible();

	if (!children && !children_invisible)
	{
		renderable.visual->SetShaderTexture(shader, texture);
		return;
	}

	if (id < 1)
	{
		for (auto* child : *children)
		{
			child->SetShaderTexture(shader, texture);
		}
		for (auto* child : *children_invisible)
		{
			child->SetShaderTexture(shader, texture);
		}
		return;
	}

	for (auto* child : *children)
	{
		if (child->getID() != id) continue;
		child->SetShaderTexture(shader, texture);
		return;
	}
	for (auto* child : *children_invisible)
	{
		if (child->getID() != id) continue;
		child->SetShaderTexture(shader, texture);
		return;
	}
}

void script_attachment::ResetShaderTexture(int id)
{
	if (!renderable.visual) return;
	xr_vector<IRenderVisual*>* children = renderable.visual->get_children();
	xr_vector<IRenderVisual*>* children_invisible = renderable.visual->get_children_invisible();

	if (!children && !children_invisible)
	{
		renderable.visual->ResetShaderTexture();
		return;
	}

	if (id < 1)
	{
		for (auto* child : *children)
		{
			child->ResetShaderTexture();
		}
		for (auto* child : *children_invisible)
		{
			child->ResetShaderTexture();
		}
		return;
	}

	for (auto* child : *children)
	{
		if (child->getID() != id) continue;
		child->ResetShaderTexture();
		return;
	}
	for (auto* child : *children_invisible)
	{
		if (child->getID() != id) continue;
		child->ResetShaderTexture();
		return;
	}
}
