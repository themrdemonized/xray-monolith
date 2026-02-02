#version 450
// Sky fragment shader - samples cubemap and blends two weather textures
// Hardware depth test (LESS_OR_EQUAL) ensures sky only draws where depth == 1.0

layout(location = 0) in vec3 v_TexCoord;
layout(location = 0) out vec4 o_Color;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 skyColorWeight; // .rgb = sky_color tint, .a = weather blend weight
} pc;

layout(set = 1, binding = 0) uniform samplerCube s_Sky0; // Current weather sky
layout(set = 1, binding = 1) uniform samplerCube s_Sky1; // Next weather sky

void main()
{
    // Sample both weather cubemaps and blend by weight
    vec3 sky0 = texture(s_Sky0, v_TexCoord).rgb;
    vec3 sky1 = texture(s_Sky1, v_TexCoord).rgb;
    vec3 sky = mix(sky0, sky1, pc.skyColorWeight.a);

    // Apply environment sky_color tint (brightness/time-of-day modulation)
    sky *= pc.skyColorWeight.rgb;

    o_Color = vec4(sky, 1.0);
}
