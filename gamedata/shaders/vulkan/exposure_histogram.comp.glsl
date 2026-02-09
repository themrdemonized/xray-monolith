#version 450
// ============================================================================
// exposure_histogram.comp.glsl - Build luminance histogram from HDR scene
// ============================================================================
// Reads rt_HDR (R16G16B16A16_SFLOAT), computes log-luminance per pixel,
// atomicAdd into 256-bin SSBO histogram.
// Dispatched as ceil(width/16) x ceil(height/16) workgroups.
// ============================================================================

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0) uniform sampler2D u_HDR;
layout(std430, set = 0, binding = 1) buffer HistogramSSBO {
    uint bins[256];
} histogram;

layout(push_constant) uniform PC {
    float minLogLum;   // -10.0
    float logLumRange; // 12.0 (covers -10 to +2)
    uint  width;
    uint  height;
} pc;

void main()
{
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= pc.width || gid.y >= pc.height)
        return;

    vec2 uv = (vec2(gid) + 0.5) / vec2(pc.width, pc.height);
    vec3 color = texture(u_HDR, uv).rgb;

    // Rec.709 luminance
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Skip near-black pixels (they'd map to extreme negative log values)
    if (lum < 1e-5)
        return;

    // Map log2(luminance) to [0..1] range within our configured window
    float logLum = clamp((log2(lum) - pc.minLogLum) / pc.logLumRange, 0.0, 1.0);

    // Map to bin index [0..254] (bin 255 reserved for overflow)
    uint bin = uint(logLum * 254.0 + 0.5);
    atomicAdd(histogram.bins[bin], 1u);
}
