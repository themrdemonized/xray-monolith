#include <math.h>
#include <vector>
#include <algorithm>
#include "svp_lens_detect_math.h"

// stage-E tube-march tuning, design-intent per the doc A3.2 spec
static const float TUBE_SLAB_M         = 0.005f;  // 5 mm march step
static const float TUBE_RADIUS_SCALE   = 3.0f;    // cylinder radius = this x eye radius
static const int   TUBE_EMPTY_RUN      = 3;       // consecutive empty slabs terminate the march
static const int   TUBE_SLAB_MIN_PTS   = 3;       // a slab counts as dense at this many verts
static const float TUBE_MIN_LENGTH     = 0.05f;   // reject tubes shorter than this
static const float TUBE_MAX_LENGTH     = 0.45f;   // reject tubes longer than this, survey longest optic ~0.39m
static const float TUBE_RING_INNER_PCT = 0.15f;   // low-percentile mouth radial = clear aperture
static const float TUBE_CONCENTRIC_TOL = 0.5f;    // mouth centroid lateral offset / eye radius
static const float TUBE_MAX_MID_GAP_M  = 0.05f;   // reject a long gap before the mouth

// local vector helpers, model.js:15-31
static inline svp_v3 vsub(const svp_v3& a, const svp_v3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline svp_v3 vscale(const svp_v3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static inline float  vdot(const svp_v3& a, const svp_v3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline svp_v3 vcross(const svp_v3& a, const svp_v3& b)
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static inline float vlen(const svp_v3& a) { return sqrtf(vdot(a, a)); }
static inline svp_v3 vnorm(const svp_v3& a)
{
	float L = vlen(a);
	if (L < 1e-12f) return { 0.f, 0.f, 0.f };
	return { a.x / L, a.y / L, a.z / L };
}

// symmetric 3x3 Jacobi eigen, optics-math.js:131-162, double for the convergence gates
struct SEig3 { double values[3]; double vectors[3][3]; };
static SEig3 jacobi_eigen3(const double m[3][3])
{
	double a[3][3] = { { m[0][0], m[0][1], m[0][2] }, { m[1][0], m[1][1], m[1][2] }, { m[2][0], m[2][1], m[2][2] } };
	double v[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	static const int pqs[3][2] = { { 0, 1 }, { 0, 2 }, { 1, 2 } };
	for (int sweep = 0; sweep < 32; ++sweep)
	{
		if (fabs(a[0][1]) + fabs(a[0][2]) + fabs(a[1][2]) < 1e-18) break;
		for (int e = 0; e < 3; ++e)
		{
			int p = pqs[e][0], q = pqs[e][1];
			if (fabs(a[p][q]) < 1e-20) continue;
			double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
			double t = (theta >= 0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
			double c = 1.0 / sqrt(t * t + 1.0), s = t * c;
			for (int k = 0; k < 3; ++k) { double akp = a[k][p], akq = a[k][q]; a[k][p] = c * akp - s * akq; a[k][q] = s * akp + c * akq; }
			for (int k = 0; k < 3; ++k) { double apk = a[p][k], aqk = a[q][k]; a[p][k] = c * apk - s * aqk; a[q][k] = s * apk + c * aqk; }
			for (int k = 0; k < 3; ++k) { double vkp = v[k][p], vkq = v[k][q]; v[k][p] = c * vkp - s * vkq; v[k][q] = s * vkp + c * vkq; }
		}
	}
	SEig3 out;
	out.values[0] = a[0][0]; out.values[1] = a[1][1]; out.values[2] = a[2][2];
	for (int k = 0; k < 3; ++k) { out.vectors[k][0] = v[0][k]; out.vectors[k][1] = v[1][k]; out.vectors[k][2] = v[2][k]; }
	return out;
}

// PCA disc fit, optics-math.js:166-196
SDiscFit svp_fit_disc(const svp_v3* pts, int n)
{
	SDiscFit r;
	r.center = { 0, 0, 0 }; r.normal = { 0, 0, 1 }; r.radius = 0.f; r.rms = 0.f; r.valid = false;
	if (!pts || n < FIT_MIN_POINTS) return r;

	double cx = 0, cy = 0, cz = 0;
	for (int i = 0; i < n; ++i) { cx += pts[i].x; cy += pts[i].y; cz += pts[i].z; }
	cx /= n; cy /= n; cz /= n;

	double xx = 0, xy = 0, xz = 0, yy = 0, yz = 0, zz = 0;
	for (int i = 0; i < n; ++i)
	{
		double dx = pts[i].x - cx, dy = pts[i].y - cy, dz = pts[i].z - cz;
		xx += dx * dx; xy += dx * dy; xz += dx * dz; yy += dy * dy; yz += dy * dz; zz += dz * dz;
	}
	double m[3][3] = { { xx, xy, xz }, { xy, yy, yz }, { xz, yz, zz } };
	SEig3 eig = jacobi_eigen3(m);

	// normal is the smallest-eigenvalue direction, signed toward +Z
	int mi = 0;
	if (eig.values[1] < eig.values[mi]) mi = 1;
	if (eig.values[2] < eig.values[mi]) mi = 2;
	double nx = eig.vectors[mi][0], ny = eig.vectors[mi][1], nz = eig.vectors[mi][2];
	double nl = sqrt(nx * nx + ny * ny + nz * nz);
	if (nl < 1e-12) { nx = 0; ny = 0; nz = 1; }
	else { nx /= nl; ny /= nl; nz /= nl; }
	if (nz < 0) { nx = -nx; ny = -ny; nz = -nz; }

	// radial spread off the normal and the out-of-plane residual
	std::vector<double> radial; radial.reserve(n);
	double outSq = 0;
	for (int i = 0; i < n; ++i)
	{
		double dx = pts[i].x - cx, dy = pts[i].y - cy, dz = pts[i].z - cz;
		double out = dx * nx + dy * ny + dz * nz;
		outSq += out * out;
		double rx = dx - nx * out, ry = dy - ny * out, rz = dz - nz * out;
		radial.push_back(sqrt(rx * rx + ry * ry + rz * rz));
	}
	std::sort(radial.begin(), radial.end());
	int qi = (int)floor(n * 0.9);
	if (qi > n - 1) qi = n - 1;
	double radius = radial[qi];
	double rms = sqrt(outSq / n);
	if (radius > FIT_MAX_RADIUS || rms > FIT_MAX_RMS_RATIO * radius) return r;

	r.center = { (float)cx, (float)cy, (float)cz };
	r.normal = { (float)nx, (float)ny, (float)nz };
	r.radius = (float)radius;
	r.rms = (float)rms;
	r.valid = true;
	return r;
}

// 1D clustering along an axis, optics-math.js:200-217, clusters come back rear -> front
void svp_split_along_axis(const svp_v3* pts, int n, const svp_v3& axis,
                          std::vector<std::vector<svp_v3>>& clusters)
{
	clusters.clear();
	if (!pts || n <= 0) return;
	svp_v3 dir = vnorm(axis);

	std::vector<std::pair<float, int>> proj; proj.reserve(n);
	for (int i = 0; i < n; ++i) proj.push_back({ vdot(pts[i], dir), i });
	std::sort(proj.begin(), proj.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first < b.first; });

	std::vector<float> gaps; gaps.reserve(n > 1 ? n - 1 : 0);
	for (int i = 1; i < n; ++i) gaps.push_back(proj[i].first - proj[i - 1].first);
	std::vector<float> sorted = gaps;
	std::sort(sorted.begin(), sorted.end());
	float median = sorted.empty() ? 0.f : sorted[sorted.size() / 2];
	float threshold = std::max(SPLIT_MIN_GAP, 2.f * median);

	std::vector<svp_v3> cur;
	cur.push_back(pts[proj[0].second]);
	for (int i = 1; i < n; ++i)
	{
		if (proj[i].first - proj[i - 1].first > threshold) { clusters.push_back(cur); cur.clear(); }
		cur.push_back(pts[proj[i].second]);
	}
	clusters.push_back(cur);
}

// bone axis is a usable split seed only when it roughly agrees with +Z, optics-math.js:220-226
svp_v3 svp_axis_seed_for(const svp_v3& model_axis_k)
{
	svp_v3 k = vnorm(model_axis_k);
	float d = k.z;
	if (d >= AXIS_SEED_MAX_COS) return k;
	if (-d >= AXIS_SEED_MAX_COS) return vscale(k, -1.f);
	return { 0.f, 0.f, 1.f };
}

// split -> fit -> filter -> assign, optics-math.js:230-263 assignment half
SLensDetectResult svp_detect_lens_discs(const std::vector<svp_v3>& candidates,
                                        const svp_v3& axis_seed, int source)
{
	SLensDetectResult res;
	res.eyepiece.valid = false; res.objective.valid = false;
	res.source = source; res.vert_count = (int)candidates.size(); res.ok = false;
	if (candidates.size() < (size_t)FIT_MIN_POINTS) return res;

	std::vector<std::vector<svp_v3>> clusters;
	svp_split_along_axis(candidates.data(), (int)candidates.size(), axis_seed, clusters);

	std::vector<SDiscFit> discs;
	for (size_t i = 0; i < clusters.size(); ++i)
	{
		SDiscFit d = svp_fit_disc(clusters[i].data(), (int)clusters[i].size());
		if (d.valid) discs.push_back(d);
	}
	if (discs.empty()) return res;

	res.eyepiece = discs[0];
	for (int i = (int)discs.size() - 1; i >= 1; --i)
	{
		svp_v3 delta = vsub(discs[i].center, res.eyepiece.center);
		if (vdot(delta, res.eyepiece.normal) > OBJECTIVE_MIN_FORWARD) { res.objective = discs[i]; break; }
	}
	res.ok = true;
	return res;
}

// orthonormal frame from the axis alone, model.js:41-48
struct SLensFrame { svp_v3 i, j, k; };
static SLensFrame bone_local_frame(const svp_v3& kAxis)
{
	svp_v3 k = vnorm(kAxis);
	svp_v3 worldUp = { 0.f, 1.f, 0.f };
	svp_v3 tmp = (fabsf(vdot(k, worldUp)) > 0.99f) ? svp_v3{ 1.f, 0.f, 0.f } : svp_v3{ 0.f, 1.f, 0.f };
	svp_v3 i = vnorm(vcross(tmp, k));
	svp_v3 j = vcross(k, i);
	return { i, j, k };
}

// objective center in the eyepiece local frame divided by eye radius, optics-math.js:93-97
svp_v4 svp_lens_offset_from_centers(const svp_v3& axis_dir, const svp_v3& eye_c,
                                    float eye_r, const svp_v3& obj_c, float obj_r)
{
	SLensFrame f = bone_local_frame(axis_dir);
	svp_v3 delta = vsub(obj_c, eye_c);
	float r = (eye_r != 0.f) ? eye_r : 1e-9f;
	svp_v4 o;
	o.x = vdot(delta, f.i) / r;
	o.y = vdot(delta, f.j) / r;
	o.z = vdot(delta, f.k) / r;
	o.w = obj_r / r;
	return o;
}

// stage-E tube march, doc A3.2, recovers the objective from the housing when Stage D found only the ocular
bool svp_tube_march_objective(const std::vector<svp_v3>& all_verts,
                              const svp_v3& eye_center, const svp_v3& eye_normal,
                              float eye_radius, SDiscFit& obj_out)
{
	obj_out.valid = false;
	if (all_verts.empty() || eye_radius <= 0.f) return false;
	svp_v3 axis = vnorm(eye_normal);
	float cyl_r = TUBE_RADIUS_SCALE * eye_radius;

	// keep forward verts inside the cylinder with their axial and radial distances
	struct SVertTR { float t; float radial; svp_v3 p; };
	std::vector<SVertTR> col;
	float max_t = 0.f;
	for (size_t vi = 0; vi < all_verts.size(); ++vi)
	{
		svp_v3 d = vsub(all_verts[vi], eye_center);
		float t = vdot(d, axis);
		if (t < 0.f) continue;
		svp_v3 perp = vsub(d, vscale(axis, t));
		float rad = vlen(perp);
		if (rad > cyl_r) continue;
		col.push_back({ t, rad, all_verts[vi] });
		if (t > max_t) max_t = t;
	}
	if (col.empty()) return false;

	int nslabs = (int)floor(max_t / TUBE_SLAB_M) + 1;
	if (nslabs <= 0) return false;
	std::vector<int> slab_count(nslabs, 0);
	for (size_t i = 0; i < col.size(); ++i)
	{
		int si = (int)floor(col[i].t / TUBE_SLAB_M);
		if (si >= 0 && si < nslabs) slab_count[si]++;
	}

	// march forward to the last dense slab, terminate at K consecutive empties
	int empty_run = 0, mouth_slab = -1, cur_gap = 0, max_mid_gap = 0;
	bool hit_cap = false;
	for (int si = 0; si < nslabs; ++si)
	{
		if (slab_count[si] >= TUBE_SLAB_MIN_PTS)
		{
			mouth_slab = si;
			if (cur_gap > max_mid_gap) max_mid_gap = cur_gap;
			cur_gap = 0; empty_run = 0;
		}
		else
		{
			empty_run++; cur_gap++;
			if (empty_run >= TUBE_EMPTY_RUN) break;
		}
		if ((float)si * TUBE_SLAB_M > TUBE_MAX_LENGTH) { hit_cap = true; break; }
	}
	if (mouth_slab < 0) return false;
	// a solid barrel stays dense out to the cap without the tube's open mouth, reject rather than latch the muzzle
	if (hit_cap) return false;

	float mouth_t = (float)mouth_slab * TUBE_SLAB_M;
	if (mouth_t < TUBE_MIN_LENGTH || mouth_t > TUBE_MAX_LENGTH) return false;
	if ((float)max_mid_gap * TUBE_SLAB_M > TUBE_MAX_MID_GAP_M) return false;

	// gather the terminal-slab ring
	std::vector<svp_v3> mouth_pts;
	std::vector<float> mouth_rad;
	svp_v3 ring_sum = { 0, 0, 0 };
	int ring_n = 0;
	for (size_t i = 0; i < col.size(); ++i)
	{
		if ((int)floor(col[i].t / TUBE_SLAB_M) == mouth_slab)
		{
			mouth_pts.push_back(col[i].p);
			mouth_rad.push_back(col[i].radial);
			ring_sum.x += col[i].p.x; ring_sum.y += col[i].p.y; ring_sum.z += col[i].p.z;
			ring_n++;
		}
	}
	if (ring_n <= 0) return false;

	// concentricity gate on the mouth centroid
	svp_v3 center = { ring_sum.x / ring_n, ring_sum.y / ring_n, ring_sum.z / ring_n };
	svp_v3 cdelta = vsub(center, eye_center);
	float ct = vdot(cdelta, axis);
	svp_v3 cperp = vsub(cdelta, vscale(axis, ct));
	if (vlen(cperp) > TUBE_CONCENTRIC_TOL * eye_radius) return false;

	// route a, a clean disc fit on the mouth verts
	if ((int)mouth_pts.size() >= FIT_MIN_POINTS)
	{
		SDiscFit d = svp_fit_disc(mouth_pts.data(), (int)mouth_pts.size());
		if (d.valid) { obj_out = d; return true; }
	}

	// route b, inner-radius aperture of the mouth ring
	std::sort(mouth_rad.begin(), mouth_rad.end());
	int qi = (int)floor(mouth_rad.size() * TUBE_RING_INNER_PCT);
	if (qi > (int)mouth_rad.size() - 1) qi = (int)mouth_rad.size() - 1;
	if (qi < 0) qi = 0;
	obj_out.center = center;
	obj_out.normal = axis;
	obj_out.radius = mouth_rad[qi];
	obj_out.rms = 0.f;
	obj_out.valid = true;
	return true;
}

// sv98 fixture, doc 3.1 and Addendum 2, synthesizes a 379-point ocular disc and checks the fit
bool svp_lens_detect_selftest()
{
	const svp_v3 center = { 0.000517f, 0.09351f, 0.016879f };
	const float target_r = 0.014699f;
	const float target_rms = 0.000528f;
	const int   N = 379;
	const float golden = 2.399963f;

	// pick the outer radius so the 90th-percentile radial lands on the measured ocular radius
	float qf = (floorf(N * 0.9f) + 0.5f) / (float)N;
	float Rmax = target_r / sqrtf(qf);
	std::vector<svp_v3> pts; pts.reserve(N);
	for (int i = 0; i < N; ++i)
	{
		float ri = Rmax * sqrtf((i + 0.5f) / (float)N);
		float th = i * golden;
		float dz = (i & 1) ? -target_rms : target_rms;
		pts.push_back({ center.x + ri * cosf(th), center.y + ri * sinf(th), center.z + dz });
	}

	svp_v3 seed = svp_axis_seed_for(svp_v3{ 0.f, 0.f, 1.f });
	SLensDetectResult res = svp_detect_lens_discs(pts, seed, 0);
	if (!res.ok || !res.eyepiece.valid || res.objective.valid) return false;

	const SDiscFit& e = res.eyepiece;
	if (fabsf(e.center.x - center.x) > 1e-3f) return false;
	if (fabsf(e.center.y - center.y) > 1e-3f) return false;
	if (fabsf(e.center.z - center.z) > 1e-3f) return false;
	if (fabsf(e.radius - target_r) > 1e-3f) return false;
	if (fabsf(e.rms - target_rms) > 1e-3f) return false;
	if (e.normal.z < 0.99f) return false;

	// export identities, mm from the objective radius and z from the forward distance
	if (fabsf(svp_mm_suggestion(0.017675f) - 35.35f) > 0.05f) return false;
	float eye_r = 0.014699f, obj_r = 0.017675f, fwd = 14.639f * eye_r;
	svp_v4 off = svp_lens_offset_from_centers(svp_v3{ 0, 0, 1 }, svp_v3{ 0, 0, 0 }, eye_r, svp_v3{ 0, 0, fwd }, obj_r);
	if (fabsf(off.z - 14.639f) > 0.01f) return false;
	if (fabsf(off.w - 1.2025f) > 0.01f) return false;
	return true;
}
