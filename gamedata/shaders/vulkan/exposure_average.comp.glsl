#version 450
// ============================================================================
// exposure_average.comp.glsl - Compute average luminance + exposure from histogram
// ============================================================================
// Single workgroup (256 threads). Each thread loads one histogram bin into
// shared memory, then thread 0 scans the histogram excluding bottom/top 5%
// of pixels, computes weighted average log-luminance, derives target exposure,
// temporally smooths, and writes to 1x1 R32F exposure image.
// Also clears the histogram SSBO for the next frame.
// ============================================================================

layout(local_size_x = 256) in;

layout(std430, set = 0, binding = 0) buffer HistogramSSBO {
    uint bins[256];
} histogram;

layout(set = 0, binding = 1, r32f) uniform image2D u_Exposure;

layout(push_constant) uniform PC {
    float minLogLum;    // -10.0
    float logLumRange;  // 12.0
    uint  totalPixels;  // width * height
    float adaptSpeed;   // clamp(1.5 * dt, 0, 1)
    float keyValue;     // 0.18 (middle gray)
} pc;

shared uint histShared[256];

void main()
{
    uint lid = gl_LocalInvocationIndex;

    // Load histogram bin into shared memory
    histShared[lid] = histogram.bins[lid];
    barrier();

    // Clear histogram SSBO for next frame (all threads do their bin)
    histogram.bins[lid] = 0u;

    // Only thread 0 computes the average
    if (lid != 0)
        return;

    uint lowCut  = uint(float(pc.totalPixels) * 0.05);
    uint highCut = uint(float(pc.totalPixels) * 0.95);

    float weightedSum = 0.0;
    uint count = 0u;
    uint cumulative = 0u;

    for (int i = 0; i < 256; i++)
    {
        uint prev = cumulative;
        cumulative += histShared[i];

        // Only count pixels between the 5th and 95th percentile
        if (cumulative > lowCut && prev < highCut)
        {
            uint lo = max(prev, lowCut);
            uint hi = min(cumulative, highCut);
            uint contrib = hi - lo;

            // Reconstruct log-luminance from bin center
            float logLum = pc.minLogLum + (float(i) + 0.5) / 256.0 * pc.logLumRange;
            weightedSum += logLum * float(contrib);
            count += contrib;
        }
    }

    // Convert back from log space
    float avgLum = exp2((count > 0u) ? (weightedSum / float(count)) : 0.0);

    // Target exposure: keyValue / averageLuminance, clamped to sane range
    float target = clamp(pc.keyValue / max(avgLum, 1e-4), 0.1, 20.0);

    // Read previous exposure for temporal smoothing
    float prev = imageLoad(u_Exposure, ivec2(0)).r;
    if (prev <= 0.0)
        prev = target;  // First frame: snap to target

    // Exponential smoothing
    float speed = clamp(pc.adaptSpeed, 0.0, 1.0);
    float result = prev + (target - prev) * speed;

    imageStore(u_Exposure, ivec2(0), vec4(result, 0.0, 0.0, 0.0));
}
