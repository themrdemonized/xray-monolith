#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input from vertex shader
layout(location = 0) in vec3 fragTexCoord;
layout(location = 1) in vec3 fragWorldPos;

// Output (ray entry point в world space)
layout(location = 0) out vec4 outRayData;

void main() {
    // Back faces: Output world position as ray ENTRY point
    // Store в RGB, alpha = 1.0
    outRayData = vec4(fragWorldPos, 1.0);
}
