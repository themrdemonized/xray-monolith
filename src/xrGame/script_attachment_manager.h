#pragma once
#include "Iscript_attachment.h"
#include "script_export_space.h"
#include "script_game_object.h"
#include "script_light_inline.h"
#include "physics_shell_scripted.h"
#include "../xrcdb/ispatial.h"
#include "../xrEngine/IRenderable.h"
#include "../xrEngine/icollidable.h"
#include "../xrEngine/xr_collide_form.h"

class CHudItem;
class CPhysicsShell;
class CODEGeom;

class CCF_Attachment_Skeleton : public CCF_Skeleton
{
protected:
	virtual void BuildState();
	virtual void BuildTopLevel();
public:
	CCF_Attachment_Skeleton(script_attachment* _owner);

	virtual BOOL _RayQuery(const collide::ray_defs& Q, collide::rq_results& R);
};

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
	public ICollidable,
    public Iscript_attachment
{
	friend class CCharacterPhysicsSupport;
	friend class CGameObject;
private:
	struct SavedPosition
	{
		u32 dwTime;
		Fvector vPosition;
	};
private:
	shared_str m_name;

	Fmatrix m_offset;
	Fvector m_attachment_offset[4]; //position, rotation, origin, scale
	float m_custom_fov;

	shared_str m_model_name;
	shared_str m_current_motion;
	u16 m_parent_bone;
    bool m_bVisible;
    Fmatrix m_previous_matrix;

	LPCSTR m_script_ui_func;
	CUIWindow* m_script_ui;
	Fmatrix m_script_ui_mat;
	Fvector m_script_ui_offset[4];
	Fvector2 m_script_ui_scale;
	u16 m_script_ui_bone;

	AttachmentScriptLight* m_script_light;
	u16 m_script_light_bone;

	bool m_bStopAtEndAnimIsRunning, m_bCamCollision;
	u32 m_anim_end;

	u16 m_type;
	script_attachment* m_parent_attachment;
	CGameObject* m_parent_object;
	CGameObject* m_toplevel_parent;
	bool m_parent_level, m_bUsePHShapes, m_ph_frozen;
	xr_map<shared_str, script_attachment*> m_children;
	xr_map<u16, script_attachment_bone_cb*> m_bone_callbacks;

	::luabind::object* m_userdata;

	luabind::functor<bool>* m_ph_contact_callback_func;
	CPhysicsShell* m_physics_shell;
	xr_vector<CODEGeom*> m_attachment_geoms;
	svector<SavedPosition, 4> PositionStack;

    luabind::functor<void>* m_motion_end_cb;

    bool m_bInstanceMultirender;
	CGameObject* m_model_instance_owner;
	CHudItem* m_model_instance_owner_hud;

	u32 m_last_upd_frame;

public:
	script_attachment(LPCSTR name, LPCSTR model_name);
	~script_attachment()
	{
		if (m_physics_shell)
		{
			m_physics_shell->Deactivate();
			xr_delete(m_physics_shell);
		}

		spatial_unregister();
		ClearModelInstance();

		xr_delete(collidable.model);
		::Render->model_Delete(renderable.visual);
		renderable.visual = nullptr;
		delete_data(m_children);
		delete_data(m_bone_callbacks);
		xr_delete(m_userdata);
		xr_delete(m_ph_contact_callback_func);
	}

	virtual void spatial_register();
	virtual void spatial_unregister();
	virtual void spatial_move();
	void spatial_update(float eps_P, float eps_R);
	void spatial_SetObstacle(bool enable);
	bool spatial_GetObstacle();
	void spatial_SetCollidable(bool enable);
	bool spatial_GetCollidable();
	void spatial_SetVisible(bool enable);
	bool spatial_GetVisible();
	virtual IRenderable* dcast_Renderable() { return this; }
	virtual ICollidable* dcast_Collidable() { return this; }

	virtual void renderable_Render(IDSGraphManager* DM);
	void Render(IKinematics* model, const Fmatrix& parent_transform, IDSGraphManager* DM);
	void RenderAt(IDSGraphManager* DM, const Fmatrix& transform);
	void RenderUI(bool defer_r1 = true);
	virtual void renderable_RenderUI() override { RenderUI(); }
    bool HasUI() { return m_script_ui; }
	void RenderInstanceAttachments(IDSGraphManager* DM, const Fmatrix& transform);
	void UpdateInstanceAttachments();

	void AttachLight(AttachmentScriptLight* light);
	AttachmentScriptLight* DetachLight();
	AttachmentScriptLight* GetLight();
	void SetScriptLightBone(u16 bone) { m_script_light_bone = bone; }
	void SetScriptLightBone(LPCSTR bone) { m_script_light_bone = bone_id(bone); }
	u16 GetScriptLightBone() { return m_script_light_bone; }

	void Update();
	void UpdateXFORM(IKinematics* model, Fmatrix* mat);
	void OnUpdateXFORM();
    void ForceUpdateXFORM(bool recursive = false);
	void RefreshVisBoxChain(u16 type);
	void UpdateBoneCallbacks();
	void RecalcOffset();

	void SetCustomFOV(float fov) { m_custom_fov = fov; };
	float GetCustomFOV() { return m_custom_fov; };

	void SetPosition(Fvector pos) { SetPosition(pos.x, pos.y, pos.z); }
	void SetPosition(float x, float y, float z);
	Fvector GetPosition() { return m_attachment_offset[0]; }

	void SetRotation(Fvector rot) { SetRotation(rot.x, rot.y, rot.z); }
	void SetRotation(float x, float y, float z);
	Fvector GetRotation() { return m_attachment_offset[1]; }

	void SetScale(Fvector scale);
    void SetScale(float scale) { SetScale(Fvector().set(scale, scale, scale)); }
	Fvector GetScale() { return m_attachment_offset[3]; }
	float GetScaleVal()
    {
        return (m_attachment_offset[3].x == m_attachment_offset[3].y && m_attachment_offset[3].y == m_attachment_offset[3].z)
            ? m_attachment_offset[3].x
            : (m_attachment_offset[3].x + m_attachment_offset[3].y + m_attachment_offset[3].z) / 3.f;
    }
    float GetScaleMax() { return fmaxf(m_attachment_offset[3].x, fmaxf(m_attachment_offset[3].y, m_attachment_offset[3].z));}

	void SetOrigin(Fvector org) { SetOrigin(org.x, org.y, org.z); }
	void SetOrigin(float x, float y, float z);
	Fvector GetOrigin() { return m_attachment_offset[2]; }

	void SetParent(script_attachment* att);
	void SetParent(CGameObject* obj);
	void SetParent(CScriptGameObject* obj);
	void SetParentLevel();
	::luabind::object GetParent();
	void SetTopLevelParent(CGameObject* obj);
	CGameObject* GetTopLevelParent() { return m_toplevel_parent; }
	CScriptGameObject* GetTopLevelParentScript() { return m_toplevel_parent ? m_toplevel_parent->lua_game_object() : nullptr; }

	void SetParentBone(u16 bone_id);
	void SetParentBone(LPCSTR bone);
	u16 GetParentBone() { return m_parent_bone; }

	void CreatePhysicsShell();
	void SetPHFreeze(bool freeze);
	bool GetPHFreeze() { return m_ph_frozen; }
	cphysics_shell_scripted* get_physics_shell() const;
	void DBGDrawPhysicsShell();
	void SetPHContactCallback(LPCSTR func);
	void SetPHContactCallback(const luabind::functor<bool>& func);

	static void ObjectContactCallback(bool& do_colide, bool bo1, dContact& c, SGameMtl* /*material_1*/,
		SGameMtl* /*material_2*/);

	void LoadModel(LPCSTR model_name, bool keep_bc = false);
	LPCSTR GetModelScript() { return *m_model_name; }

    void SetModelInstanceMultirender(bool val);
    bool ModelInstanceMultirender() { return m_bInstanceMultirender; }
	void ModelInstance(CScriptGameObject* obj, bool bHud);
	void ModelInstance(::luabind::object obj, bool bHud);
	void ModelInstance(CScriptGameObject* obj) { ModelInstance(obj, false); }
	void ModelInstance(::luabind::object obj) { ModelInstance(obj, false); }
	void ClearModelInstance();

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

	void SetType(u16 type);
	u16 GetType() { return m_type; }

	u32 PlayMotionCB(LPCSTR name, bool mixin, float speed, const ::luabind::functor<void>& motion_end_cb);
	u32 PlayMotionCB(LPCSTR name, bool mixin, float speed, LPCSTR motion_end_cb);
	u32 PlayMotion(LPCSTR name, bool mixin = true, float speed = 1.f);
	u32 motion_length(const MotionID& M, const CMotionDef*& md, float speed);
	
	void AddToParentShell(CGameObject* obj);
	void RemoveFromParentShell(CGameObject* obj);
	void OnParentCreatePhysicsShell();
	void OnParentDestroyPhysicsShell();

	void SetUsePHShapes(bool b);
	bool GetUsePHShapes() { return m_bUsePHShapes; }

	u16 bone_id(LPCSTR bone_name);
	LPCSTR bone_name(u16 bone_id);

	bool GetBoneVisible(u16 bone_id);
	bool GetBoneVisible(LPCSTR bone_name) { return GetBoneVisible(bone_id(bone_name)); }

	void SetBoneVisible(u16 bone_id, bool bVisibility, bool bRecursive = true);
	void SetBoneVisible(LPCSTR bone_name, bool bVisibility, bool bRecursive = true) { SetBoneVisible(bone_id(bone_name), bVisibility, bRecursive); }

	Fmatrix bone_transform(u16 bone_id, bool bPrev);
	Fmatrix bone_transform(u16 bone_id) { return bone_transform(bone_id, false); }
	Fmatrix bone_transform(LPCSTR bone_name) { return bone_transform(bone_id(bone_name), false); }
	Fmatrix bone_transform(LPCSTR bone_name, bool bPrev) { return bone_transform(bone_id(bone_name), bPrev); }

	Fvector bone_position(u16 bone_id);
	Fvector bone_position(LPCSTR bone_name) { return bone_position(bone_id(bone_name)); }

	Fvector bone_direction(u16 bone_id);
	Fvector bone_direction(LPCSTR bone_name) { return bone_direction(bone_id(bone_name)); }

	u16 bone_parent(u16 bone_id);
	u16 bone_parent(LPCSTR bone_name) { return bone_parent(bone_id(bone_name)); }

	::luabind::object list_bones();
	
	Fmatrix& BoneTransform(IKinematics* model);

	static void _BCL ScriptAttachmentBoneCallback(CBoneInstance* B);
	void SetBoneCallback(u16 bone_id, u16 parent_bone, bool overwrite);
	void SetBoneCallback(LPCSTR bone, LPCSTR parent_bone, bool overwrite);
	void SetBoneCallback(u16 bone_id, LPCSTR parent_bone, bool overwrite);
	void SetBoneCallback(LPCSTR bone, u16 parent_bone, bool overwrite) { SetBoneCallback(bone_id(bone), parent_bone, overwrite); }
	void SetBoneCallback(u16 bone_id, const ::luabind::functor<Fmatrix>& func, bool overwrite);
	void SetBoneCallback(LPCSTR bone, const ::luabind::functor<Fmatrix>& func, bool overwrite) { SetBoneCallback(bone_id(bone), func, overwrite); }

	void SetBoneCallback(u16 bone_id, u16 parent_bone) { SetBoneCallback(bone_id, parent_bone, false); }
	void SetBoneCallback(LPCSTR bone, LPCSTR parent_bone) { SetBoneCallback(bone, parent_bone, false); }
	void SetBoneCallback(u16 bone_id, LPCSTR parent_bone) { SetBoneCallback(bone_id, parent_bone, false); }
	void SetBoneCallback(LPCSTR bone, u16 parent_bone) { SetBoneCallback(bone_id(bone), parent_bone, false); }
	void SetBoneCallback(u16 bone_id, const ::luabind::functor<Fmatrix>& func) { SetBoneCallback(bone_id, func, false); }
	void SetBoneCallback(LPCSTR bone, const ::luabind::functor<Fmatrix>& func) { SetBoneCallback(bone_id(bone), func, false); }

	void RemoveBoneCallback(u16 bone_id);
	void RemoveBoneCallback(LPCSTR bone) { RemoveBoneCallback(bone_id(bone)); }

	Fmatrix GetTransform() { return renderable.xform; }
	Fmatrix& XFORM() { return renderable.xform; }
    const Fmatrix& XFORM_prev() { return m_previous_matrix; }
	IRenderVisual* Visual() const;
	IKinematics* Kinematics() const;
	Fmatrix GetOffset() const { return m_offset; }
	Fvector Position() const;
	Fvector Center() const;
	float Radius() const;
	const Fbox& Box();
    bool has_shapes();
    ::luabind::object script_attachment::BoneShape(u16 id, bool local = false);
	xr_map<shared_str, script_attachment*>* GetAttachments() { return &m_children; }

	::luabind::object GetShaders();
	::luabind::object GetDefaultShaders();
	void SetShaderTexture(int id, LPCSTR shader, LPCSTR texture);
	void ResetShaderTexture(int id);

    void SetCamCollide(bool b) { m_bCamCollision = b; }
    bool GetCamCollide() { return m_bCamCollision; }

	DECLARE_SCRIPT_REGISTER_FUNCTION
};
