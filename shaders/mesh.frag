#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec2 fragLightmapUV;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D diffuseSampler;
layout(binding = 1) uniform sampler2D lightmapSampler;

layout(push_constant) uniform PushConstants {
    mat4 renderMatrix;
    vec3 cameraPos;
    float timeOrInterp;
    uint surfaceType;
} pcs;

void main() {
    // ========================================================
    // 1. SKYBOX LOGIC (Dual Parallax)
    // ========================================================
    if (pcs.surfaceType == 2) {
        // Calculate the view direction from the camera to this pixel
        vec3 viewDir = normalize(fragWorldPos - pcs.cameraPos);
        
        // Quake planar sky projection
        vec2 skyUV = viewDir.xy * 1.5; 
        
        // Scroll the background clouds slowly, and foreground clouds faster
        vec2 bgUV = skyUV + vec2(pcs.timeOrInterp * 0.02, pcs.timeOrInterp * 0.01);
        vec2 fgUV = skyUV + vec2(pcs.timeOrInterp * 0.06, pcs.timeOrInterp * 0.03);
        
        // The Quake sky texture is stacked vertically. 
        // Bottom half (0.5 to 1.0) is BG. Top half (0.0 to 0.5) is FG.
        bgUV.t = fract(bgUV.t) * 0.5 + 0.5;
        fgUV.t = fract(fgUV.t) * 0.5;
        
        vec4 bgColor = texture(diffuseSampler, bgUV);
        vec4 fgColor = texture(diffuseSampler, fgUV);
        
        // If foreground is transparent, draw background!
        if (fgColor.a < 0.1) {
            outColor = bgColor;
        } else {
            outColor = fgColor;
        }
        return; // Skies are fullbright, skip lightmaps!
    }

    // ========================================================
    // 2. LIQUID WARPING LOGIC
    // ========================================================
    vec2 finalUV = fragUV;
    
    if (pcs.surfaceType == 1) {
        // Quake's signature sine-wave distortion, mapped to world coordinates
        float warpX = sin(fragWorldPos.y * 0.05 + pcs.timeOrInterp * 3.0) * 0.05;
        float warpY = sin(fragWorldPos.x * 0.05 + pcs.timeOrInterp * 3.0) * 0.05;
        finalUV += vec2(warpX, warpY);
    }

    // ========================================================
    // 3. NORMAL DRAWING
    // ========================================================
    vec4 diffuseColor = texture(diffuseSampler, finalUV);
    vec4 lightmapColor = texture(lightmapSampler, fragLightmapUV);
    outColor = diffuseColor * lightmapColor * 1.5;
}