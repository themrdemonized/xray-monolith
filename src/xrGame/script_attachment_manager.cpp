#pragma once
#include "stdafx.h"
#include "script_attachment_manager.h"
#include "player_hud.h"
#include "actor.h"
#include "ui\UIScriptWnd.h"

script_attachment::script_attachment(u16 id, LPCSTR model_name)
{
	m_script_ui = nullptr;
	m_script_ui_func = 0;
	m_script_ui_mat = Fidentity;
	m_script_ui_offset[0].set(0, 0, 0);
	m_script_ui_offset[1].set(0, 0, 0);
	m_script_ui_bone = 0;
	m_script_light = nullptr;
	m_script_light_bone = 0;
	m_slot = id;
	m_parent_bone = 0;
	m_offset = Fidentity;
	m_transform = Fidentity;
	m_position.set(0, 0, 0);
	m_rotation.set(0, 0, 0);
	m_origin.set(0, 0, 0);
	m_scale.set(1, 1, 1);
	m_bStopAtEndAnimIsRunning = false;
	m_anim_end = 0;
	m_flags.assign(eSA_RenderWorld);
	m_last_upd_frame = 0;
	m_current_motion = "idle";
	m_model_name = "";
	LoadModel(model_name);
	PlayMotion("idle", false);
}

script_attachment* script_attachment::AddAttachment(u16 slot, LPCSTR model_name)
{
	script_attachment* att = xr_new<script_attachment>(slot, model_name);
	R_ASSERT(att);
	att->SetParent(this);
	return att;
}

