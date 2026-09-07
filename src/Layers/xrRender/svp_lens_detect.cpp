#include "stdafx.h"

#include "SkeletonCustom.h"
#include "SkeletonX.h"
#include "fhierrarhyvisual.h"
#include "../../xrEngine/bone.h"
#include "svp_lens_detect.h"

// measured optical lens discs from the weapon hud mesh, ported from the 3DB viewer detection
// runs once per unique visual and caches, inert until IKinematics::GetLensDetection is queried

// pure pipeline stage, compiles in every renderer, only ever reached from the DX11 query below
bool svp_lens_fit(const std::vector<svp_v3>& candidates, const svp_v3& axis_seed, int source,
                  const std::vector<svp_v3>& all_verts, SLensDetection& out)
{
	out.ok = false;
	out.has_objective = false;
	out.source = source;
	out.mm = 0.f;
	out.offset.set(0.f, 0.f, 0.f, 0.f);
	out.eye_center.set(0.f, 0.f, 0.f);
	out.eye_normal.set(0.f, 0.f, 1.f);
	out.eye_radius = 0.f;
	out.obj_center.set(0.f, 0.f, 0.f);
	out.obj_radius = 0.f;

	SLensDetectResult r = svp_detect_lens_discs(candidates, axis_seed, source);
	if (!r.ok || !r.eyepiece.valid)
		return false;

	out.eye_center.set(r.eyepiece.center.x, r.eyepiece.center.y, r.eyepiece.center.z);
	out.eye_normal.set(r.eyepiece.normal.x, r.eyepiece.normal.y, r.eyepiece.normal.z);
	out.eye_radius = r.eyepiece.radius;
	out.ok = true;

	SDiscFit obj = r.objective;
	int objsrc = source;
	if (!obj.valid)
	{
		// stage E recovers a measured objective from the housing tube mouth
		svp_v3 ec = { out.eye_center.x, out.eye_center.y, out.eye_center.z };
		svp_v3 en = { out.eye_normal.x, out.eye_normal.y, out.eye_normal.z };
		SDiscFit m;
		if (svp_tube_march_objective(all_verts, ec, en, out.eye_radius, m))
		{
			obj = m;
			objsrc = 2;
		}
	}

	if (obj.valid)
	{
		out.has_objective = true;
		out.obj_center.set(obj.center.x, obj.center.y, obj.center.z);
		out.obj_radius = obj.radius;
		svp_v3 axis = { out.eye_normal.x, out.eye_normal.y, out.eye_normal.z };
		svp_v3 ec = { out.eye_center.x, out.eye_center.y, out.eye_center.z };
		svp_v4 off = svp_lens_offset_from_centers(axis, ec, out.eye_radius, obj.center, obj.radius);
		out.offset.set(off.x, off.y, off.z, off.w);
		out.mm = svp_mm_suggestion(obj.radius);
		out.source = objsrc;
	}
	return out.ok;
}

#if defined(USE_DX10) || defined(USE_DX11)

// lens naming the modeler committed, case-insensitive substring of lens or glass
static bool svp_name_is_lens(LPCSTR n)
{
	if (!n)
		return false;
	string256 low;
	xr_strcpy(low, n);
	xr_strlwr(low);
	return (strstr(low, "lens") != nullptr) || (strstr(low, "glass") != nullptr);
}

static inline svp_v3 svp_from(const Fvector& v) { return { v.x, v.y, v.z }; }

// append each dedup vertex as its bind-pose model position with the global dominant bone id
// the stored bone id is a global skeleton id, no palette remap (skinning indexes LL_GetTransform_R by it)
void CSkeletonX::SVP_GatherVerts(xr_vector<Fvector>& positions, xr_vector<u16>& bones)
{
	if (*Vertices1W)
	{
		const vertBoned1W* v = *Vertices1W;
		for (u32 i = 0, n = Vertices1W.size(); i < n; ++i)
		{
			positions.push_back(v[i].P);
			bones.push_back((u16)v[i].matrix);
		}
	}
	else if (*Vertices2W)
	{
		const vertBoned2W* v = *Vertices2W;
		for (u32 i = 0, n = Vertices2W.size(); i < n; ++i)
		{
			positions.push_back(v[i].P);
			bones.push_back((v[i].w > 0.5f) ? v[i].matrix1 : v[i].matrix0);
		}
	}
	else if (*Vertices3W)
	{
		const vertBoned3W* v = *Vertices3W;
		for (u32 i = 0, n = Vertices3W.size(); i < n; ++i)
		{
			positions.push_back(v[i].P);
			const float w2 = 1.f - v[i].w[0] - v[i].w[1];
			u16 b = v[i].m[0];
			float best = v[i].w[0];
			if (v[i].w[1] > best) { best = v[i].w[1]; b = v[i].m[1]; }
			if (w2 > best) { b = v[i].m[2]; }
			bones.push_back(b);
		}
	}
	else if (*Vertices4W)
	{
		const vertBoned4W* v = *Vertices4W;
		for (u32 i = 0, n = Vertices4W.size(); i < n; ++i)
		{
			positions.push_back(v[i].P);
			const float w3 = 1.f - v[i].w[0] - v[i].w[1] - v[i].w[2];
			u16 b = v[i].m[0];
			float best = v[i].w[0];
			if (v[i].w[1] > best) { best = v[i].w[1]; b = v[i].m[1]; }
			if (v[i].w[2] > best) { best = v[i].w[2]; b = v[i].m[2]; }
			if (w3 > best) { b = v[i].m[3]; }
			bones.push_back(b);
		}
	}
}

