#version 330 core

in vec3  vColor;
in vec3  vNormal;
in vec3  vWorldPos;
in float vSky;
in float vBlk;

out vec4 FragColor;

uniform float sunFactor;
uniform vec3  skyAmbient;
uniform vec3  u_sunDir;
uniform vec3  camPos;
uniform float u_weather;

#define MAX_LANTERNS 48
uniform int   u_lanternCount;
uniform vec3  u_lanternPos[MAX_LANTERNS];
uniform float u_lanternIntensity[MAX_LANTERNS];
uniform float u_lanternRadius[MAX_LANTERNS];
uniform vec3  u_lanternColor[MAX_LANTERNS];

// Warm point lights — lanterns, street lamps, campfires.
const vec3 LANTERN_COLOR = vec3(1.00, 0.76, 0.40);
vec3 calcLanternLight(vec3 worldPos) {
    vec3 contrib = vec3(0.0);
    for (int i = 0; i < u_lanternCount; i++) {
        float ldist = length(worldPos - u_lanternPos[i]);
        if (ldist >= u_lanternRadius[i]) continue;
        float falloff = 1.0 - ldist / u_lanternRadius[i];
        falloff *= falloff;
        contrib += falloff * u_lanternIntensity[i] * u_lanternColor[i];
    }
    return contrib;
}

void main() {
    // Two-sided shading — blades and fronds are thin, lit from either face.
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;

    float NdotL   = max(dot(N, u_sunDir), 0.0);
    float diffuse = 0.45 + 0.55 * smoothstep(0.0, 0.7, NdotL);

    // Direct sun only reaches vegetation open to the sky; baked sky light keeps
    // plants under a canopy shaded.
    float skyMask = smoothstep(0.15, 0.5, vSky);
    vec3  skyAmb  = vSky * sunFactor * skyAmbient * 0.30;
    vec3  sun     = diffuse * sunFactor * skyAmbient * 0.85 * skyMask;
    vec3  blk     = vBlk * vec3(1.00, 0.76, 0.40) * 0.9;

    vec3 light = skyAmb + sun;
    light = max(light, blk);
    light = max(light, calcLanternLight(vWorldPos));
    light = max(light, vec3(0.020, 0.020, 0.025));

    vec3 result = vColor * light;

    // Saturation: lively in clear weather, muted under a storm sky.
    float lum = dot(result, vec3(0.299, 0.587, 0.114));
    result = mix(vec3(lum), result, mix(1.12, 0.82, u_weather));

    // Atmospheric fog — matches the terrain shader.
    float dist     = length(vWorldPos - camPos);
    float fogStart = mix(26.0, 10.0, u_weather);
    float fogDens  = mix(0.0030, 0.0125, u_weather);
    float fog      = exp(-max(dist - fogStart, 0.0) * fogDens);
    vec3  fogCol   = skyAmbient * max(sunFactor, 0.12) * mix(0.90, 0.72, u_weather);
    result = mix(fogCol, result, clamp(fog, 0.0, 1.0));

    result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(result, 1.0);
}