void script_attachment::Render(IKinematics* model, Fmatrix* mat, bool hud_mode)
{
	if (!model || (m_bone_callbacks[0] && m_bone_callbacks[0]->m_bone_id != BI_NONE))
		m_transform = *mat;
	else
		m_transform.mul(*mat, BoneTransform(model));

	m_transform.mulB_43(m_offset);

	if (m_script_light && (hud_mode != !!m_flags.test(eSA_RenderWorld)))
	{
		Fmatrix LM;
		Fmatrix light_bone = m_model->dcast_PKinematics()->LL_GetTransform(m_script_light_bone);
		LM.mul(m_transform, light_bone);
		m_script_light->SetXFORM(LM);
	}

	if (m_model->dcast_PKinematicsAnimated())
		m_model->dcast_PKinematicsAnimated()->UpdateTracks();
	m_model->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model->dcast_PKinematics()->CalculateBones(TRUE);

	::Render->set_Transform(&m_transform);
	::Render->add_Visual(m_model);

	if (m_children.size())
	{
		xr_map<u16, script_attachment*>::iterator it = m_children.begin();
		xr_map<u16, script_attachment*>::iterator it_e = m_children.end();
		for (; it != it_e; ++it)
		{
			(*it).second->Render(m_model->dcast_PKinematics(), &m_transform);
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
		xr_map<u16, script_attachment_bone_cb*>::iterator it = m_bone_callbacks.begin();
		xr_map<u16, script_attachment_bone_cb*>::iterator it_e = m_bone_callbacks.end();
		for (; it != it_e; ++it)
		{
			script_attachment_bone_cb* cb = (*it).second;
			if (!cb) continue;
			
			if (cb->m_func)
			{
				cb->m_mat.set((*(cb->m_func))(
					m_model->dcast_PKinematics()->LL_GetBoneInstance(cb->m_attachment_bone_id).mTransformHidden, //Transform without bone callback modifier
					m_model->dcast_PKinematics()->LL_BoneName_dbg((*it).first)));

				continue;
			}
			
			Fmatrix& target = cb->m_mat;
			u16 bone = cb->m_bone_id;

			if (m_parent_object)
			{
				if (GetFFlags().test(eSA_RenderHUD))
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
				if (bone >= m_parent_attachment->m_model->dcast_PKinematics()->LL_BoneCount())
					target = Fidentity;
				else
					target = m_parent_attachment->m_model->dcast_PKinematics()->LL_GetTransform(bone);
			}
		}
	}

	if (m_children.size())
	{
		xr_map<u16, script_attachment*>::iterator it = m_children.begin();
		xr_map<u16, script_attachment*>::iterator it_e = m_children.end();
		for (; it != it_e; ++it)
		{
			(*it).second->Update();
		}
	}
}

void script_attachment::RenderUI(bool hud_mode)
{
	if (hud_mode || (!hud_mode && (m_flags.test(eSA_RenderWorld) || m_flags.test(eSA_CamAttached))))
	{
		if (m_script_ui)
		{
			if (hud_mode)
			{
				Fmatrix LM;
				Fmatrix ui_bone = m_model->dcast_PKinematics()->LL_GetTransform(m_script_ui_bone);
				LM.mul(m_transform, ui_bone);
				LM.mulB_43(m_script_ui_mat);
				UIRender->CacheSetXformWorld(LM);
				m_script_ui->Draw();
			}
			else
			{
				IUIRender::ePointType bk = UI().m_currentPointType;
				UI().m_currentPointType = IUIRender::pttLIT;
				UIRender->CacheSetCullMode(IUIRender::cmNONE);

				Fmatrix LM;
				Fmatrix ui_bone = m_model->dcast_PKinematics()->LL_GetTransform(m_script_ui_bone);
				LM.mul(m_transform, ui_bone);
				LM.mulB_43(m_script_ui_mat);
				UIRender->CacheSetXformWorld(LM);
				m_script_ui->Draw();
				UIRender->CacheSetCullMode(IUIRender::cmCCW);
				UI().m_currentPointType = bk;
			}
		}
	}

	if (m_children.size())
	{
		xr_map<u16, script_attachment*>::iterator it = m_children.begin();
		xr_map<u16, script_attachment*>::iterator it_e = m_children.end();
		for (; it != it_e; ++it)
		{
			(*it).second->RenderUI(hud_mode);
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
	if (!!m_origin.x || !!m_origin.y || !!m_origin.z)
	{
		Fmatrix rotation_matrix;
		
		m_offset.translate(m_origin);

		Fvector rotation = m_rotation;
		rotation.mul(PI / 180.f);
		rotation_matrix.setHPB(rotation.x, rotation.y, rotation.z);

		m_offset.mulA_43(rotation_matrix);

		m_offset.mulA_43(Fmatrix().scale(m_scale));
		m_offset.mulA_43(Fmatrix().translate(m_position));
	}
	else
	{
		Fvector rotation = m_rotation;
		rotation.mul(PI / 180.f);
		m_offset.setHPB(rotation.x, rotation.y, rotation.z);

		m_offset.translate_over(m_position);
		m_offset.mulB_43(Fmatrix().scale(m_scale));
	}
}

void script_attachment::SetPosition(Fvector pos)
{
	m_position = pos;
	RecalcOffset();
}

void script_attachment::SetRotation(Fvector rot)
{
	m_rotation = rot;
	RecalcOffset();
}

void script_attachment::SetScale(Fvector scale)
{
	m_scale = scale;
	RecalcOffset();
}

void script_attachment::SetOrigin(Fvector org)
{
	m_origin = org.invert();
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

		m_parent_attachment->RemoveChild(m_slot);
	}

	if (m_parent_object)
		m_parent_object->remove_attachment(m_slot);

	m_parent_object = nullptr;
	m_parent_attachment = att;
	m_parent_attachment->AddChild(m_slot, this);
}

void script_attachment::SetParent(CGameObject* obj)
{
	if (!obj)
		return;

	if (m_parent_attachment)
		m_parent_attachment->RemoveChild(m_slot);

	if (m_parent_object)
	{
		if (m_parent_object == obj)
			return;

		m_parent_object->remove_attachment(m_slot);
	}
		

	m_parent_object = obj;
	m_parent_attachment = nullptr;
	m_parent_object->add_attachment(m_slot, this);
}

void script_attachment::SetParent(CScriptGameObject* obj)
{
	if (!obj || !&obj->object())
		return;

	if (m_parent_attachment)
		m_parent_attachment->RemoveChild(m_slot);

	if (m_parent_object)
	{
		if (m_parent_object == &obj->object())
			return;

		m_parent_object->remove_attachment(m_slot);
	}


	m_parent_object = &obj->object();
	m_parent_attachment = nullptr;
	m_parent_object->add_attachment(m_slot, this);
}

luabind::object script_attachment::GetParent()
{
	luabind::object table = luabind::newtable(ai().script_engine().lua());
	table["object"] = m_parent_object ? m_parent_object->lua_game_object() : nullptr;
	table["attachment"] = m_parent_attachment ? m_parent_attachment : nullptr;
	return table;
}

script_attachment* script_attachment::AddChild(u16 slot, script_attachment* att)
{
	R_ASSERT(att);
	RemoveChild(slot, true);
	m_children.emplace(mk_pair(slot, att));
	return att;
}

script_attachment* script_attachment::GetChild(u16 slot)
{
	if (m_children.size())
	{
		xr_map<u16, script_attachment*>::iterator att = m_children.find(slot);
		if (att != m_children.end())
			return att->second;
	}

	return nullptr;
}

void script_attachment::RemoveChild(u16 slot, bool destroy)
{
	xr_map<u16, script_attachment*>::iterator att = m_children.find(slot);
	if (att == m_children.end())
		return;

	if (destroy)
		xr_delete(att->second);

	m_children.erase(att);
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
	if (!m_model->dcast_PKinematicsAnimated())
		return 0;

	MotionID M2 = m_model->dcast_PKinematicsAnimated()->ID_Cycle_Safe(name);
	if (!M2.valid())
		M2 = m_model->dcast_PKinematicsAnimated()->ID_Cycle_Safe("idle");

	if (!M2.valid())
		return 0;

	u16 pc = m_model->dcast_PKinematicsAnimated()->partitions().count();
	for (u16 pid = 0; pid < pc; ++pid)
		CBlend* B = m_model->dcast_PKinematicsAnimated()->PlayCycle(pid, M2, mixin, 0, 0, 0, speed);

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

	m_model->dcast_PKinematicsAnimated()->UpdateTracks();
	m_model->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model->dcast_PKinematics()->CalculateBones(true);

	return length;
}

u32 script_attachment::motion_length(const MotionID& M, const CMotionDef*& md, float speed)
{
	if (!m_model->dcast_PKinematicsAnimated())
		return 0;

	md = m_model->dcast_PKinematicsAnimated()->LL_GetMotionDef(M);
	VERIFY(md);
	if (md->flags & esmStopAtEnd)
	{
		CMotion* motion = m_model->dcast_PKinematicsAnimated()->LL_GetRootMotion(M);
		return iFloor(0.5f + 1000.f * motion->GetLength() / (md->Dequantize(md->speed) * speed));
	}
	return 0;
}

void script_attachment::SetBoneVisible(u16 bone_id, bool bVisibility)
{
	if (m_model->dcast_PKinematics()->LL_BoneCount() > bone_id)
	{
		bool bVisibleNow = m_model->dcast_PKinematics()->LL_GetBoneVisible(bone_id);
		if (bVisibleNow != bVisibility)
			m_model->dcast_PKinematics()->LL_SetBoneVisible(bone_id, bVisibility, TRUE);
	}
}

bool script_attachment::GetBoneVisible(u16 bone_id)
{
	if (m_model->dcast_PKinematics()->LL_BoneCount() > bone_id)
		return m_model->dcast_PKinematics()->LL_GetBoneVisible(bone_id);

	return false;
}

void script_attachment::LoadModel(LPCSTR model_name, bool keep_bc)
{
	if (m_model_name == model_name)
		return;

	m_model_name = model_name;

	u16 count_prev;

	if (m_model)
	{
		count_prev = m_model->dcast_PKinematics()->LL_BoneCount();
		::Render->model_Delete(m_model);
	}
	
	m_model = ::Render->model_Create(*m_model_name);
	R_ASSERT(m_model);

	//Bone Callbacks
	if (keep_bc && m_bone_callbacks.size() && count_prev <= m_model->dcast_PKinematics()->LL_BoneCount())
	{
		xr_map<u16, script_attachment_bone_cb*>::const_iterator it = m_bone_callbacks.cbegin();
		xr_map<u16, script_attachment_bone_cb*>::const_iterator it_e = m_bone_callbacks.cend();
		for (; it != it_e; ++it)
		{
			m_model->dcast_PKinematics()->LL_GetBoneInstance((*it).first).set_callback(bctCustom, ScriptAttachmentBoneCallback, (*it).second, (*it).second->m_overwrite);
		}

		PlayMotion(*m_current_motion, false);
	}
}

void script_attachment::SetScriptUI(LPCSTR ui_func)
{
	if (m_script_ui_func != nullptr && 0 == xr_strcmp(m_script_ui_func, ui_func)) return;

	luabind::functor<CUIDialogWndEx*> funct;

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

void script_attachment::SetScriptUIPosition(Fvector pos)
{
	m_script_ui_offset[0] = pos;
	m_script_ui_mat.translate_over(m_script_ui_offset[0]);
}

void script_attachment::SetScriptUIRotation(Fvector rot)
{
	m_script_ui_offset[1] = rot;
	m_script_ui_offset[1].mul(PI / 180.f);
	m_script_ui_mat.setHPB(m_script_ui_offset[1].x, m_script_ui_offset[1].y, m_script_ui_offset[1].z);
	m_script_ui_mat.translate_over(m_script_ui_offset[0]);
}

void script_attachment::ScriptAttachmentBoneCallback(CBoneInstance* B)
{
	script_attachment_bone_cb* params = static_cast<script_attachment_bone_cb*>(B->callback_param());
	bool bVisible = params->m_attachment->GetBoneVisible(params->m_attachment_bone_id);
	Fmatrix& target = bVisible ? B->mTransform : B->mTransformHidden;
	target = params->m_mat;
}

void script_attachment::SetBoneCallback(u16 bone_id, u16 parent_bone, bool overwrite)
{
	if (m_model && m_model->dcast_PKinematics())
	{
		if (bone_id >= m_model->dcast_PKinematics()->LL_BoneCount())
		{
			Msg("![SetBoneCallback]: Attachment has no bone with id %i", bone_id);
			return;
		}

		if (m_bone_callbacks[bone_id])
		{
			m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_id).reset_callback();
			m_bone_callbacks.erase(bone_id);
		}

		m_bone_callbacks[bone_id] = xr_new<script_attachment_bone_cb>(parent_bone, this, bone_id, overwrite);
		m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_id).set_callback(bctCustom, ScriptAttachmentBoneCallback, m_bone_callbacks[bone_id], overwrite);
	}
	else
		Msg("![SetBoneCallback]: Attachment has no visual with bones");
}

void script_attachment::SetBoneCallback(u16 bone_id, const luabind::functor<Fmatrix>& func, bool overwrite)
{
	if (!(m_model && m_model->dcast_PKinematics()))
	{
		Msg("![SetBoneCallback]: Attachment has no visual with bones");
		return;
	}

	if (bone_id >= m_model->dcast_PKinematics()->LL_BoneCount())
	{
		Msg("![SetBoneCallback]: Attachment has no bone with id %i", bone_id);
		return;
	}

	if (m_bone_callbacks[bone_id])
	{
		m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_id).reset_callback();
		m_bone_callbacks.erase(bone_id);
	}

	m_bone_callbacks[bone_id] = xr_new<script_attachment_bone_cb>(func, this, bone_id, overwrite);
	CBoneInstance& bInst = m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_id);
	bInst.set_callback(bctCustom, ScriptAttachmentBoneCallback, m_bone_callbacks[bone_id], overwrite);
	(m_bone_callbacks[bone_id]->m_mat).set(GetBoneVisible(bone_id) ? bInst.mTransformHidden : bInst.mTransform);
}

