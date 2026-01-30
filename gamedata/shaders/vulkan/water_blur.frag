#version 450

// Water blur fragment shader - Gaussian blur for water SSR smoothing
// Uses fullscreen triangle (no vertex inputs needed - combine.vert.spv)

layout(location = 0) out vec4 outColor;

// Set 1: Input texture (water SSR result)
layout(set = 1, binding = 0) uniform sampler2D inputTex;

// Push constants: blur direction and texel size
layout(push_constant) uniform BlurParams {
    float dirX;
    float dirY;
    float texelSizeX;
    float texelSizeY;
} params;

// 9-tap Gaussian weights (sigma ~= 2.0)
const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec2 uv = gl_FragCoord.xy * vec2(params.texelSizeX, params.texelSizeY);
    vec2 blurDir = vec2(params.dirX, params.dirY) * vec2(params.texelSizeX, params.texelSizeY);

    vec4 result = texture(inputTex, uv) * weights[0];

    for (int i = 1; i < 5; i++) {
        vec2 offset = blurDir * float(i);
        result += texture(inputTex, uv + offset) * weights[i];
        result += texture(inputTex, uv - offset) * weights[i];
    }

    outColor = result;
}
