#version 450
// Sky vertex shader - renders cubemap skybox using half-box geometry
// Position IS the cubemap lookup direction

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;       // View-projection matrix (rotation only, no translation)
    vec4 skyColorWeight; // sky_color.rgb + blend weight in .a
} pc;

layout(location = 0) out vec3 v_TexCoord;

void main()
{
    v_TexCoord = a_Position;
    vec4 pos = pc.viewProj * vec4(a_Position, 1.0);
    gl_Position = pos.xyww; // z=w trick: depth = 1.0 after perspective divide
}
