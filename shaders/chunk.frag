#version 330 core

in vec2  TexCoord;
in float SkyLight;
in float BlockLight;
in vec3  FragWorldPos;
in vec3  FragNormal;
in vec4  FragPosLightSpace;

out vec4 FragColor;

uniform sampler2D atlas;
uniform sampler2D shadowMap;
uniform float sunFactor;
uniform vec3  skyAmbient;
uniform vec3  camPos;
uniform vec3  u_sunDir;
#define MAX_LANTERNS 16
uniform int   u_lanternCount;
uniform vec3  u_lanternPos[MAX_LANTERNS];
uniform float u_lanternIntensity[MAX_LANTERNS];
uniform float u_lanternRadius[MAX_LANTERNS];
uniform float time;

const vec3 LANTERN_COLOR = vec3(1.00, 0.76, 0.40);

vec3 calcLanternLight(vec3 worldPos) {
    vec3 contrib = vec3(0.0);
    for (int i = 0; i < u_lanternCount; i++) {
        float ldist   = length(worldPos - u_lanternPos[i]);
        float falloff = max(0.0, 1.0 - ldist / u_lanternRadius[i]);
        falloff *= falloff;
        contrib = max(contrib, falloff * u_lanternIntensity[i] * LANTERN_COLOR);
    }
    return contrib;
}

float cloudHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float cloudNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(cloudHash(i), cloudHash(i + vec2(1,0)), f.x),
               mix(cloudHash(i + vec2(0,1)), cloudHash(i + vec2(1,1)), f.x), f.y);
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
    vec3 hit = pos + sunDir * dist;
    vec2 s1  = hit.xz * 0.003 + vec2(t * 0.010, t * 0.007);
    vec2 s2  = hit.xz * 0.006 + vec2(-t * 0.016, t * 0.009) + vec2(31.7, 17.3);
    float n  = cloudFBM(s1) * 0.60 + cloudFBM(s2) * 0.40;
    return 1.0 - smoothstep(0.43, 0.72, n) * 0.75;
}

float calcShadow(vec4 fragPosLS, float NdotL) {
    vec3 proj = fragPosLS.xyz / fragPosLS.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;

    float bias = max(0.006 * (1.0 - NdotL), 0.0015);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    // 3x3 PCF kernel for soft shadows
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float closestDepth = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += (proj.z - bias > closestDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec4 texSample = texture(atlas, TexCoord);
    if (texSample.a < 0.5) discard;
    vec3 base = texSample.rgb;

    // Directional sun: NdotL with soft ramp
    float NdotL   = max(dot(FragNormal, u_sunDir), 0.0);
    float diffuse = smoothstep(0.05, 0.55, NdotL);

    // Shadow (geometry shadow + cloud shadow combined)
    float shadowFade  = clamp(sunFactor * 3.0 - 0.2, 0.0, 1.0);
    float shadow      = calcShadow(FragPosLightSpace, NdotL) * shadowFade;
    float cloudAtten  = getCloudShadow(FragWorldPos, u_sunDir, time);

    // Sky ambient (indirect light, not shadowed)
    vec3 skyAmb = SkyLight * sunFactor * skyAmbient * 0.22;

    // Direct sun, attenuated by both geometry shadow and cloud shadow
    vec3 sunContrib = diffuse * (1.0 - shadow * 0.82) * sunFactor * skyAmbient * 0.95 * cloudAtten;

    // Warm block / lantern light
    vec3 blockContrib = BlockLight * vec3(1.00, 0.76, 0.40) * 0.9;
    vec3 lanternContrib = calcLanternLight(FragWorldPos);

    vec3 light = skyAmb + sunContrib;
    light = max(light, blockContrib);
    light = max(light, lanternContrib);
    light = max(light, vec3(0.013, 0.011, 0.016));

    vec3 result = base * light;

    // Slight saturation boost (cartoonish punch)
    float lum = dot(result, vec3(0.299, 0.587, 0.114));
    result = mix(vec3(lum), result, 1.15);

    // Atmospheric fog
    float dist    = length(FragWorldPos - camPos);
    float fogDist = max(dist - 32.0, 0.0);
    float fog     = exp(-fogDist * 0.0018);
    vec3  fogCol  = skyAmbient * max(sunFactor, 0.15) * 0.85;
    result = mix(fogCol, result, clamp(fog, 0.0, 1.0));

    // Valheim-style ground mist: low-lying haze below ~y=36, thickens with distance
    float mistHeight  = clamp(36.0 - FragWorldPos.y, 0.0, 10.0) / 10.0;
    float mistFogDist = max(dist - 8.0, 0.0);
    float mistDensity = 1.0 - exp(-mistFogDist * 0.012);
    float mist        = mistHeight * mistDensity;
    vec3  mistCol     = skyAmbient * max(sunFactor, 0.10) * 1.05;
    result = mix(result, mistCol, mist * 0.55);

    // Gamma correction (no Reinhard — avoids washed-out look)
    result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(result, 1.0);
}
