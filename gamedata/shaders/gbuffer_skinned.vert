#version 450

// ============================================================================
// gbuffer_skinned.vert - G-Buffer Skinned Vertex Shader
// ============================================================================
//
// GPU skinning for skeletal meshes (1-4 bones per vertex).
// Bone matrices are provided via SSBO (set 2, binding 1).
// Skinning mode is selected via push constant u_SkinMode.
//
// Hardware vertex formats (from FSkinned.cpp):
//
// Stride 36 (1W): FLOAT4 P + D3DCOLOR N_I + D3DCOLOR T + D3DCOLOR B + FLOAT2 tc
//   N_I.a = bone_index * 3  (UBYTE4N: value/255 then *255/3 to decode)
//
// Stride 44 (2W): FLOAT4 P + D3DCOLOR N_w0 + D3DCOLOR T + D3DCOLOR B + FLOAT4 tc_i
//   N.a = weight0 (UBYTE4N)
//   T.a = 0 (unused), B.a = 0 (unused)
//   tc_i.xy = UV, tc_i.z = s16(bone_index0*3) as float, tc_i.w = s16(bone_index1*3) as float
//
// Stride 44 (3W): FLOAT4 P + D3DCOLOR N_w0 + D3DCOLOR T_w1 + D3DCOLOR B_i2 + FLOAT4 tc_i
//   N.a = weight0 (UBYTE4N), T.a = weight1 (UBYTE4N), B.a = bone_index2*3 (UBYTE)
//   tc_i.xy = UV, tc_i.z = s16(bone_index0*3) as float, tc_i.w = s16(bone_index1*3) as float
//
// Stride 40 (4W): FLOAT4 P + D3DCOLOR N_w0 + D3DCOLOR T_w1 + D3DCOLOR B_w2 + FLOAT2 tc + D3DCOLOR indices
//   N.a = weight0, T.a = weight1, B.a = weight2 (all UBYTE4N)
//   indices RGBA = 4 bone indices * 3 (UBYTE4N)
//
// ============================================================================

// Vertex inputs
layout(location = 0) in vec4 a_Position;    // FLOAT4: xyz = position, w = unused/pad
layout(location = 1) in vec4 a_Normal;      // D3DCOLOR (UBYTE4N): xyz = normal, a = bone_idx(1W) or weight0(2W+)
layout(location = 2) in vec4 a_TexCoordExt; // FLOAT2 (stride 36,40) or FLOAT4 (stride 44): xy=tc, zw=bone indices for 2W/3W
layout(location = 3) in vec4 a_Tangent;     // D3DCOLOR: xyz = tangent, a = weight1(3W/4W) or 0(1W/2W)
layout(location = 4) in vec4 a_Binormal;    // D3DCOLOR: xyz = binormal, a = bone_idx2*3(3W) or weight2(4W) or 0(1W/2W)
layout(location = 5) in vec4 a_BoneIndices; // D3DCOLOR: 4 bone indices * 3 (stride 40 only)

// Outputs to fragment shader
layout(location = 0) out vec3 v_PositionEye;
layout(location = 1) out vec3 v_NormalEye;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) out vec3 v_WorldPos;
layout(location = 4) out vec3 v_WorldNormal;
layout(location = 5) out vec4 v_CurrClipPos;  // Current clip-space position (for motion vectors)
layout(location = 6) out vec4 v_PrevClipPos;  // Previous clip-space position (for motion vectors)

// Push constants
layout(push_constant) uniform PushConstants
{
    mat4 u_Model;       // offset 0: Model matrix (local -> world)
    mat4 u_PrevModel;   // offset 64: Previous frame model matrix (for per-object motion vectors)
    mat4 u_Projection;  // offset 128: Projection matrix (eye -> clip)
    float u_UVScale;    // offset 192: UV scale (always 1.0 for skinned)
    uint u_SkinMode;    // offset 196: 1=1W, 2=2W, 3=3W, 4=4W
    float _pad0;        // offset 200: (alphaRef - fragment only)
    float _pad1;        // offset 204: (detailScale - fragment only)
    uint u_BoneCount;   // offset 208: aligned bone count (prev bones start at bones[u_BoneCount])
} pc;

// GlobalLighting UBO (Set 0, Binding 0) — View matrix + prevVP
layout(std140, set = 0, binding = 0) uniform GlobalLighting
{
    vec4 _pad_global[95];   // offsets 0-1519 (not used in this shader)
    mat4 m_prevVP;          // offset 1520: Previous frame ViewProjection
    mat4 m_View;            // offset 1584: Current frame View matrix
} uGlobal;

// Bone matrices SSBO (set 2, binding 1)
layout(std430, set = 2, binding = 1) readonly buffer BoneMatrices
{
    mat4 bones[];
};

