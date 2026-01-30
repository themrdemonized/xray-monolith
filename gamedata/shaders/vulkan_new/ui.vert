#version 450

// UI Vertex Shader для X-Ray Engine
// Формат вертекса: FVF::TL (position vec4, color uint, uv vec2)

// Input vertex attributes
layout(location = 0) in vec4 inPosition;   // Screen-space position (x, y, z, w)
layout(location = 1) in vec4 inColor;      // Vertex color (normalized vec4 from B8G8R8A8_UNORM)
layout(location = 2) in vec2 inTexCoord;   // Texture coordinates

// Push constants (screen dimensions для нормализации координат)
layout(push_constant) uniform PushConstants {
    vec2 screenSize;    // (width, height)
} pc;

// Output to fragment shader
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    // Convert screen-space coordinates (0..width, 0..height) to NDC (-1..1)
    // Vulkan NDC: X is [-1, 1], Y is [-1, 1]. Y is down in clip space.
    float x = (inPosition.x / pc.screenSize.x) * 2.0 - 1.0;
    float y = (inPosition.y / pc.screenSize.y) * 2.0 - 1.0;

    gl_Position = vec4(x, y, inPosition.z, 1.0);
    // D3DCOLOR is ARGB (0xAARRGGBB), stored as BGRA bytes in memory.
    // VK_FORMAT_B8G8R8A8_UNORM correctly maps: byte0->B, byte1->G, byte2->R, byte3->A
    // Vulkan maps by component NAME, so inColor is already correct RGBA.
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
