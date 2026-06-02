#version 330 core

in vec2  TexCoord;
in float SkyLight;
in float BlockLight;
in vec3  WorldPos;
in vec3  WaveNorm;
in vec3  FaceNormal;
in float WaveHeight;
in float ShoreDist;
in float WaterDepth;
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
uniform float u_weather;   // 0 = clear .. 1 = full storm
#define MAX_LANTERNS 48
uniform int   u_lanternCount;
uniform vec3  u_lanternPos[MAX_LANTERNS];
uniform float u_lanternIntensity[MAX_LANTERNS];
uniform float u_lanternRadius[MAX_LANTERNS];
uniform vec3  u_lanternColor[MAX_LANTERNS];

uniform sampler3D u_lightVol;
uniform vec3      u_lightVolOrigin;
uniform float     u_lightVolSize;

const vec3 LANTERN_COLOR = vec3(1.00, 0.76, 0.40);

// 1.0 = the point light reaches fragPos, 0.0 = an opaque voxel blocks it.
float lightVisibility(vec3 fragPos, vec3 lightPos, vec3 nrm) {
    vec3  start = fragPos + nrm * 0.6;
    vec3  seg   = lightPos - start;
    float dist  = length(seg);
    if (dist < 0.001) return 1.0;
    vec3 dir = seg / dist;
    const int STEPS = 24;
    for (int s = 1; s <= STEPS; s++) {
        float t = dist * float(s) / float(STEPS + 1);
        if (t > dist - 0.8) break;
        vec3 uvw = (start + dir * t - u_lightVolOrigin) / u_lightVolSize;
        if (uvw.x < 0.0 || uvw.x > 1.0 || uvw.y < 0.0 || uvw.y > 1.0 ||
            uvw.z < 0.0 || uvw.z > 1.0) continue;
        if (texture(u_lightVol, uvw).r > 0.5) return 0.0;
    }
    return 1.0;
}

vec3 calcLanternLight(vec3 worldPos, vec3 nrm) {
    vec3 contrib = vec3(0.0);
    for (int i = 0; i < u_lanternCount; i++) {
        float ldist = length(worldPos - u_lanternPos[i]);
        if (ldist >= u_lanternRadius[i]) continue;
        float falloff = 1.0 - ldist / u_lanternRadius[i];
        falloff *= falloff;
        float vis = lightVisibility(worldPos, u_lanternPos[i], nrm);
        contrib += falloff * u_lanternIntensity[i] * vis * u_lanternColor[i];
    }
    return contrib;
}

// ── Value noise / fBm (foam) ──────────────────────────────────────────────────
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

// ── Cloud shadow (matches sky.frag / chunk.frag cloud formula) ────────────────
float cloudNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i),           hash2(i + vec2(1,0)), f.x),
               mix(hash2(i+vec2(0,1)), hash2(i + vec2(1,1)), f.x), f.y);
}
float cloudFBM(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * cloudNoise(p); p *= 2.0; a *= 0.5; }
    return v;
}
float getCloudShadow(vec3 pos, vec3 sunDir, float t) {
    if (sunDir.y <= 0.0) return 1.0;
    float dist = (200.0 - pos.y) / sunDir.y;
    if (dist < 0.0) return 1.0;
    vec3  hit = pos + sunDir * dist;
    vec2  s1  = hit.xz * 0.003 + vec2(t * 0.010, t * 0.007);
    vec2  s2  = hit.xz * 0.006 + vec2(-t * 0.016, t * 0.009) + vec2(31.7, 17.3);
    float n   = cloudFBM(s1) * 0.60 + cloudFBM(s2) * 0.40;
    return 1.0 - smoothstep(0.43, 0.72, n) * 0.75;
}

