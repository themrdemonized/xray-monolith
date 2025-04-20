#pragma once
#include "script_export_space.h"
#include "script_game_object.h"
#include "script_light_inline.h"

enum script_attachment_flags
{
	eSA_RenderHUD = (1 << 0),
	eSA_RenderWorld = (1 << 1),
	eSA_CamAttached = (1 << 2),
};

struct script_attachment_bone_cb
{
	u16 m_bone_id, m_attachment_bone_id;
	::luabind::functor<Fmatrix>* m_func;
	script_attachment* m_attachment;
	Fmatrix m_mat;
	bool m_overwrite;

	script_attachment_bone_cb(const ::luabind::functor<Fmatrix>& func, script_attachment* att, u16 id, bool overwrite)
	{
		m_attachment = att;
		m_attachment_bone_id = id;
		m_func = xr_new<::luabind::functor<Fmatrix>>(func);
		m_mat = Fidentity;
		m_bone_id = BI_NONE;
		m_overwrite = overwrite;
	}

	script_attachment_bone_cb(u16 bone, script_attachment* att, u16 id, bool overwrite)
	{
		m_attachment_bone_id = id;
		m_attachment = att;
		m_mat = Fidentity;
		m_bone_id = bone;
		m_overwrite = overwrite;
	}

	~script_attachment_bone_cb() {}
};

class script_attachment
{
private:
	Fmatrix m_offset, m_transform;
	Fvector m_position, m_rotation, m_scale, m_origin;

	IRenderVisual* m_model;
	shared_str m_model_name;
	shared_str m_current_motion;
	u16 m_slot, m_parent_bone;

	LPCSTR m_script_ui_func;
	CUIWindow* m_script_ui;
	Fmatrix m_script_ui_mat;
	Fvector m_script_ui_offset[2];
	u16 m_script_ui_bone;

	AttachmentScriptLight* m_script_light;
	u16 m_script_light_bone;

	bool m_bStopAtEndAnimIsRunning;
	u32 m_anim_end;

	Flags32 m_flags;
	script_attachment* m_parent_attachment;
	CGameObject* m_parent_object;
	xr_map<u16, script_attachment*> m_children;
	xr_map<u16, script_attachment_bone_cb*> m_bone_callbacks;

	u32 m_last_upd_frame;

public:
	script_attachment(u16 id, LPCSTR model_name);
	~script_attachment()
	{
		::Render->model_Delete(m_model);
		m_model = nullptr;
		delete_data(m_children);
		delete_data(m_bone_callbacks);
	}

	void Render(IKinematics* model, Fmatrix* mat, bool hud_mode = false);
	void Update();
	void RenderUI(bool hud_mode = false);

	void AttachLight(AttachmentScriptLight* light);
	AttachmentScriptLight* DetachLight();
	AttachmentScriptLight* GetLight();

	void RecalcOffset();

	void SetPosition(Fvector pos);
	void SetPosition(float x, float y, float z) { SetPosition(Fvector().set(x, y, z)); }
	Fvector GetPosition() { return m_position; }

	void SetRotation(Fvector rot);
	void SetRotation(float x, float y, float z) { SetRotation(Fvector().set(x, y, z)); }
	Fvector GetRotation() { return m_rotation; }

	void SetScale(Fvector scale);
	void SetScale(float x, float y, float z) { SetScale(Fvector().set(x, y, z)); }
	void SetScale(float scale) { SetScale(Fvector().set(scale, scale, scale)); }
	Fvector GetScale() { return m_scale; }

	void SetOrigin(Fvector org);
	void SetOrigin(float x, float y, float z) { SetOrigin(Fvector().set(x, y, z)); }
	Fvector GetOrigin() { return m_origin; }

	void SetParent(script_attachment* att);
	void SetParent(CGameObject* obj);
	void SetParent(CScriptGameObject* obj);
	::luabind::object GetParent();

	void SetParentBone(u16 bone_id) { m_parent_bone = bone_id; }
	u16 GetParentBone() { return m_parent_bone; }

	void LoadModel(LPCSTR model_name, bool keep_bc = false);
	LPCSTR GetModelScript() { return *m_model_name; }

	void SetScriptUI(LPCSTR ui_func);
	LPCSTR GetScriptUI() { return m_script_ui_func; }
	void SetScriptUIPosition(Fvector pos);
	void SetScriptUIPosition(float x, float y, float z) { SetScriptUIPosition(Fvector().set(x, y, z)); }
	Fvector GetScriptUIPosition() { return m_script_ui_offset[0]; }
	void SetScriptUIRotation(Fvector rot);
	void SetScriptUIRotation(float x, float y, float z) { SetScriptUIRotation(Fvector().set(x, y, z)); }
	Fvector GetScriptUIRotation() { return m_script_ui_offset[1]; }
	void SetScriptUIBone(u16 bone) { m_script_ui_bone = bone; }
	u16 GetScriptUIBone() { return m_script_ui_bone; }

	script_attachment* AddAttachment(u16 slot, LPCSTR model_name);
	void RemoveAttachment(u16 slot) { RemoveChild(slot, true); }
	script_attachment* AddChild(u16 slot, script_attachment* att);
	script_attachment* GetChild(u16 slot);
	void RemoveChild(u16 slot, bool destroy = false);

	void SetFlags(u32 flags) { m_flags.assign(flags); }
	u32 GetFlags() { return m_flags.get(); }
	const Flags32& GetFFlags() { return m_flags; }

	u32 PlayMotion(LPCSTR name, bool mixin = true, float speed = 1.f);
	u32 motion_length(const MotionID& M, const CMotionDef*& md, float speed);

	void SetBoneVisible(u16 bone_id, bool bVisibility);
	bool GetBoneVisible(u16 bone_id);
	Fmatrix& BoneTransform(IKinematics* model);

	static void _BCL ScriptAttachmentBoneCallback(CBoneInstance* B);
	void SetBoneCallback(u16 bone_id, u16 parent_bone, bool overwrite = false);
	void SetBoneCallback(u16 bone_id, const ::luabind::functor<Fmatrix>& func, bool overwrite = false);
	void RemoveBoneCallback(u16 bone_id);

	Fmatrix GetBoneMatrix(u16 bone_id);
	Fmatrix GetTransform() { return m_transform; };
	Fvector GetCenter();

	::luabind::object GetShaders();
	::luabind::object GetDefaultShaders();
	void SetShaderTexture(int id, LPCSTR shader, LPCSTR texture);
	void ResetShaderTexture(int id);

	DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(script_attachment)
#undef script_type_list
#define script_type_list save_type_list(script_attachment)