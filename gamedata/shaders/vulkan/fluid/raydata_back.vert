#version 450
#extension GL_ARB_separate_shader_objects : enable

// Vertex input (FluidVertex)
layout(location = 0) in vec3 inPosition;    // Bounding box position [0,1]^3
layout(location = 1) in vec3 inTexCoord;    // 3D texture coordinate

// Push constants (MVP matrix)
layout(push_constant) uniform PushConstants {
    mat4 mvp;  // Model-View-Projection matrix
} pc;

// Output to fragment shader
layout(location = 0) out vec3 fragTexCoord;
layout(location = 1) out vec3 fragWorldPos;

void main() {
    // Transform vertex to clip space
    gl_Position = pc.mvp * vec4(inPosition, 1.0);

    // Pass texture coordinate and position to fragment shader
    fragTexCoord = inTexCoord;
    fragWorldPos = inPosition;  // Store local position (for now)
}