void script_attachment::RemoveBoneCallback(u16 bone_id)
{
	if (!(m_model && m_model->dcast_PKinematics()))
		return;

	if (m_bone_callbacks[bone_id])
	{
		m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_id).reset_callback();
		m_bone_callbacks.erase(bone_id);
	}
}

Fmatrix script_attachment::GetBoneMatrix(u16 bone_id)
{
	if (!(m_model && m_model->dcast_PKinematics()))
		return Fidentity;

	if (bone_id >= m_model->dcast_PKinematics()->LL_BoneCount())
		return Fidentity;

	return m_model->dcast_PKinematics()->LL_GetTransform(bone_id);
}

Fvector script_attachment::GetCenter()
{
	if (m_model)
		return m_model->getVisData().sphere.P;

	return { 0,0,0 };
}

luabind::object script_attachment::GetShaders()
{
	luabind::object table = luabind::newtable(ai().script_engine().lua());

	if (!m_model)
	{
		table["error"] = true;
		return table;
	}

	xr_vector<IRenderVisual*>* children = m_model->get_children();

	if (!children)
	{
		luabind::object subtable = luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = m_model->getDebugShader();
		subtable["texture"] = m_model->getDebugTexture();
		table[1] = subtable;
		return table;
	}

	int i = 1;

	for (auto* child : *children)
	{
		luabind::object subtable = luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShader();
		subtable["texture"] = child->getDebugTexture();
		table[i] = subtable;
		++i;
	}

	return table;
}

