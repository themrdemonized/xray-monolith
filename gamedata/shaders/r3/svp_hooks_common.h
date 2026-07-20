// svp_hooks_common 20260715 thinhook
// shared SVP uniform + texture decls for the hook headers and monolith scopes, no shader_scope_params here
#ifndef SVP_HOOKS_COMMON_INCLUDED
#define SVP_HOOKS_COMMON_INCLUDED

Texture2D s_tonemap_svp;
Texture2D s_svp_rain; // objective-glass rain droplet atlas, x=Fade y=NormalY z=NormalX w=TimeOffset
#ifndef SVP_EXPOSURE_DECLARED
#define SVP_EXPOSURE_DECLARED
uniform float4 svp_exposure; // x = 0 off else 2^bias, y = twilight dim, zw = crescent swing side offset
#endif
#ifndef SVP_OPTICS_DECLARED
#define SVP_OPTICS_DECLARED
uniform float4 svp_optics; // x = 2*ocular_radius/eye_distance, y = true-scale parallax, z = dead lane
#endif
#ifndef SVP_CONTROL_DECLARED
#define SVP_CONTROL_DECLARED
uniform float4 svp_control; // engine intent, x/y/z = strip parallax shadow/chromatism/nvg blur under true PiP
#endif
#ifndef SVP_GLASS_DECLARED
#define SVP_GLASS_DECLARED
uniform float4 svp_glass; // engine glass tunables, x = reticle washout, y = field curvature, z = ACOG fiber sun mode
#endif
#ifndef SVP_ENV_DECLARED
#define SVP_ENV_DECLARED
uniform float4 svp_env; // engine glass environment, x = veiling glare, y = rain intensity
#endif
#ifndef SVP_GLASS2_DECLARED
#define SVP_GLASS2_DECLARED
uniform float4 svp_glass2; // engine glass optics 2, x = coating, y = heat mirage, w = flat-panel V-crop
#endif
#ifndef SVP_GLASS3_DECLARED
#define SVP_GLASS3_DECLARED
uniform float4 svp_glass3; // engine glass optics 3, x = sharpen amount, y = field-stop onset, z = sharpen radial falloff, w = sharpen inner crisp radius
#endif
#ifndef SVP_GLASS4_DECLARED
#define SVP_GLASS4_DECLARED
uniform float4 svp_glass4; // engine glass optics 4, x = nvg bleach roll-off, y = nvg auto-gain, w = shadow swing envelope
#endif

#endif
