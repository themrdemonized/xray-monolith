#version 450

// Water waves fragment shader - procedural wave height/normal generation
// Output: RG = normal XY (0.5 = neutral), BA = height/foam data
// Uses fullscreen triangle (combine.vert.spv)

layout(location = 0) out vec4 outWaves;

// Push constants
layout(push_constant) uniform WaveParams {
    float time;
    float windDir;
    float windVel;
    float pad;
} params;

// Simple hash for pseudo-random noise
float hash(vec2 p) {
    p = fract(p * vec2(443.8975, 397.2973));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}

// Value noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Fractal Brownian motion
float fbm(vec2 p) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;

    for (int i = 0; i < 4; i++) {
        val += noise(p * freq) * amp;
        amp *= 0.5;
        freq *= 2.0;
    }

    return val;
}

void main() {
    vec2 uv = gl_FragCoord.xy / 512.0;

    float windRad = params.windDir * 3.14159265 / 180.0;
    vec2 windDir = vec2(cos(windRad), sin(windRad));
    float windScale = max(params.windVel * 0.01, 0.1);

    // Multiple wave octaves
    float t = params.time;
    vec2 waveUV1 = uv * 8.0 + windDir * t * 0.3 * windScale;
    vec2 waveUV2 = uv * 16.0 - windDir.yx * t * 0.2 * windScale;
    vec2 waveUV3 = uv * 4.0 + windDir * t * 0.1 * windScale;

    float wave1 = fbm(waveUV1);
    float wave2 = fbm(waveUV2) * 0.5;
    float wave3 = fbm(waveUV3) * 0.25;

    float height = wave1 + wave2 + wave3;

    // Compute normal from height derivatives
    float eps = 1.0 / 512.0;
    vec2 uvR = uv + vec2(eps, 0.0);
    vec2 uvU = uv + vec2(0.0, eps);

    float hR = fbm(uvR * 8.0 + windDir * t * 0.3 * windScale) +
               fbm(uvR * 16.0 - windDir.yx * t * 0.2 * windScale) * 0.5;
    float hU = fbm(uvU * 8.0 + windDir * t * 0.3 * windScale) +
               fbm(uvU * 16.0 - windDir.yx * t * 0.2 * windScale) * 0.5;

    vec2 normalXY = vec2(height - hR, height - hU) * windScale;

    // Encode: 0.5 = neutral, range [-1,1] -> [0,1]
    outWaves = vec4(normalXY * 0.5 + 0.5, height, 0.0);
}
