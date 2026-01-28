//---------------------------------------------------------------------------
// Vulkan independent version of SkeletonRigid.cpp
// Copied from src/Layers/xrRender/SkeletonRigid.cpp
//---------------------------------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "vk_SkeletonCompat.h"

#define FBasicVisualH  // Prevent FBasicVisual.h from being included
#include "../xrRender/SkeletonCustom.h"
#undef FBasicVisualH

#include "../../xrCore/profiler.h"

extern int psSkeletonUpdate;

#ifdef DEBUG
void check_kinematics(CKinematics* _k, LPCSTR s);
#endif

extern float IK_CALC_DIST;
extern float IK_ALWAYS_CALC_DIST;
extern float IK_CALC_SSA;
extern ENGINE_API BOOL g_bootComplete;
extern BOOL r_optimize_calculate_bones;

class IRenderable;

void CKinematics::CalculateBones(BOOL bForceExact)
{
	PROF_EVENT("CKinematics::CalculateBones");
	if (RDEVICE.dwTimeGlobal == UCalc_Time) return;

	float update_rate_k = 1.f;

#ifdef OPTIMIZE_CALCULATE_BONES
	if (g_bootComplete)
	{
		if (spatialParent)
		{
			auto& sphere = spatialParent->spatial.sphere;

			float dist = 0.f;
			float perceived_dist = Device.GetPerceivedDist(sphere.P, &dist);
			float dist_k = dist / perceived_dist;
			float ssa = Device.CalcSSADynamic(sphere.P, sphere.R);
			float ssa_k = IK_CALC_SSA / ssa;
			update_rate_k = _max(1.f, ssa_k);

			bool visibleCheck = (perceived_dist < IK_ALWAYS_CALC_DIST) || ::Render->ViewBase.testSphere_dirty(sphere.P, sphere.R);
			if (!visibleCheck)
			{
				bForceExact = FALSE;
				update_rate_k = _max(2.f, update_rate_k);
			}

			if (r_optimize_calculate_bones && canBeOptimized() && (ssa < IK_CALC_SSA))
			{
				bForceExact = FALSE;
			}
		}
	}
#endif

	xrCriticalSectionGuard g(UCalc_Mutex);
	OnCalculateBones();
	if (!bForceExact && (RDEVICE.dwTimeGlobal < (UCalc_Time + UCalc_Interval * update_rate_k))) return;
	if (Update_Visibility) Visibility_Update();

	_DBG_SINGLE_USE_MARKER;
	UCalc_Time = RDEVICE.dwTimeGlobal;

#ifdef DEBUG
	RDEVICE.Statistic->Animation.Begin();
#endif

	Bone_Calculate(bones->at(iRoot), &Fidentity);
#ifdef DEBUG
	check_kinematics(this, dbg_name.c_str());
	RDEVICE.Statistic->Animation.End();
#endif
	VERIFY(LL_GetBonesVisible()!=0);
	UCalc_Visibox++;
	if (UCalc_Visibox >= psSkeletonUpdate)
	{
		UCalc_Visibox = -(::Random.randI(psSkeletonUpdate - 1));
		UCalc_ThisFrame = true;

		Fbox Box;
		Box.invalidate();
		for (u32 b = 0; b < bones->size(); b++)
		{
			if (!LL_GetBoneVisible(u16(b))) continue;
			Fobb& obb = (*bones)[b]->obb;
			Fmatrix& Mbone = bone_instances[b].mTransform;
			Fmatrix Mbox;
			obb.xform_get(Mbox);
			Fmatrix X;
			X.mul_43(Mbone, Mbox);
			Fvector& S = obb.m_halfsize;

			Fvector P, A;
			A.set(-S.x, -S.y, -S.z);
			X.transform_tiny(P, A);
			Box.modify(P);
			A.set(-S.x, -S.y, S.z);
			X.transform_tiny(P, A);
			Box.modify(P);
			A.set(S.x, -S.y, S.z);
			X.transform_tiny(P, A);
			Box.modify(P);
			A.set(S.x, -S.y, -S.z);
			X.transform_tiny(P, A);
			Box.modify(P);
			A.set(-S.x, S.y, -S.z);
			X.transform_tiny(P, A);
			Box.modify(P);
			A.set(-S.x, S.y, S.z);
			X.transform_tiny(P, A);
			Box.modify(P);
			A.set(S.x, S.y, S.z);
			X.transform_tiny(P, A);
			Box.modify(P);
			A.set(S.x, S.y, -S.z);
			X.transform_tiny(P, A);
			Box.modify(P);
		}
		if (bones->size())
		{
			vis.box.min = (Box.min);
			vis.box.max = (Box.max);
			vis.box.getsphere(vis.sphere.P, vis.sphere.R);
		}
#ifdef DEBUG
		VERIFY3(_valid(vis.box.min)&&_valid(vis.box.max), "Invalid bones-xform in model", dbg_name.c_str());
		if(vis.sphere.R>1000.f)
		{
			for(u16 ii=0; ii<LL_BoneCount();++ii){
				Fmatrix tr;
				tr = LL_GetTransform(ii);
				Log("bone ",LL_BoneName_dbg(ii));
				Log("bone_matrix",tr);
			}
			Log("end-------");
		}
		VERIFY3(vis.sphere.R<1000.f, "Invalid bones-xform in model", dbg_name.c_str());
#endif
	}
	else
		UCalc_ThisFrame = false;

	if (Update_Callback) Update_Callback(this);
}

