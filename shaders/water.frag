#version 330 core

in vec2  TexCoord;
in float SkyLight;
in float BlockLight;
in vec3  WorldPos;
in vec3  WaveNorm;
in vec3  FaceNormal;
in float WaveHeight;
in vec4  v_reflClipPos;

out vec4 FragColor;

uniform sampler2D atlas;
uniform sampler2D u_reflTex;
uniform float sunFactor;
uniform vec3  skyAmbient;
uniform vec3  camPos;
uniform float time;
uniform float timeOfDay;
uniform vec3  u_sunDir;
uniform vec3  u_lanternPos;
uniform float u_lanternIntensity;
uniform float u_lanternRadius;

// ---- value noise ------------------------------------------------
float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i),             hash2(i + vec2(1,0)), f.x),
               mix(hash2(i + vec2(0,1)), hash2(i + vec2(1,1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { v += a * vnoise(p); p *= 2.1; a *= 0.5; }
    return v;
}

void main() {
    // Side faces of water blocks get a flat dark colour and exit.
    if (FaceNormal.y < 0.5) {
        FragColor = vec4(0.01, 0.09, 0.20, 0.85);
        return;
    }

    vec3  viewDir = normalize(camPos - WorldPos);
    vec3  wNorm   = normalize(WaveNorm);

    // ---- Sea-of-Thieves palette --------------------------------
    // Deep ocean → shallow turquoise → bright wave crest
    vec3 deepColor    = vec3(0.01, 0.12, 0.28);
    vec3 shallowColor = vec3(0.05, 0.52, 0.62);
    vec3 crestColor   = vec3(0.20, 0.80, 0.86);

    float wh          = clamp(WaveHeight / 1.84, -1.0, 1.0);   // ~[-1,1]
    float crestT      = smoothstep(0.20, 0.72, wh);
    vec3  waterBase   = mix(deepColor, shallowColor, 0.5 + 0.5 * wh);
    waterBase         = mix(waterBase, crestColor, crestT * 0.55);

    // ---- Foam --------------------------------------------------
    // Two fBm layers scroll at different speeds for organic texture.
    vec2 foamUV1 = WorldPos.xz * 0.45 + vec2(time * 0.09,  time * 0.06);
    vec2 foamUV2 = WorldPos.xz * 0.70 + vec2(time * -0.07, time * 0.11);
    float fn1 = fbm(foamUV1);
    float fn2 = fbm(foamUV2);

    // Whitecaps on wave crests
    float crestFoam  = smoothstep(0.30, 0.70, wh + fn1 * 0.30);
    // Small scattered ripple-foam across the surface
    float rippleFoam = smoothstep(0.60, 0.72, fn1 * 0.60 + fn2 * 0.40);

    // Shore foam: animated ring pattern, fades with depth proxy
    // (We use a high-freq noise layer that mimics breaking surf.)
    vec2 shoreUV = WorldPos.xz * 1.8 + vec2(time * 0.22, time * -0.15);
    float shoreFn = fbm(shoreUV);
    float shoreFoam = smoothstep(0.56, 0.65, shoreFn) * (1.0 - abs(wh) * 0.5);

    float totalFoam = clamp(max(crestFoam, max(rippleFoam * 0.65, shoreFoam * 0.50)), 0.0, 1.0);

    // ---- Terrain reflection (planar) ---------------------------
    vec2 reflNDC = v_reflClipPos.xy / v_reflClipPos.w;
    reflNDC      = reflNDC * 0.5 + 0.5;

    // Distort UVs with wave normal + slow scrolling ripple
    vec2 distortion = vec2(wNorm.x, wNorm.z) * 0.045
                    + vec2(sin(time * 0.65 + WorldPos.x * 1.3),
                           cos(time * 0.55 + WorldPos.z * 1.2)) * 0.014;
    vec2 reflUV = clamp(reflNDC + distortion, 0.002, 0.998);

    vec3 reflColor  = texture(u_reflTex, reflUV).rgb;

    // Where the reflection texture is black (sky/outside), blend toward
    // the sky ambient so the fallback is smooth.
    float reflLum = dot(reflColor, vec3(0.333));
    vec3 skyFallback = skyAmbient * max(sunFactor, 0.18) * 0.90;
    reflColor = mix(skyFallback, reflColor, smoothstep(0.02, 0.12, reflLum));

    // ---- Fresnel -----------------------------------------------
    float cosTheta = max(dot(viewDir, wNorm), 0.0);
    float fresnel  = 0.04 + 0.96 * pow(1.0 - cosTheta, 4.0);
    fresnel = clamp(fresnel, 0.0, 0.85);

    // ---- Specular highlight ------------------------------------
    vec3  reflSun = reflect(-u_sunDir, wNorm);
    float spec    = pow(max(dot(viewDir, reflSun), 0.0), 90.0) * sunFactor;

    // ---- Lighting ----------------------------------------------
    float NdotL  = max(dot(wNorm, u_sunDir), 0.0);
    float diffuse = smoothstep(0.05, 0.55, NdotL);
    vec3 skyAmb   = SkyLight * sunFactor * skyAmbient * 0.22;
    vec3 sun      = diffuse  * sunFactor * skyAmbient * 0.95;

    float ldist   = length(WorldPos - u_lanternPos);
    float lfall   = max(0.0, 1.0 - ldist / u_lanternRadius);
    lfall        *= lfall;
    vec3  lantern = lfall * u_lanternIntensity * vec3(1.00, 0.76, 0.40);

    vec3 light = skyAmb + sun;
    light = max(light, lantern);
    light = max(light, vec3(0.015, 0.014, 0.020));

    // ---- Compose -----------------------------------------------
    // 1. Lit base water colour
    vec3 result = waterBase * light;

    // 2. Reflection blended in by Fresnel
    //    Boost reflection contrast slightly for cartoonish punch.
    float reflGamma = 1.0 / 2.2;
    vec3  reflBoosted = pow(clamp(reflColor, 0.0, 1.0), vec3(reflGamma));
    result = mix(result, reflBoosted, fresnel * 0.70);

    // 3. Foam overlaid on top (receives the same lighting)
    vec3 foamCol = vec3(0.94, 0.97, 1.00);
    result = mix(result, foamCol * light, totalFoam);

    // 4. Specular (additive, slightly boosted for Sea-of-Thieves glint)
    result += spec * skyAmbient * 0.90;

    // ---- Stylised colour grading (cartoonish) ------------------
    // Saturation boost: punchy, vivid water
    float lum = dot(result, vec3(0.299, 0.587, 0.114));
    result = mix(vec3(lum), result, 1.45);

    // Slight warm highlight at crests (SoT has a subtle warm-sun sheen)
    result = mix(result, result * vec3(1.06, 1.03, 0.96), crestT * sunFactor * 0.35);

    // ---- Atmospheric fog ---------------------------------------
    float dist    = length(camPos - WorldPos);
    float fogDist = max(dist - 32.0, 0.0);
    float fog     = exp(-fogDist * 0.0018);
    vec3  fogCol  = skyAmbient * max(sunFactor, 0.15) * 0.85;
    result = mix(fogCol, result, clamp(fog, 0.0, 1.0));

    // ---- Gamma correction --------------------------------------
    result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));

    // ---- Alpha -------------------------------------------------
    float alpha = 0.70 + fresnel * 0.20 + totalFoam * 0.10;
    alpha = clamp(alpha, 0.60, 0.97);

    FragColor = vec4(result, alpha);
}
