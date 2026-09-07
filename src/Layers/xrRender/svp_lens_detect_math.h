#pragma once
#include <vector>

// pure lens-disc detection ported from the 3DB viewer optics-math.js
// engine-free split -> fit -> assign core plus the stage-E tube march

// svp_v3/svp_v4 are layout-compatible with Fvector/Fvector4 so WO-B reinterprets its arrays
struct svp_v3 { float x, y, z; };
struct svp_v4 { float x, y, z, w; };

// fitDisc output, valid == false means the cluster was rejected
struct SDiscFit {
	svp_v3 center;
	svp_v3 normal;   // signed toward +Z
	float  radius;   // 90th-percentile in-plane radius in metres
	float  rms;      // out-of-plane RMS residual in metres
	bool   valid;
};

// one entry per skeleton bone the detector needs, filled by WO-B
struct SBoneInfo {
	svp_v3 model_pos;     // bind-pose model-space bone origin
	svp_v3 model_axis_k;  // bind-pose forward column normalized
	bool   is_lens;       // name matched /lens|glass/i by the caller
};

struct SLensDetectResult {
	SDiscFit eyepiece;    // rear-most valid disc
	SDiscFit objective;   // front-most valid disc past the forward gate, valid == false if none
	int      source;      // 0 lens_verts, 1 near_bone
	int      vert_count;  // candidate count actually fit
	bool     ok;          // false means no fittable lens, caller keeps authored/live
};

// detection constants, verbatim from optics-math.js:121-128
static const int   FIT_MIN_POINTS        = 12;
static const float FIT_MAX_RADIUS        = 0.08f;
static const float FIT_MAX_RMS_RATIO     = 0.35f;
static const float SPLIT_MIN_GAP         = 0.015f;
static const float NEAR_BONE_RADIUS      = 0.05f;      // used by WO-B for the fallback selection
static const float AXIS_SEED_MAX_COS     = 0.8660254f; // cos 30
static const float OBJECTIVE_MIN_FORWARD = 0.03f;

// core stages, free functions with no state
SDiscFit svp_fit_disc(const svp_v3* pts, int n);
void     svp_split_along_axis(const svp_v3* pts, int n, const svp_v3& axis,
                              std::vector<std::vector<svp_v3>>& clusters_rear_to_front);
svp_v3   svp_axis_seed_for(const svp_v3& model_axis_k);

// full pipeline on the already-selected candidate cloud plus the lens seed axis
// positions in bind-pose model space in metres
SLensDetectResult svp_detect_lens_discs(const std::vector<svp_v3>& candidates,
                                        const svp_v3& axis_seed, int source);

// stage-E tube march recovers a measured objective from housing verts when the
// pipeline returned eyepiece-only, all_verts is the whole visual or scope subtree
bool svp_tube_march_objective(const std::vector<svp_v3>& all_verts,
                              const svp_v3& eye_center, const svp_v3& eye_normal,
                              float eye_radius, SDiscFit& obj_out);

// export identities, offset in eyepiece-radius units and mm from the fitted radius
svp_v4 svp_lens_offset_from_centers(const svp_v3& axis_dir, const svp_v3& eye_c,
                                    float eye_r, const svp_v3& obj_c, float obj_r);
inline float svp_mm_suggestion(float obj_radius_m) { return obj_radius_m * 2.f * 1000.f; }

// asserts the sv98 fixture, returns true on pass, gated caller lives in WO-D
bool svp_lens_detect_selftest();
