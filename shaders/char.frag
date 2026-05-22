#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec4 Color;
in vec4 FragPosLightSpace;

out vec4 FragColor;

uniform vec3  u_sunDir;
uniform float sunFactor;
uniform vec3  skyAmbient;
uniform sampler2D shadowMap;
#define MAX_LANTERNS 48
uniform int   u_lanternCount;
uniform vec3  u_lanternPos[MAX_LANTERNS];
uniform float u_lanternIntensity[MAX_LANTERNS];
uniform float u_lanternRadius[MAX_LANTERNS];
uniform float time;
uniform float u_alpha;
uniform float u_skyExposure;   // 0 = enclosed/indoors, 1 = open sky

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
        contrib += falloff * u_lanternIntensity[i] * vis * LANTERN_COLOR;
    }
    return contrib;
}

// ── Cloud shadow (identical formula to chunk/water shaders) ───────────────────
float cHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float cNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(cHash(i), cHash(i+vec2(1,0)), f.x),
               mix(cHash(i+vec2(0,1)), cHash(i+vec2(1,1)), f.x), f.y);
}
float cFBM(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * cNoise(p); p *= 2.0; a *= 0.5; }
    return v;
}
float getCloudShadow(vec3 pos, vec3 sunDir, float t) {
    if (sunDir.y <= 0.0) return 1.0;
    float dist = (200.0 - pos.y) / sunDir.y;
    if (dist < 0.0) return 1.0;
    vec3 hit = pos + sunDir * dist;
    vec2 s1  = hit.xz * 0.003 + vec2(t * 0.010, t * 0.007);
    vec2 s2  = hit.xz * 0.006 + vec2(-t * 0.016, t * 0.009) + vec2(31.7, 17.3);
    float n  = cFBM(s1) * 0.60 + cFBM(s2) * 0.40;
    return 1.0 - smoothstep(0.43, 0.72, n) * 0.75;
}

float calcShadow(vec4 fragPosLS, float NdotL) {
    vec3 proj = fragPosLS.xyz / fragPosLS.w * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;
    float bias = max(0.006 * (1.0 - NdotL), 0.0015);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++) {
            float d = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += (proj.z - bias > d) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

void main() {
    vec3  norm    = normalize(Normal);
    float NdotL   = max(dot(norm, u_sunDir), 0.0);
    float diffuse = smoothstep(0.05, 0.55, NdotL);

    float shadowFade = clamp(sunFactor * 3.0 - 0.2, 0.0, 1.0);
    float blockShadow = calcShadow(FragPosLightSpace, NdotL) * shadowFade;
    float cloudAtten  = getCloudShadow(FragPos, u_sunDir, time);
    float shadow = min(blockShadow + (1.0 - cloudAtten), 1.0);

    // Indoors the character loses direct sun and most ambient — it must be lit
    // by lanterns, matching the dark house interior around it.
    vec3 skyAmb     = sunFactor * skyAmbient * 0.28 * mix(0.18, 1.0, u_skyExposure);
    vec3 sunContrib = diffuse * (1.0 - shadow * 0.82) * sunFactor * skyAmbient * 0.95 * u_skyExposure;

    vec3 lanternContrib = calcLanternLight(FragPos, norm);

    vec3 light = skyAmb + sunContrib;
    light = max(light, lanternContrib);
    light = max(light, vec3(0.013, 0.011, 0.016));

    vec3 result = Color.rgb * light;
    result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(result, Color.a * u_alpha);
}
