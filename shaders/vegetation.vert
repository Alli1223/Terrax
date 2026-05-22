#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec3  aColor;
layout(location = 3) in float aSway;       // 0 root .. 1 tip (wind / bend weight)
layout(location = 4) in float aSkyLight;
layout(location = 5) in float aBlockLight;

uniform mat4  view;
uniform mat4  projection;
uniform float time;
uniform float u_weather;   // 0 = calm .. 1 = storm

#define MAX_DISTURB 8
uniform int  u_disturbCount;            // movers that bend nearby vegetation
uniform vec3 u_disturbPos[MAX_DISTURB];

out vec3  vColor;
out vec3  vNormal;
out vec3  vWorldPos;
out float vSky;
out float vBlk;

void main() {
    vec3 worldPos = aPos;

    // --- Wind sway --------------------------------------------------------
    // A slow swell plus a faster flutter, displacing each vertex along the
    // wind. aSway^2 weights it so roots stay planted and tips travel most;
    // wind strength rises with the weather so vegetation whips in storms.
    float windStrength = 0.05 + u_weather * 0.30;
    vec2  windDir = normalize(vec2(0.82, 0.30)
                  + 0.35 * vec2(sin(time * 0.07), cos(time * 0.09)));
    float phase   = dot(aPos.xz, vec2(0.17, 0.13));
    float swell   = sin(time * 1.6 + phase) + 0.4 * sin(time * 3.3 + phase * 1.7);
    float flutter = 0.22 * sin(time * 7.5 + phase * 2.4);
    float bend    = (swell + flutter) * windStrength * (aSway * aSway);
    worldPos.xz += windDir * bend;
    worldPos.y  -= abs(bend) * 0.15 * aSway;

    // --- Interaction: vegetation bends and flattens away from movers -------
    for (int i = 0; i < u_disturbCount; i++) {
        vec2  toVeg = aPos.xz - u_disturbPos[i].xz;
        float d     = length(toVeg);
        const float radius = 1.4;
        if (d < radius) {
            float push = 1.0 - d / radius;
            push *= push;                          // smooth radial falloff
            vec2 dir = (d > 0.001) ? toVeg / d : vec2(1.0, 0.0);
            worldPos.xz += dir * (push * 0.55 * aSway);
            worldPos.y  -= push * 0.35 * aSway;    // pressed down underfoot
        }
    }

    vColor    = aColor;
    vNormal   = aNormal;
    vWorldPos = worldPos;
    vSky      = aSkyLight;
    vBlk      = aBlockLight;
    gl_Position = projection * view * vec4(worldPos, 1.0);
}
