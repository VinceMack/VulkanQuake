#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec2 fragLightmapUV;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in flat uvec4 fragStyles; // <--- Received from vertex shader

layout(location = 0) out vec4 outColor;

// Diffuse and Lightmap textures are still in Set 0
layout(set = 0, binding = 0) uniform sampler2D diffuseSampler;
layout(set = 0, binding = 1) uniform sampler2D lightmapSampler;

// ---> NEW: The 64 animated lightstyle floats are in Set 1
layout(set = 1, binding = 0) uniform Lightstyles {
    vec4 values[16]; // 16 vec4s = exactly 64 floats (256 bytes, tightly packed!)
} ubo;

// Helper function to extract the correct float
float GetLightstyle(uint index) {
    return ubo.values[index / 4][index % 4];
}

layout(push_constant) uniform PushConstants {
    mat4 renderMatrix;
    vec3 cameraPos;
    float timeOrInterp;
    uint surfaceType;
} pcs;

void main() {
    // 1. Skybox Logic (Unchanged from previous step)
    if (pcs.surfaceType == 2) {
        vec3 viewDir = normalize(fragWorldPos - pcs.cameraPos);
        vec2 skyUV = viewDir.xy * 1.5; 
        vec2 bgUV = skyUV + vec2(pcs.timeOrInterp * 0.02, pcs.timeOrInterp * 0.01);
        vec2 fgUV = skyUV + vec2(pcs.timeOrInterp * 0.06, pcs.timeOrInterp * 0.03);
        bgUV.t = fract(bgUV.t) * 0.5 + 0.5;
        fgUV.t = fract(fgUV.t) * 0.5;
        vec4 bgColor = texture(diffuseSampler, bgUV);
        vec4 fgColor = texture(diffuseSampler, fgUV);
        outColor = (fgColor.a < 0.1) ? bgColor : fgColor;
        return; 
    }

    // 2. Liquid Warping (Unchanged from previous step)
    vec2 finalUV = fragUV;
    if (pcs.surfaceType == 1) {
        float warpX = sin(fragWorldPos.y * 0.05 + pcs.timeOrInterp * 3.0) * 0.05;
        float warpY = sin(fragWorldPos.x * 0.05 + pcs.timeOrInterp * 3.0) * 0.05;
        finalUV += vec2(warpX, warpY);
    }

    // 3. Normal Drawing with Animated Lightstyles!
    vec4 diffuseColor = texture(diffuseSampler, finalUV);
    vec4 lm = texture(lightmapSampler, fragLightmapUV);
    
    // Composite the 4 shadow layers using the UBO array
    float brightness = 0.0;
    if (fragStyles.x != 255) brightness += lm.r * GetLightstyle(fragStyles.x);
    if (fragStyles.y != 255) brightness += lm.g * GetLightstyle(fragStyles.y);
    if (fragStyles.z != 255) brightness += lm.b * GetLightstyle(fragStyles.z);
    if (fragStyles.w != 255) brightness += lm.a * GetLightstyle(fragStyles.w);

    // If no lightstyles are active (water, slime, lava, or fallback geometry) or if it's a liquid, use the fallback lightmap value (lm.r)
    if (pcs.surfaceType == 1 || (fragStyles.x == 255 && fragStyles.y == 255 && fragStyles.z == 255 && fragStyles.w == 255)) {
        brightness = lm.r;
    }

    // 1. Quake Overbrighting (Lightmaps can double the brightness of a diffuse texture)
    vec3 finalColor = diffuseColor.rgb * (brightness * 1.3);

    // 2. Hardware Gamma Correction (hardware hack to crush shadows and restore mood)
    finalColor = pow(finalColor, vec3(1.0 / 0.6)); 

    outColor = vec4(finalColor, diffuseColor.a);
}