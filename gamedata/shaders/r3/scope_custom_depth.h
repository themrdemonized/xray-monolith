/*
	=====================================================================
	Addon      : Shader 3D Scopes
	Link       : https://www.moddb.com/mods/stalker-anomaly/addons/shader-3d-scopes
	Authors    : LVutner, party_50

	All credit to original authors.
	=====================================================================
*/

#include "scope_3dss_common.h"

float scope_custom_depth(float4 hpos) {
    bool nvg_blur = SETTING(SETTINGS, ST_NVG_BLUR) && floor(shader_param_8.x) != 0 || m_hud_params.x == 0;

    float NO_BLUR = 100.0;

    // true pip under nvg stamps the disc as unwritten sky, the nightvision depth terms
    // otherwise read the 100m marker as mid range scenery and boost it into washout
    if (shader_scope_params.w < -1.5 && floor(shader_param_8.x) != 0)
        return 0.0;

    return nvg_blur
        ? hpos.z
        : NO_BLUR;
}

// true pip clips the far stamp to the glass disc so the housing keeps its
// real gbuffer depth for weapon dof, nvg and fake pip keep the whole mesh
float scope_custom_depth(float4 hpos, float2 tc0) {
    if (shader_scope_params.w < -1.5 && floor(shader_param_8.x) == 0)
        clip(1.0 - length((tc0 - 0.5) * 2.0));
    return scope_custom_depth(hpos);
}