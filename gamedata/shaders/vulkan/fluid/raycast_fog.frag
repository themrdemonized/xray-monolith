#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;

// Textures
layout(set = 0, binding = 0) uniform sampler2D rayDataTex;    // Ray entry/exit points
layout(set = 0, binding = 1) uniform sampler3D densityTex;    // Volume density/color
layout(set = 0, binding = 2) uniform sampler3D velocityTex;   // Volume velocity (optional)

// Push constants
layout(push_constant) uniform RaycastParams {
    vec3 eyePosition;      // Camera position
    float padding1;
    vec3 gridSize;         // Grid dimensions (128, 128, 128)
    float stepSize;        // Ray step size (1/128)
    mat3 invTransform;     // Inverse transform for ray direction
    float density;         // Density multiplier
} params;

// Output
layout(location = 0) out vec4 outColor;

// Constants
const int MAX_STEPS = 128;
const float ABSORPTION = 2.0;
const float SCATTERING = 0.5;

void main() {
    // Read ray entry/exit points from RayDataTex
    vec4 rayData = texture(rayDataTex, fragTexCoord);
    vec3 rayEntry = rayData.xyz;
    
    // Calculate ray direction (from entry to eye)
    vec3 rayDir = normalize(params.eyePosition - rayEntry);
    
    // Transform ray direction to volume space
    vec3 volumeRayDir = params.invTransform * rayDir;
    
    // Calculate ray length (fixed for now, should use exit point)
    float rayLength = length(params.gridSize) * params.stepSize;
    
    // Raymarching variables
    vec3 currentPos = rayEntry;
    vec4 accumulatedColor = vec4(0.0);
    float transmittance = 1.0;
    
    // Raymarching loop
    for (int i = 0; i < MAX_STEPS; i++) {
        if (transmittance < 0.01) break;  // Early exit if fully opaque
        
        // Convert world position to texture coordinates [0,1]^3
        vec3 texCoord = (currentPos - rayEntry) / params.gridSize + 0.5;
        
        // Clamp to valid range
        if (any(lessThan(texCoord, vec3(0.0))) || any(greaterThan(texCoord, vec3(1.0)))) {
            break;
        }
        
        // Sample density from 3D texture
        vec4 densitySample = texture(densityTex, texCoord);
        float density = densitySample.a * params.density;
        
        if (density > 0.001) {
            // Lighting (simple ambient for fog)
            vec3 color = densitySample.rgb;
            float light = 0.5 + 0.5 * texCoord.y;  // Height-based lighting
            
            // Beer-Lambert law (absorption)
            float absorption = exp(-density * ABSORPTION * params.stepSize);
            
            // Accumulate color
            vec3 luminance = color * light * density * params.stepSize;
            accumulatedColor.rgb += luminance * transmittance;
            
            // Update transmittance
            transmittance *= absorption;
        }
        
        // Advance ray
        currentPos += volumeRayDir * params.stepSize;
    }
    
    // Final alpha (1 - transmittance)
    accumulatedColor.a = 1.0 - transmittance;
    
    // Output final color
    outColor = accumulatedColor;
}