#endif // USE_DX10 || USE_DX11

bool CKinematics::GetLensDetection(SLensDetection& out)
{
	out.ok = false;
	out.has_objective = false;
	out.source = 0;
	out.mm = 0.f;
	out.offset.set(0.f, 0.f, 0.f, 0.f);
	out.eye_center.set(0.f, 0.f, 0.f);
	out.eye_normal.set(0.f, 0.f, 1.f);
	out.eye_radius = 0.f;
	out.obj_center.set(0.f, 0.f, 0.f);
	out.obj_radius = 0.f;

#if defined(USE_DX10) || defined(USE_DX11)
	// process-lifetime cache keyed by visual path, negatives cached too, deterministic per mesh
	static xr_map<shared_str, SLensDetection> s_cache;
	static xrCriticalSection s_cache_cs;
	shared_str key = getDebugName();
	{
		xrCriticalSectionGuard g(&s_cache_cs);
		xr_map<shared_str, SLensDetection>::iterator it = s_cache.find(key);
		if (it != s_cache.end())
		{
			out = it->second;
			return out.ok;
		}
	}

	SLensDetection res = out;

	// bind-pose bone transforms, model space and pose independent
	xr_vector<Fmatrix> binds;
	LL_GetBindTransform(binds);
	const u16 bc = LL_BoneCount();

	// lens bones by name, the first match seeds the split axis and the near-bone fallback
	xr_vector<u16> lens_ids;
	int seed = -1;
	for (u16 b = 0; b < bc; ++b)
	{
		if (svp_name_is_lens(LL_GetData(b).name.c_str()))
		{
			lens_ids.push_back(b);
			if (seed < 0)
				seed = b;
		}
	}

	// every child mesh vertex with its global dominant bone, visible and hidden alike
	xr_vector<Fvector> pos;
	xr_vector<u16> bone;
	for (u32 i = 0; i < children.size(); ++i)
	{
		CSkeletonX* C = LL_GetChild(i);
		if (C)
			C->SVP_GatherVerts(pos, bone);
	}
	for (u32 i = 0; i < children_invisible.size(); ++i)
	{
		CSkeletonX* C = fast_dynamic_cast<CSkeletonX*>(children_invisible[i]);
		if (C)
			C->SVP_GatherVerts(pos, bone);
	}

	if (seed >= 0 && !pos.empty())
	{
		// stage A, primary dominant-bone candidates then the near-bone fallback
		std::vector<svp_v3> cand;
		for (u32 i = 0; i < pos.size(); ++i)
		{
			for (u32 k = 0; k < lens_ids.size(); ++k)
			{
				if (bone[i] == lens_ids[k])
				{
					cand.push_back(svp_from(pos[i]));
					break;
				}
			}
		}

		int source = 0;
		if ((int)cand.size() < FIT_MIN_POINTS)
		{
			cand.clear();
			source = 1;
			const Fvector sp = binds[seed].c;
			for (u32 i = 0; i < pos.size(); ++i)
			{
				Fvector d;
				d.sub(pos[i], sp);
				if (d.magnitude() <= NEAR_BONE_RADIUS)
					cand.push_back(svp_from(pos[i]));
			}
		}

		if ((int)cand.size() >= FIT_MIN_POINTS)
		{
			svp_v3 axis_seed = svp_axis_seed_for(svp_from(binds[seed].k));
			std::vector<svp_v3> allv;
			allv.reserve(pos.size());
			for (u32 i = 0; i < pos.size(); ++i)
				allv.push_back(svp_from(pos[i]));
			svp_lens_fit(cand, axis_seed, source, allv, res);
		}
	}

	{
		xrCriticalSectionGuard g(&s_cache_cs);
		s_cache[key] = res;
	}
	out = res;
	return res.ok;
#else
	return false;
#endif
}