// Decode bone index from D3DCOLOR alpha channel (stored as index * 3, in 0..1 UBYTE4N range)
uint decodeBoneIndex(float alpha_normalized)
{
    // alpha is 0..1 (UBYTE4N), original value was bone_idx * 3 stored as u8
    // Convert back: round(alpha * 255) / 3
    return uint(round(alpha_normalized * 255.0)) / 3u;
}

// Decode bone index from float field (stored as float(bone_idx * 3))
// Used for tc_i.z and tc_i.w in 2W/3W format
uint decodeBoneIndexFromFloat(float value)
{
    return uint(round(abs(value))) / 3u;
}

void main()
{
    vec3 pos = a_Position.xyz;
    // B8G8R8A8_UNORM gives (Nz,Ny,Nx,bone_idx) in [0,1]; swizzle .zyx → (Nx,Ny,Nz)
    vec3 normal = a_Normal.xyz * 2.0 - 1.0;

    mat4 skinMatrix;
    mat4 prevSkinMatrix;
    uint BC = pc.u_BoneCount; // previous bones start at bones[BC]

    if (pc.u_SkinMode == 1u)
    {
        // ====================================================================
        // 1-bone skinning (stride 36)
        // ====================================================================
        uint boneIdx = decodeBoneIndex(a_Normal.a);
        skinMatrix     = bones[boneIdx];
        prevSkinMatrix = bones[boneIdx + BC];
    }
    else if (pc.u_SkinMode == 2u)
    {
        // ====================================================================
        // 2-bone skinning (stride 44)
        // ====================================================================
        float w0 = a_Normal.a;
        float w1 = 1.0 - w0;

        uint idx0 = decodeBoneIndexFromFloat(a_TexCoordExt.z);
        uint idx1 = decodeBoneIndexFromFloat(a_TexCoordExt.w);

        skinMatrix     = bones[idx0]      * w0 + bones[idx1]      * w1;
        prevSkinMatrix = bones[idx0 + BC] * w0 + bones[idx1 + BC] * w1;
    }
    else if (pc.u_SkinMode == 3u)
    {
        // ====================================================================
        // 3-bone skinning (stride 44)
        // ====================================================================
        float w0 = a_Normal.a;
        float w1 = a_Tangent.a;
        float w2 = 1.0 - w0 - w1;

        uint idx0 = decodeBoneIndexFromFloat(a_TexCoordExt.z);
        uint idx1 = decodeBoneIndexFromFloat(a_TexCoordExt.w);
        uint idx2 = decodeBoneIndex(a_Binormal.a);

        skinMatrix     = bones[idx0]      * w0 + bones[idx1]      * w1 + bones[idx2]      * w2;
        prevSkinMatrix = bones[idx0 + BC] * w0 + bones[idx1 + BC] * w1 + bones[idx2 + BC] * w2;
    }
    else // pc.u_SkinMode == 4u
    {
        // ====================================================================
        // 4-bone skinning (stride 40)
        // ====================================================================
        float w0 = a_Normal.a;
        float w1 = a_Tangent.a;
        float w2 = a_Binormal.a;
        float w3 = 1.0 - w0 - w1 - w2;

        uint idx0 = decodeBoneIndex(a_BoneIndices.r);
        uint idx1 = decodeBoneIndex(a_BoneIndices.g);
        uint idx2 = decodeBoneIndex(a_BoneIndices.b);
        uint idx3 = decodeBoneIndex(a_BoneIndices.a);

        skinMatrix     = bones[idx0]      * w0 + bones[idx1]      * w1
                       + bones[idx2]      * w2 + bones[idx3]      * w3;
        prevSkinMatrix = bones[idx0 + BC] * w0 + bones[idx1 + BC] * w1
                       + bones[idx2 + BC] * w2 + bones[idx3 + BC] * w3;
    }

    // Apply skinning transforms
    vec4 skinnedPos     = skinMatrix * vec4(pos, 1.0);
    vec4 prevSkinnedPos = prevSkinMatrix * vec4(pos, 1.0);
    vec3 skinnedNormal  = mat3(skinMatrix) * normal;

    // Transform through MVP
    vec4 worldPos = pc.u_Model * skinnedPos;
    vec4 eyePos   = uGlobal.m_View * worldPos;
    gl_Position   = pc.u_Projection * eyePos;

    // Motion vectors: current and previous clip-space positions
    v_CurrClipPos = gl_Position;
    v_PrevClipPos = uGlobal.m_prevVP * (pc.u_PrevModel * prevSkinnedPos);

    v_PositionEye = eyePos.xyz;
    v_WorldPos    = worldPos.xyz;

    vec3 worldNormal = (pc.u_Model * vec4(skinnedNormal, 0.0)).xyz;
    v_WorldNormal    = worldNormal;
    v_NormalEye      = (uGlobal.m_View * vec4(worldNormal, 0.0)).xyz;

    // UV is always in .xy
    v_TexCoord = a_TexCoordExt.xy * pc.u_UVScale;
}