luabind::object script_attachment::GetDefaultShaders()
{
	luabind::object table = luabind::newtable(ai().script_engine().lua());

	if (!m_model)
	{
		table["error"] = true;
		return table;
	}

	xr_vector<IRenderVisual*>* children = m_model->get_children();

	if (!children)
	{
		luabind::object subtable = luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = m_model->getDebugShaderDef();
		subtable["texture"] = m_model->getDebugTextureDef();
		table[1] = subtable;
		return table;
	}

	int i = 1;

	for (auto* child : *children)
	{
		luabind::object subtable = luabind::newtable(ai().script_engine().lua());
		subtable["shader"] = child->getDebugShaderDef();
		subtable["texture"] = child->getDebugTextureDef();
		table[i] = subtable;
		++i;
	}

	return table;
}

void script_attachment::SetShaderTexture(int id, LPCSTR shader, LPCSTR texture)
{
	if (!m_model) return;
	xr_vector<IRenderVisual*>* children = m_model->get_children();

	if (!children)
	{
		m_model->SetShaderTexture(shader, texture);
		return;
	}

	if (id == -1)
	{
		for (auto* child : *children)
		{
			child->SetShaderTexture(shader, texture);
		}
		return;
	}

	id--;

	if (id >= 0 && children->size() > id)
		children->at(id)->SetShaderTexture(shader, texture);
}

void script_attachment::ResetShaderTexture(int id)
{
	if (!m_model) return;
	xr_vector<IRenderVisual*>* children = m_model->get_children();

	if (!children)
	{
		m_model->ResetShaderTexture();
		return;
	}

	if (id == -1)
	{
		for (auto* child : *children)
		{
			child->ResetShaderTexture();
		}
		return;
	}

	id--;

	if (id >= 0 && children->size() > id)
		children->at(id)->ResetShaderTexture();
}