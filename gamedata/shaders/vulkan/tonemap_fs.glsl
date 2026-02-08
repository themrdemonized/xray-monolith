#version 450

// ============================================================================
// tonemap_fs.glsl - Tonemap Pass Fragment Shader
// ============================================================================
//
// Reads rt_HDR (full-scene HDR), applies:
//   1. Exposure
//   2. Tone mapping (ACES/Reinhard/Uncharted2)
//   3. Vignette
//   4. Gamma correction (if swapchain is UNORM)
//
// Outputs LDR to swapchain.
//
// ============================================================================

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 o_Color;

// Set 1, binding 0: rt_HDR (HDR scene)
layout(set = 1, binding = 0) uniform sampler2D s_hdr;

// Push constants (matches TonemapPushConstants in C++)
layout(push_constant) uniform PushConstants
{
    float exposure;
    float vignetteInner;
    float vignetteOuter;
    float vignetteIntensity;
    uint toneMappingMode;  // 0=None, 1=Reinhard, 2=ACES, 3=Uncharted2
} pc;

// ============================================================================
// Tone Mapping Operators
// ============================================================================

vec3 tonemap_reinhard(vec3 color)
{
    return color / (color + 1.0);
}

vec3 tonemap_aces(vec3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 tonemap_uncharted2_partial(vec3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 tonemap_uncharted2(vec3 color)
{
    float W = 11.2;
    vec3 curr = tonemap_uncharted2_partial(color * 2.0);
    vec3 whiteScale = 1.0 / tonemap_uncharted2_partial(vec3(W));
    return curr * whiteScale;
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    vec3 color = texture(s_hdr, v_TexCoord).rgb;

    // 1. Apply exposure
    color *= pc.exposure;

    // 2. Apply tone mapping (HDR -> LDR)
    if (pc.toneMappingMode == 1) {
        color = tonemap_reinhard(color);
    } else if (pc.toneMappingMode == 2) {
        color = tonemap_aces(color);
    } else if (pc.toneMappingMode == 3) {
        color = tonemap_uncharted2(color);
    }

    // 3. Apply vignette
    vec2 centerOffset = v_TexCoord - 0.5;
    float dist = length(centerOffset);
    float vignette = 1.0 - smoothstep(pc.vignetteInner, pc.vignetteOuter, dist);
    vignette = mix(1.0, vignette, pc.vignetteIntensity);
    color *= vignette;

    // 4. Gamma correction
    // If swapchain is SRGB, hardware handles gamma.
    // If swapchain is UNORM, apply manually:
    // color = pow(color, vec3(1.0 / 2.2));

    o_Color = vec4(color, 1.0);
}
