#pragma once
#include "svp_lens_detect_math.h"
#include "../../Include/xrRender/Kinematics.h"

// engine wrapper over the pure detection math, candidate selection is done engine-side
// candidates already selected, all_verts the whole visual for the stage-E tube march
bool svp_lens_fit(const std::vector<svp_v3>& candidates, const svp_v3& axis_seed, int source,
                  const std::vector<svp_v3>& all_verts, SLensDetection& out);