#ifdef DEBUG
void check_kinematics(CKinematics* _k, LPCSTR s)
{
	CKinematics* K = _k;
	Fmatrix& MrootBone = K->LL_GetBoneInstance(K->LL_GetBoneRoot()).mTransform;
	if(MrootBone.c.y >10000)
	{
		Msg("all bones transform:--------[%s]",s);

		for(u16 ii=0; ii<K->LL_BoneCount();++ii){
			Fmatrix tr;
			tr = K->LL_GetTransform(ii);
			Log("bone ",K->LL_BoneName_dbg(ii));
			Log("bone_matrix",tr);
		}
		Log("end-------");
		VERIFY3(0,"check_kinematics failed for ", s);
	}
}
#endif

void CKinematics::BuildBoneMatrix(const CBoneData* bd, CBoneInstance& bi, const Fmatrix* parent,
                                  u8 channel_mask/* = (1<<0)*/)
{
	if (LL_GetBoneVisible(bd->GetSelfID()))
	{
		bi.mTransform.mul_43(*parent, bd->bind_transform);
		bi.mTransformHidden.set(bi.mTransform);
	}
	else
	{
		bi.mTransform.c = (*parent).c;
		bi.mTransformHidden.mul_43(*parent, bd->bind_transform);
	}
}

void CKinematics::CLBone(const CBoneData* bd, CBoneInstance& bi, const Fmatrix* parent, u8 channel_mask /*= (1<<0)*/)
{
	if (!bi.callback_overwrite())
		BuildBoneMatrix(bd, bi, parent, channel_mask);

	if (bi.callback())
		bi.callback()(&bi);

	bi.mRenderTransform.mul_43(bi.mTransform, bd->m2b_transform);
}

void CKinematics::Bone_GetAnimPos(Fmatrix& pos, u16 id, u8 mask_channel, bool ignore_callbacks)
{
	R_ASSERT(id<LL_BoneCount());
	CBoneInstance bi = LL_GetBoneInstance(id);
	BoneChain_Calculate(&LL_GetData(id), bi, mask_channel, ignore_callbacks);
#ifndef MASTER_GOLD
	R_ASSERT( _valid( bi.mTransform ) );
#endif
	pos.set(bi.mTransform);
}

void CKinematics::Bone_Calculate(CBoneData* bd, Fmatrix* parent)
{
	xrCriticalSectionGuard g(UCalc_Mutex2);
	u16 SelfID = bd->GetSelfID();
	CBoneInstance& BONE_INST = LL_GetBoneInstance(SelfID);
	CLBone(bd, BONE_INST, parent, u8(-1));
	for (xr_vector<CBoneData*>::iterator C = bd->children.begin(); C != bd->children.end(); C++)
		Bone_Calculate(*C, &BONE_INST.mTransform);
}

void CKinematics::BoneChain_Calculate(const CBoneData* bd, CBoneInstance& bi, u8 mask_channel, bool ignore_callbacks)
{
	u16 SelfID = bd->GetSelfID();
	BoneCallback bc = bi.callback();
	BOOL ow = bi.callback_overwrite();
	if (ignore_callbacks)
	{
		bi.set_callback(bi.callback_type(), 0, bi.callback_param(), 0);
	}
	if (SelfID == LL_GetBoneRoot())
	{
		CLBone(bd, bi, &Fidentity, mask_channel);
		bi.set_callback(bi.callback_type(), bc, bi.callback_param(), ow);
		return;
	}
	u16 ParentID = bd->GetParentID();
	R_ASSERT(ParentID != BI_NONE);
	CBoneData* ParrentDT = &LL_GetData(ParentID);
	CBoneInstance parrent_bi = LL_GetBoneInstance(ParentID);
	BoneChain_Calculate(ParrentDT, parrent_bi, mask_channel, ignore_callbacks);
	CLBone(bd, bi, &parrent_bi.mTransform, mask_channel);
	bi.set_callback(bi.callback_type(), bc, bi.callback_param(), ow);
}

extern "C" void* vkCreateKinematics()
{
	return xr_new<CKinematics>();
}
