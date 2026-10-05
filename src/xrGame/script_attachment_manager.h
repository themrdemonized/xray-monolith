#pragma once
#include "script_export_space.h"
#include "script_game_object.h"
#include "script_light_inline.h"

#include "../xrcdb/ispatial.h"
#include "../xrEngine/IRenderable.h"

enum script_attachment_type
{
	eSA_HUD = 0,
	eSA_World,
	eSA_CamAttached,
	eSA_undefined
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
		m_attachment_bone_id = id;
		m_attachment = att;
		m_func = xr_new<::luabind::functor<Fmatrix>>(func);
		m_mat = Fidentity;
		m_bone_id = BI_NONE;
		m_overwrite = overwrite;
	}

	script_attachment_bone_cb(u16 bone, script_attachment* att, u16 id, bool overwrite)
	{
		m_attachment_bone_id = id;
		m_attachment = att;
		m_func = nullptr;
		m_mat = Fidentity;
		m_bone_id = bone;
		m_overwrite = overwrite;
	}

	~script_attachment_bone_cb() {}
};

class script_attachment :
	public IRenderable,
	public ISpatial
{
private:
	shared_str m_name;

	Fmatrix m_offset;
	Fvector m_attachment_offset[4];

	IKinematics* m_kinematics;
	shared_str m_model_name;
	shared_str m_current_motion;
	u16 m_parent_bone;

	LPCSTR m_script_ui_func;
	CUIWindow* m_script_ui;
	Fmatrix m_script_ui_mat;
	Fvector m_script_ui_offset[4];
	Fvector2 m_script_ui_scale;
	u16 m_script_ui_bone;

	AttachmentScriptLight* m_script_light;
	u16 m_script_light_bone;

	bool m_bStopAtEndAnimIsRunning;
	u32 m_anim_end;

	u16 m_type;
	script_attachment* m_parent_attachment;
	CGameObject* m_parent_object;
	bool m_parent_level;
	xr_map<shared_str, script_attachment*> m_children;
	xr_map<u16, script_attachment_bone_cb*> m_bone_callbacks;

	::luabind::object* m_userdata;

	u32 m_last_upd_frame;

public:
	script_attachment(LPCSTR name, LPCSTR model_name);
	~script_attachment()
	{
		spatial_unregister();
		::Render->model_Delete(renderable.visual);
		renderable.visual = nullptr;
		delete_data(m_children);
		delete_data(m_bone_callbacks);
		xr_delete(m_userdata);
	}

	virtual void spatial_register();
	virtual void spatial_unregister();
	virtual void spatial_move();
	virtual IRenderable* dcast_Renderable() { return this; }

	virtual void renderable_Render();

	void Render(IKinematics* model, Fmatrix* mat);
	void Update();
	void RenderUI();

	void AttachLight(AttachmentScriptLight* light);
	AttachmentScriptLight* DetachLight();
	AttachmentScriptLight* GetLight();
	void SetScriptLightBone(u16 bone) { m_script_light_bone = bone; }
	void SetScriptLightBone(LPCSTR bone) { m_script_light_bone = bone_id(bone); }
	u16 GetScriptLightBone() { return m_script_light_bone; }

	void RecalcOffset();

	void SetPosition(Fvector pos) { SetPosition(pos.x, pos.y, pos.z); }
	void SetPosition(float x, float y, float z);
	Fvector GetPosition() { return m_attachment_offset[0]; }

	void SetRotation(Fvector rot) { SetRotation(rot.x, rot.y, rot.z); }
	void SetRotation(float x, float y, float z);
	Fvector GetRotation() { return m_attachment_offset[1]; }

	void SetScale(Fvector scale) { SetScale(scale.x, scale.y, scale.z); }
	void SetScale(float x, float y, float z);
	void SetScale(float scale) { SetScale(Fvector().set(scale, scale, scale)); }
	Fvector GetScale() { return m_attachment_offset[2]; }

	void SetOrigin(Fvector org) { SetOrigin(org.x, org.y, org.z); }
	void SetOrigin(float x, float y, float z);
	Fvector GetOrigin() { return m_attachment_offset[3]; }

	void SetParent(script_attachment* att);
	void SetParent(CGameObject* obj);
	void SetParent(CScriptGameObject* obj);
	void SetParentLevel();
	::luabind::object GetParent();

	void SetParentBone(u16 bone_id) { m_parent_bone = bone_id; }
	void SetParentBone(LPCSTR bone);
	u16 GetParentBone() { return m_parent_bone; }

	void LoadModel(LPCSTR model_name, bool keep_bc = false);
	LPCSTR GetModelScript() { return *m_model_name; }

	void SetName(LPCSTR name);
	LPCSTR GetName() { return *m_name; }

	const ::luabind::object& GetUserdata() const;
	void SetUserdata(::luabind::object obj);

	void SetScriptUI(LPCSTR ui_func);
	LPCSTR GetScriptUI() { return m_script_ui_func; }

	void RecalcScriptUIOffset();

	void SetScriptUIPosition(Fvector pos) { SetScriptUIPosition(pos.x, pos.y, pos.z); }
	void SetScriptUIPosition(float x, float y, float z);
	Fvector GetScriptUIPosition() { return m_script_ui_offset[0]; }

	void SetScriptUIRotation(Fvector rot) { SetScriptUIRotation(rot.x, rot.y, rot.z); }
	void SetScriptUIRotation(float x, float y, float z);
	Fvector GetScriptUIRotation() { return m_script_ui_offset[1]; }

	void SetScriptUIScale(Fvector rot) { SetScriptUIScale(rot.x, rot.y, rot.z); }
	void SetScriptUIScale(float x, float y, float z);
	Fvector GetScriptUIScale() { return m_script_ui_offset[2]; }

	void SetScriptUIOrigin(Fvector rot) { SetScriptUIOrigin(rot.x, rot.y, rot.z); }
	void SetScriptUIOrigin(float x, float y, float z);
	Fvector GetScriptUIOrigin() { return m_script_ui_offset[3]; }

	void SetScriptUIBone(u16 bone) { m_script_ui_bone = bone; }
	void SetScriptUIBone(LPCSTR bone) { m_script_ui_bone = bone_id(bone); }
	u16 GetScriptUIBone() { return m_script_ui_bone; }

	script_attachment* AddAttachment(LPCSTR name, LPCSTR model_name);
	void RemoveAttachment(LPCSTR name) { RemoveChild(name, true); }
	void RemoveAttachment(script_attachment* child);
	script_attachment* AddChild(LPCSTR name, script_attachment* att);
	script_attachment* GetChild(LPCSTR name);
	void RemoveChild(LPCSTR name, bool destroy = false);
	void IterateAttachments(::luabind::functor<bool> functor);

	void SetType(u16 type) { m_type = type < eSA_undefined ? type : eSA_World; }
	u16 GetType() { return m_type; }

	u32 PlayMotion(LPCSTR name, bool mixin = true, float speed = 1.f);
	u32 motion_length(const MotionID& M, const CMotionDef*& md, float speed);
	
	u16 bone_id(LPCSTR bone_name);
	LPCSTR bone_name(u16 bone_id);

	bool GetBoneVisible(u16 bone_id);
	bool GetBoneVisible(LPCSTR bone_name) { return GetBoneVisible(bone_id(bone_name)); }

	void SetBoneVisible(u16 bone_id, bool bVisibility, bool bRecursive = true);
	void SetBoneVisible(LPCSTR bone_name, bool bVisibility, bool bRecursive = true) { SetBoneVisible(bone_id(bone_name), bVisibility, bRecursive); }

	Fmatrix bone_transform(u16 bone_id);
	Fmatrix bone_transform(LPCSTR bone_name) { return bone_transform(bone_id(bone_name)); }

	Fvector bone_position(u16 bone_id);
	Fvector bone_position(LPCSTR bone_name) { return bone_position(bone_id(bone_name)); }

	Fvector bone_direction(u16 bone_id);
	Fvector bone_direction(LPCSTR bone_name) { return bone_direction(bone_id(bone_name)); }

	u16 bone_parent(u16 bone_id);
	u16 bone_parent(LPCSTR bone_name) { return bone_parent(bone_id(bone_name)); }

	::luabind::object list_bones();
	
	Fmatrix& BoneTransform(IKinematics* model);

	static void _BCL ScriptAttachmentBoneCallback(CBoneInstance* B);
	void SetBoneCallback(u16 bone_id, u16 parent_bone, bool overwrite = false);
	void SetBoneCallback(LPCSTR bone, LPCSTR parent_bone, bool overwrite = false);
	void SetBoneCallback(u16 bone, LPCSTR parent_bone, bool overwrite = false);
	void SetBoneCallback(LPCSTR bone, u16 parent_bone, bool overwrite = false) { SetBoneCallback(bone_id(bone), parent_bone, overwrite); }
	void SetBoneCallback(u16 bone_id, const ::luabind::functor<Fmatrix>& func, bool overwrite = false);
	void SetBoneCallback(LPCSTR bone, const ::luabind::functor<Fmatrix>& func, bool overwrite = false) { SetBoneCallback(bone_id(bone), func, overwrite); }
	void RemoveBoneCallback(u16 bone_id);
	void RemoveBoneCallback(LPCSTR bone) { RemoveBoneCallback(bone_id(bone)); }

	Fmatrix GetTransform() { return renderable.xform; }
	Fmatrix GetOffset() { return m_offset; }
	Fvector GetCenter();
	const Fbox& Box();
	xr_map<shared_str, script_attachment*>* GetAttachments() { return &m_children; }

	::luabind::object GetShaders();
	::luabind::object GetDefaultShaders();
	void SetShaderTexture(int id, LPCSTR shader, LPCSTR texture);
	void ResetShaderTexture(int id);

	DECLARE_SCRIPT_REGISTER_FUNCTION
};
