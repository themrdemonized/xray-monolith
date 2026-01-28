#version 450

// Fullscreen quad vertex shader
// Input: quad vertices in clip space (-1..1)
// Output: position and UV for fragment shader

layout(location = 0) in vec4 aPosition;  // Clip space position
layout(location = 1) in vec2 aTexCoord;  // UV coordinates

layout(location = 0) out vec2 vTexCoord;  // Pass UV to fragment shader

void main()
{
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
}
