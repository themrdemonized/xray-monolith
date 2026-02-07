#version 450

// ============================================================================
// gbuffer.vert - G-Buffer Vertex Shader
// ============================================================================
//
// Transforms geometry vertices and prepares data for deferred shading.
//
// Vertex format (stride 32 - level static):
//   Position (FLOAT3)    @ offset 0
//   Normal   (D3DCOLOR)  @ offset 12   [B8G8R8A8_UNORM: (Nz,Ny,Nx) in 0..1]
//   TexCoord (SHORT4)    @ offset 24   [R16G16_SSCALED: raw short values]
//
// Vertex format (stride 36+ - skinned):
//   Position (FLOAT3)    @ offset 0    (from FLOAT4, .w ignored by format)
//   Normal   (D3DCOLOR)  @ offset 16   [B8G8R8A8_UNORM: (Nz,Ny,Nx) in 0..1]
//   TexCoord (FLOAT2)    @ offset 28   [R32G32_SFLOAT: native floats]
//
// ============================================================================

// Vertex inputs
layout(location = 0) in vec3 a_Position;  // Local/world-space position
layout(location = 1) in vec3 a_Normal;    // Packed normal (B8G8R8A8 → Nz,Ny,Nx in [0,1])
layout(location = 2) in vec2 a_TexCoord;  // Texture coordinates (raw)

// Outputs to fragment shader
layout(location = 0) out vec3 v_PositionEye;  // Eye-space position
layout(location = 1) out vec3 v_NormalEye;    // Eye-space normal
layout(location = 2) out vec2 v_TexCoord;     // Texture coordinates (scaled)

// Push constants (transformation matrices + UV scale)
layout(push_constant) uniform PushConstants
{
    mat4  u_Model;       // offset 0:   Model matrix (local → world)
    mat4  u_View;        // offset 64:  View matrix (world → eye)
    mat4  u_Projection;  // offset 128: Projection matrix (eye → clip)
    float u_UVScale;     // offset 192: UV multiplier (1/1024 for SHORT2, 1.0 for FLOAT2)
} pc;

void main()
{
    // 1. Transform position: local → world → eye → clip
    vec4 posWorld = pc.u_Model * vec4(a_Position, 1.0);
    vec4 posEye   = pc.u_View * posWorld;
    v_PositionEye = posEye.xyz;
    gl_Position   = pc.u_Projection * posEye;

    // 2. Unpack normal from D3DCOLOR (B8G8R8A8_UNORM)
    //    Hardware gives us (Nz, Ny, Nx) in [0,1] range.
    //    Swizzle .zyx → (Nx, Ny, Nz), then remap [0,1] → [-1,1].
    vec3 normal = a_Normal.xyz * 2.0 - 1.0;

    // 3. Transform normal to eye space (assumes uniform scale)
    mat3 normalMatrix = mat3(pc.u_View * pc.u_Model);
    v_NormalEye = normalize(normalMatrix * normal);

    // 4. Scale texture coordinates
    //    stride 32 (level static): UVs are SHORT2 SSCALED (raw int values), need *1/1024
    //    stride 36+ (skinned):     UVs are native FLOAT2, u_UVScale = 1.0
    v_TexCoord = a_TexCoord * pc.u_UVScale;
}
