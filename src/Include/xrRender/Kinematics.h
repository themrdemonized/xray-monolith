#ifndef	Kinematics_included
#define	Kinematics_included
#pragma once

#include "RenderVisual.h"
#include "../../xrCore/intrusive_ptr.h"

typedef void (* UpdateCallback)(IKinematics* P);

class CBoneData;
class IBoneData;
class IKinematicsAnimated;
class IRenderVisual;
class ISpatial;
class CBoneInstance;
struct SEnumVerticesCallback;

using ISpatialShared = intrusive_ptr<ISpatial>;

// 10 fps
#define UCalc_Interval		(u32(100))

// pip measured lens geometry for one visual, bind-pose model space, metres
// filled by CKinematics::GetLensDetection, ok == false when no fittable lens
struct SLensDetection
{
	Fvector  eye_center;
	Fvector  eye_normal;
	float    eye_radius;
	Fvector  obj_center;
	float    obj_radius;
	bool     has_objective;
	Fvector4 offset; // scope_objective_lens_offset x,y,z,w in eyepiece-radius units
	float    mm;     // s3ds_objective_mm 2000 x obj_radius, 0 when no objective
	int      source; // 0 lens verts, 1 near bone, 2 tube march objective
	bool     ok;
};

class IKinematics
{
public:
    xrCriticalSection UCalc_Mutex;
	typedef xr_vector<std::pair<shared_str, u16>> accel;

	struct pick_result
	{
		Fvector normal;
		float dist;
		Fvector tri[3];
	};

public:

#ifdef OPTIMIZE_CALCULATE_BONES
	ISpatialShared spatialParent = nullptr;
#endif

	virtual void Bone_Calculate(CBoneData* bd, Fmatrix* parent) = 0;
	virtual void Bone_GetAnimPos(Fmatrix& pos, u16 id, u8 channel_mask, bool ignore_callbacks) = 0;

	virtual bool PickBone(const Fmatrix& parent_xform, pick_result& r, float dist, const Fvector& start,
	                      const Fvector& dir, u16 bone_id) = 0;
	virtual void EnumBoneVertices(SEnumVerticesCallback& C, u16 bone_id) = 0;

	// pip run-or-cached measured lens fit for this visual, false for non-skinned or no lens
	virtual bool GetLensDetection(SLensDetection& out) { return false; }

	// Low level interface
	virtual u16 _BCL LL_BoneID(LPCSTR B) = 0;
	virtual u16 _BCL LL_BoneID(const shared_str& B) = 0;
	virtual LPCSTR _BCL LL_BoneName_dbg(u16 ID) = 0;

	virtual CInifile* _BCL LL_UserData() = 0;
	virtual accel* LL_Bones() = 0;

	virtual ICF CBoneInstance& _BCL LL_GetBoneInstance(u16 bone_id) = 0;

	virtual CBoneData& _BCL LL_GetData(u16 bone_id) = 0;

	virtual const IBoneData& _BCL GetBoneData(u16 bone_id) const = 0;

	virtual u16 _BCL LL_BoneCount() const = 0;
	virtual u16 LL_VisibleBoneCount() = 0;

	virtual ICF Fmatrix& _BCL LL_GetTransform(u16 bone_id) = 0;
	virtual ICF Fmatrix& _BCL LL_GetTransform_safed(u16 bone_id) = 0;
	virtual ICF const Fmatrix& _BCL LL_GetTransform(u16 bone_id) const = 0;
	virtual ICF void _BCL LL_GetBoneLocalPosition(u16 bone_id, Fvector& result) {}
	virtual ICF void _BCL LL_GetBoneLocalTransform(u16 bone_id, Fmatrix& result) {}
	virtual ICF void _BCL LL_GetBoneWorldPosition(u16 bone_id, const Fmatrix& xform, Fvector& result) {}
	virtual ICF void _BCL LL_GetBoneWorldTransform(u16 bone_id, const Fmatrix& xform, Fmatrix& result) {}
	virtual ICF void _BCL CalculateBBox(BOOL bforce = TRUE) {}

	virtual ICF Fmatrix& LL_GetTransform_R(u16 bone_id) = 0;
	virtual Fobb& LL_GetBox(u16 bone_id) = 0;
	virtual const Fbox& _BCL GetBox() const = 0;
	virtual void LL_GetBindTransform(xr_vector<Fmatrix>& matrices) = 0;
	virtual int LL_GetBoneGroups(xr_vector<xr_vector<u16>>& groups) = 0;

	virtual u16 _BCL LL_GetBoneRoot() = 0;
	virtual void LL_SetBoneRoot(u16 bone_id) = 0;

	virtual BOOL _BCL LL_GetBoneVisible(u16 bone_id) = 0;
	virtual void LL_SetBoneVisible(u16 bone_id, BOOL val, BOOL bRecursive) = 0;
	virtual u64 _BCL LL_GetBonesVisible() = 0;
	virtual void LL_SetBonesVisible(u64 mask) = 0;

	//--DSR-- SilencerOverheat_start
	virtual IRenderVisual* GetVisualByBone(u16 bone_id) = 0;
	virtual IRenderVisual* GetVisualByBone(LPCSTR bone_name) = 0;
	//--DSR-- SilencerOverheat_end

	// Main functionality
	virtual void CalculateBones(BOOL bForceExact = FALSE) = 0; // Recalculate skeleton
	virtual void CalculateBones_Invalidate() = 0;
	virtual void Callback(UpdateCallback C, void* Param) = 0;

	//	Callback: data manipulation
	virtual void SetUpdateCallback(UpdateCallback pCallback) = 0;
	virtual void SetUpdateCallbackParam(void* pCallbackParam) = 0;
	virtual bool NeedUCalc() = 0;

	virtual UpdateCallback GetUpdateCallback() = 0;
	virtual void* GetUpdateCallbackParam() = 0;
	//UpdateCallback						Update_Callback;
	//void*								Update_Callback_Param;
	virtual IRenderVisual* _BCL dcast_RenderVisual() = 0;
	virtual IKinematicsAnimated* dcast_PKinematicsAnimated() = 0;

	// debug
#ifdef DEBUG
	virtual void						DebugRender			(Fmatrix& XFORM) = 0;
#endif
	virtual shared_str getDebugName() = 0;
};

IC IKinematics* PKinematics(IRenderVisual* V) { return V ? V->dcast_PKinematics() : 0; }

#endif	//	Kinematics_included
