#version 450
#extension GL_ARB_separate_shader_objects : enable

// Vertex input (FluidVertex format for fullscreen quad)
layout(location = 0) in vec3 inPosition;    // NDC coordinates [-1,1]
layout(location = 1) in vec3 inTexCoord;    // Texture coordinates [0,1]

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Fullscreen quad: pass through position
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);

    // Pass texture coordinates to fragment shader (only XY used for 2D)
    fragTexCoord = inTexCoord.xy;
}
