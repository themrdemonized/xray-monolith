#version 450

// UI Fragment Shader for X-Ray Engine
// Exactly matches R4 behavior: texture * vertex_color
// Semi-transparency comes from the texture's alpha channel

// Input from vertex shader
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

// Texture sampler (CLAMP_TO_EDGE mode set in C++)
layout(set = 0, binding = 0) uniform sampler2D texSampler;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    // Normal: texture * vertex color
    outColor = texColor * fragColor;
}
