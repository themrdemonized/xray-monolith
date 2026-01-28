#version 450

// ============================================================================
// forward.frag - Forward Rendering Fragment Shader
// ============================================================================
//
// Phase 2.19.3: Forward Shaders
//
// Forward lighting для transparent objects.
//
// Features:
// - Albedo texture sampling
// - Directional light (sun)
// - Point lights (up to 4 closest)
// - Ambient lighting
// - Alpha output (для blending)
//
// Simplified lighting model (не PBR):
// - Lambertian diffuse
// - Blinn-Phong specular (optional)
// - Distance attenuation для point lights
//
// ============================================================================

// Inputs from vertex shader
layout(location = 0) in vec3 v_Position;  // Eye-space position
layout(location = 1) in vec3 v_Normal;    // Eye-space normal
layout(location = 2) in vec2 v_TexCoord;  // Texture coordinates

// Output
layout(location = 0) out vec4 o_Color;

// ============================================================================
// Descriptor Sets
// ============================================================================

// Set 2: Material textures (per-object)
layout(set = 2, binding = 0) uniform sampler2D s_diffuse;  // Albedo/diffuse texture

// ============================================================================
// Push Constants (Lighting Data)
// ============================================================================

const int MAX_POINT_LIGHTS = 4;  // Maximum point lights to consider

layout(push_constant) uniform PushConstants
{
    // Matrices (offset 0, 192 bytes)
    mat4 u_Model;
    mat4 u_View;
    mat4 u_Projection;

    // Directional light (sun) - offset 192
    vec4 u_SunDirection;  // Eye-space direction, .w = intensity
    vec4 u_SunColor;      // RGB color, .w = unused

    // Ambient light - offset 224
    vec4 u_AmbientColor;  // RGB color, .w = unused

    // Point lights count - offset 240
    uint u_PointLightCount;  // Number of active point lights (0-4)
    uint padding[3];

    // Point lights (offset 256, up to 4 lights)
    // Each light: 32 bytes (2 vec4)
    vec4 u_PointLightPosRange[MAX_POINT_LIGHTS];  // xyz = eye-space pos, w = 1/(range*range)
    vec4 u_PointLightColor[MAX_POINT_LIGHTS];     // rgb = color, a = intensity
} pc;

// ============================================================================
// Lighting Functions
// ============================================================================

/**
 * CalculateDirectionalLight() - Compute directional light contribution
 *
 * @param N - Normal (eye-space, normalized)
 * @param albedo - Surface albedo color
 * @return Light contribution (RGB)
 */
vec3 CalculateDirectionalLight(vec3 N, vec3 albedo)
{
    // Light direction (already in eye-space)
    vec3 L = normalize(pc.u_SunDirection.xyz);

    // Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);

    // Light contribution
    vec3 diffuse = pc.u_SunColor.rgb * NdotL * pc.u_SunDirection.w;

    return albedo * diffuse;
}

/**
 * CalculatePointLight() - Compute point light contribution
 *
 * @param P - Fragment position (eye-space)
 * @param N - Normal (eye-space, normalized)
 * @param albedo - Surface albedo color
 * @param index - Light index (0-3)
 * @return Light contribution (RGB)
 */
vec3 CalculatePointLight(vec3 P, vec3 N, vec3 albedo, uint index)
{
    // Light position and attenuation factor
    vec3 lightPos = pc.u_PointLightPosRange[index].xyz;
    float attFactor = pc.u_PointLightPosRange[index].w;  // 1 / (range * range)

    // Light direction and distance
    vec3 L = lightPos - P;
    float dist = length(L);
    L /= dist;  // Normalize

    // Distance attenuation (inverse square)
    float attenuation = 1.0 / (dist * dist);

    // Smooth falloff at light range edge
    float rangeFactor = 1.0 - smoothstep(0.0, 1.0, sqrt(dist * dist * attFactor));
    attenuation *= rangeFactor;

    // Early exit if too far
    if (rangeFactor <= 0.001) {
        return vec3(0.0);
    }

    // Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);

    // Light contribution
    vec3 lightColor = pc.u_PointLightColor[index].rgb;
    float intensity = pc.u_PointLightColor[index].a;
    vec3 diffuse = lightColor * NdotL * intensity * attenuation;

    return albedo * diffuse;
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    // ========================================================================
    // 1. Sample albedo texture
    // ========================================================================
    vec4 albedo = texture(s_diffuse, v_TexCoord);

    // Alpha test (discard fully transparent pixels)
    if (albedo.a < 0.01) {
        discard;
    }

    // ========================================================================
    // 2. Normalize normal (может быть денормализован после interpolation)
    // ========================================================================
    vec3 N = normalize(v_Normal);

    // ========================================================================
    // 3. Initialize lighting with ambient
    // ========================================================================
    vec3 lighting = pc.u_AmbientColor.rgb;

    // ========================================================================
    // 4. Add directional light (sun)
    // ========================================================================
    lighting += CalculateDirectionalLight(N, albedo.rgb);

    // ========================================================================
    // 5. Add point lights (up to 4 closest)
    // ========================================================================
    uint lightCount = min(pc.u_PointLightCount, MAX_POINT_LIGHTS);

    for (uint i = 0; i < lightCount; i++) {
        lighting += CalculatePointLight(v_Position, N, albedo.rgb, i);
    }

    // ========================================================================
    // 6. Final color (с alpha для blending)
    // ========================================================================
    o_Color = vec4(lighting, albedo.a);
}
