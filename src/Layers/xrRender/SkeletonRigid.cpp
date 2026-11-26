//---------------------------------------------------------------------------
#include 	"stdafx.h"
#pragma hdrstop

#include 	"SkeletonCustom.h"

#include "../../xrCore/profiler.h"

extern int psSkeletonUpdate;

#ifdef DEBUG
void check_kinematics(CKinematics* _k, LPCSTR s);
#endif

extern float IK_CALC_DIST;
extern float IK_ALWAYS_CALC_DIST;
extern float IK_CALC_SSA;
extern ENGINE_API BOOL g_bootComplete;
BOOL r_optimize_calculate_bones = TRUE;

class IRenderable;

void CKinematics::CalculateBones(BOOL bForceExact)
{
	PROF_EVENT("CKinematics::CalculateBones");
	// early out.
	// check if the info is still relevant
	// skip all the computations - assume nothing changes in a small period of time :)
	if (RDEVICE.dwTimeGlobal == UCalc_Time) return; // early out for "fast" update

	// demonized: reduce calculate bones updates when the object is far away and not in frustum
	// Available only if can get parent xform
	// Refactor later for per object basis
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

			// Visibility check, perform always
			bool visibleCheck = (perceived_dist < IK_ALWAYS_CALC_DIST) || ::Render->ViewBase.testSphere_dirty(sphere.P, sphere.R);
			if (!visibleCheck)
			{
				bForceExact = FALSE;
				update_rate_k = _max(2.f, update_rate_k);

				/*if (RDEVICE.dwTimeGlobal % 100 < 10)
				{
					Msg("CKinematics::CalculateBones, object out of frustum, dist %.2f, update_rate_k %.2f", dist / dist_k, update_rate_k);
				}*/
			}

			// screen space area check, perform when cvar is enabled and can be optimized
			if (r_optimize_calculate_bones && canBeOptimized() && (ssa < IK_CALC_SSA))
			{
				bForceExact = FALSE;

				/*if (RDEVICE.dwTimeGlobal % 100 < 10)
				{
					Msg("CKinematics::CalculateBones, object canBeOptimized and dist > IK_CALC_DIST * dist_k, dist %.2f, update_rate_k %.2f, ssa %.4f", dist / dist_k, update_rate_k, ssa);
				}*/
			}
		}
	}
#endif

	xrCriticalSectionGuard g(UCalc_Mutex);
	OnCalculateBones();
	if (!bForceExact && (RDEVICE.dwTimeGlobal < (UCalc_Time + UCalc_Interval * update_rate_k))) return; // early out for "slow" update
	if (Update_Visibility) Visibility_Update();

	_DBG_SINGLE_USE_MARKER;
	// here we have either:
	//	1:	timeout elapsed
	//	2:	exact computation required
	UCalc_Time = RDEVICE.dwTimeGlobal;

	// exact computation
	// Calculate bones
#ifdef DEBUG
	RDEVICE.Statistic->Animation.Begin();
#endif

	Bone_Calculate(bones->at(iRoot), &Fidentity);
#ifdef DEBUG
	check_kinematics				(this, dbg_name.c_str() );
	RDEVICE.Statistic->Animation.End	();
#endif
	VERIFY(LL_GetBonesVisible()!=0);
	// Calculate BOXes/Spheres if needed
	UCalc_Visibox++;
	if (UCalc_Visibox >= psSkeletonUpdate)
	{
		// mark
		UCalc_Visibox = -(::Random.randI(psSkeletonUpdate - 1));
		UCalc_ThisFrame = true;
		CalculateBBox();
#ifdef DEBUG
		// Validate
		VERIFY3(_valid(vis.box.min) && _valid(vis.box.max), "Invalid bones-xform in model", dbg_name.c_str());
		if (vis.sphere.R > 1000.f)
		{
			for (u16 ii = 0; ii < LL_BoneCount(); ++ii) {
				Fmatrix tr;
				tr = LL_GetTransform(ii);
				Log("bone ", LL_BoneName_dbg(ii));
				Log("bone_matrix", tr);
			}
			Log("end-------");
		}
		VERIFY3(vis.sphere.R < 1000.f, "Invalid bones-xform in model", dbg_name.c_str());
#endif
	}
	else
		UCalc_ThisFrame = false;
	//
	if (Update_Callback) Update_Callback(this);
}

#ifdef DEBUG
void check_kinematics(CKinematics* _k, LPCSTR s)
{
	CKinematics* K = _k;
	Fmatrix& MrootBone = K->LL_GetBoneInstance(K->LL_GetBoneRoot()).mTransform;
	if (MrootBone.c.y > 10000)
	{
		Msg("all bones transform:--------[%s]", s);

		for (u16 ii = 0; ii < K->LL_BoneCount(); ++ii) {
			Fmatrix tr;

			tr = K->LL_GetTransform(ii);
			Log("bone ", K->LL_BoneName_dbg(ii));
			Log("bone_matrix", tr);
		}
		Log("end-------");
		VERIFY3(0, "check_kinematics failed for ", s);
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
	R_ASSERT(id < LL_BoneCount());
	xrCriticalSectionGuard guard(&UCalc_Mutex);
	CBoneInstance bi = LL_GetBoneInstance(id);
	BoneChain_Calculate(&LL_GetData(id), bi, mask_channel, ignore_callbacks);
#ifndef MASTER_GOLD
	R_ASSERT(_valid(bi.mTransform));
#endif
	pos.set(bi.mTransform);
}

void CKinematics::Bone_Calculate(CBoneData* bd, Fmatrix* parent)
{
	u16 SelfID = bd->GetSelfID();
	CBoneInstance& BONE_INST = LL_GetBoneInstance(SelfID);
	CLBone(bd, BONE_INST, parent, u8(-1));
	// Calculate children
	for (CBoneData* bd : bd->children)
		Bone_Calculate(bd, &BONE_INST.mTransform);
}

void CKinematics::BoneChain_Calculate(const CBoneData* bd, CBoneInstance& bi, u8 mask_channel, bool ignore_callbacks)
{
	u16 SelfID = bd->GetSelfID();
	//CBlendInstance& BLEND_INST	= LL_GetBlendInstance(SelfID);
	//CBlendInstance::BlendSVec &Blend = BLEND_INST.blend_vector();
	//ignore callbacks
	BoneCallback bc = bi.callback();
	BOOL ow = bi.callback_overwrite();
	if (ignore_callbacks)
	{
		bi.set_callback(bi.callback_type(), 0, bi.callback_param(), 0);
	}
	if (SelfID == LL_GetBoneRoot())
	{
		CLBone(bd, bi, &Fidentity, mask_channel);
		//restore callback	
		bi.set_callback(bi.callback_type(), bc, bi.callback_param(), ow);
		return;
	}
	u16 ParentID = bd->GetParentID();
	R_ASSERT(ParentID != BI_NONE);
	CBoneData* ParrentDT = &LL_GetData(ParentID);
	CBoneInstance parrent_bi = LL_GetBoneInstance(ParentID);
	BoneChain_Calculate(ParrentDT, parrent_bi, mask_channel, ignore_callbacks);
	CLBone(bd, bi, &parrent_bi.mTransform, mask_channel);
	//restore callback
	bi.set_callback(bi.callback_type(), bc, bi.callback_param(), ow);
}