void main() {
    // Side faces of water blocks get a flat dark colour and exit.
    if (FaceNormal.y < 0.5) {
        FragColor = vec4(0.01, 0.09, 0.20, 0.85);
        return;
    }

    vec3  viewDir = normalize(camPos - WorldPos);
    vec3  wNorm   = normalize(WaveNorm);

    // ── Sea-of-Thieves palette ────────────────────────────────────────────────
    vec3 deepColor    = vec3(0.01, 0.09, 0.24);   // deeper, more saturated abyss
    vec3 shallowColor = vec3(0.06, 0.58, 0.66);   // more vivid turquoise
    vec3 crestColor   = vec3(0.24, 0.86, 0.92);
    vec3 shoreColor   = vec3(0.18, 0.80, 0.78);   // bright turquoise of the shallows

    float wh       = clamp(WaveHeight / 2.10, -1.0, 1.0);
    float crestT   = smoothstep(0.20, 0.72, wh);
    vec3  waterBase = mix(deepColor, shallowColor, 0.5 + 0.5 * wh);
    waterBase       = mix(waterBase, crestColor, crestT * 0.55);
    // Lift the water toward a bright shallow turquoise as it nears land.
    waterBase       = mix(shoreColor, waterBase, smoothstep(0.05, 0.62, ShoreDist));
    // Deep water hides its floor: fade the surface toward an opaque abyssal
    // colour as the column deepens. Shallows (shore, rivers, ponds) keep their
    // clarity. depthFade is reused below to drive the alpha to fully opaque.
    float depthFade = smoothstep(0.12, 0.50, WaterDepth);
    waterBase       = mix(waterBase, deepColor * 0.55, depthFade * 0.85);

    // ── Foam ─────────────────────────────────────────────────────────────────
    vec2 foamUV1 = WorldPos.xz * 0.45 + vec2(time * 0.09,  time * 0.06);
    vec2 foamUV2 = WorldPos.xz * 0.70 + vec2(time * -0.07, time * 0.11);
    float fn1 = fbm(foamUV1), fn2 = fbm(foamUV2);

    float crestFoam  = smoothstep(0.26, 0.66, wh + fn1 * 0.32);
    float rippleFoam = smoothstep(0.60, 0.72, fn1 * 0.60 + fn2 * 0.40);

    // ── Shoreline surf: a foam band that washes up the shore and recedes.
    // ShoreDist is 0 at land and 1 in open water (~6 blocks out). A drifting
    // phase makes stretches of coast break at slightly different times rather
    // than pulsing as one ring.
    float surfNoise = fbm(WorldPos.xz * 0.55 + vec2(time * 0.06, -time * 0.04));
    float wash      = sin(time * 1.25 + fbm(WorldPos.xz * 0.035) * 6.2831);
    float foamReach = mix(0.20, 0.60, 0.5 + 0.5 * wash);
    float surfBand  = smoothstep(foamReach, foamReach - 0.26,
                                 ShoreDist + surfNoise * 0.18 - 0.09);
    float waterline = smoothstep(0.17, 0.015, ShoreDist);   // permanent wet edge
    float shoreFoam = clamp(max(surfBand, waterline), 0.0, 1.0);

    float totalFoam = clamp(max(max(crestFoam, shoreFoam), rippleFoam * 0.6),
                            0.0, 1.0);

    // ── Terrain reflection (planar) ───────────────────────────────────────────
    vec2 reflNDC = v_reflClipPos.xy / v_reflClipPos.w * 0.5 + 0.5;
    vec2 distortion = vec2(wNorm.x, wNorm.z) * 0.045
                    + vec2(sin(time * 0.65 + WorldPos.x * 1.3),
                           cos(time * 0.55 + WorldPos.z * 1.2)) * 0.014;
    vec2 reflUV   = clamp(reflNDC + distortion, 0.002, 0.998);
    vec3 reflColor = texture(u_reflTex, reflUV).rgb;

    float reflLum = dot(reflColor, vec3(0.333));
    vec3 skyFallback = skyAmbient * max(sunFactor, 0.18) * 0.90;
    reflColor = mix(skyFallback, reflColor, smoothstep(0.02, 0.12, reflLum));

    // ── Fresnel ───────────────────────────────────────────────────────────────
    float cosTheta = max(dot(viewDir, wNorm), 0.0);
    float fresnel  = clamp(0.02 + 0.98 * pow(1.0 - cosTheta, 5.0), 0.0, 0.92);

    // ── Specular sun glint: a tight bright core plus a soft wide sheen, with a
    //    drifting sparkle field near the reflection for a lively sun-on-water look.
    float cloudAtten = getCloudShadow(WorldPos, u_sunDir, time);
    vec3  reflSun    = reflect(-u_sunDir, wNorm);
    float sd         = max(dot(viewDir, reflSun), 0.0);
    float spec       = (pow(sd, 220.0) * 1.3 + pow(sd, 36.0) * 0.30) * sunFactor * cloudAtten;
    float sparkle    = smoothstep(0.62, 1.0, fbm(WorldPos.xz * 2.6 - vec2(time * 0.6)))
                       * pow(sd, 8.0) * sunFactor * cloudAtten;

    // ── Lighting ──────────────────────────────────────────────────────────────
    float NdotL  = max(dot(wNorm, u_sunDir), 0.0);
    float diffuse = smoothstep(0.05, 0.55, NdotL);
    vec3  skyAmb  = SkyLight * sunFactor * skyAmbient * 0.22;
    vec3  sun     = diffuse  * sunFactor * skyAmbient * 0.95 * cloudAtten;

    vec3 lantern = calcLanternLight(WorldPos, wNorm);

    vec3 light = skyAmb + sun;
    light = max(light, lantern);
    light = max(light, vec3(0.015, 0.014, 0.020));

    // ── Compose ───────────────────────────────────────────────────────────────
    vec3 result = waterBase * light;

    float reflGamma = 1.0 / 2.2;
    vec3  reflBoosted = pow(clamp(reflColor, 0.0, 1.0), vec3(reflGamma));
    result = mix(result, reflBoosted, fresnel * 0.82);

    result = mix(result, vec3(0.94, 0.97, 1.00) * light, totalFoam);
    // Warm glint near the horizon sun, cooling to bright white as it climbs.
    vec3 sunGlint = mix(vec3(1.00, 0.60, 0.28), vec3(1.00, 0.97, 0.88),
                        clamp(u_sunDir.y * 2.0, 0.0, 1.0));
    result += (spec + sparkle * 0.7) * sunGlint;

    // Saturation boost — eased back toward grey as a storm sets in.
    float lum = dot(result, vec3(0.299, 0.587, 0.114));
    result = mix(vec3(lum), result, mix(1.45, 1.02, u_weather));

    // Warm crest sheen
    result = mix(result, result * vec3(1.06, 1.03, 0.96), crestT * sunFactor * 0.35);

    // ── Atmospheric fog ───────────────────────────────────────────────────────
    float dist     = length(camPos - WorldPos);
    float fogStart = mix(26.0, 10.0, u_weather);
    float fogDens  = mix(0.0030, 0.0125, u_weather);
    float fog      = exp(-max(dist - fogStart, 0.0) * fogDens);
    vec3  fogCol   = skyAmbient * max(sunFactor, 0.12) * mix(0.90, 0.72, u_weather);
    result = mix(fogCol, result, clamp(fog, 0.0, 1.0));

    result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));

    float alpha = clamp(0.72 + fresnel * 0.18 + totalFoam * 0.20, 0.62, 0.98);
    alpha = mix(alpha, 1.0, depthFade);   // deep ocean is fully opaque — no floor shows through
    FragColor = vec4(result, alpha);
}
