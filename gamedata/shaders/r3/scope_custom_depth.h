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

    return nvg_blur
        ? hpos.z
        : NO_BLUR;
}