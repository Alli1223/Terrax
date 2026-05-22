#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aTexCoord;
layout(location = 3) in float aMaterialID;
layout(location = 4) in float aSkyLight;
layout(location = 5) in float aBlockLight;
layout(location = 6) in float aShoreDistance;

uniform mat4  model;
uniform mat4  view;
uniform mat4  projection;
uniform float time;
uniform mat4  u_reflProjView;

out vec2  TexCoord;
out float SkyLight;
out float BlockLight;
out vec3  WorldPos;
out vec3  WaveNorm;
out vec3  FaceNormal;
out float WaveHeight;
out float ShoreDist;
out vec4  v_reflClipPos;

float hashV(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hashV(i),           hashV(i + vec2(1,0)), f.x),
               mix(hashV(i+vec2(0,1)), hashV(i + vec2(1,1)), f.x), f.y);
}

void main() {
    vec3 pos = aPos;

    if (aNormal.y > 0.5) {
        // --- Group A: Ocean swells (long wavelength, dominant height) ---
        float pa0 = pos.x * 0.110 + pos.z * 0.000  + time * 0.38;
        float pa1 = pos.x * 0.000 + pos.z * 0.135  + time * 0.32;
        float pa2 = pos.x * 0.078 + pos.z * 0.082  + time * 0.43;
        float pa3 =-pos.x * 0.048 + pos.z * 0.112  + time * 0.36;

        // --- Group B: Mid-range chop ---
        float pb0 = pos.x * 0.530 + pos.z * 0.185  + time * 1.05;
        float pb1 =-pos.x * 0.245 + pos.z * 0.555  + time * 0.88;
        float pb2 = pos.x * 0.375 - pos.z * 0.428  + time * 1.22;

        // --- Group C: Short surface ripple ---
        float pc0 = pos.x * 1.340 + pos.z * 0.730  + time * 2.18;
        float pc1 =-pos.x * 0.850 + pos.z * 1.470  + time * 1.92;
        float pc2 = pos.x * 1.610 - pos.z * 0.550  + time * 2.52;

        // Independent envelopes per group — each drifts at a different speed/scale
        // so patches of rough/calm water emerge at different spatial frequencies.
        vec2  ep   = pos.xz * 0.052;
        float envA = 0.32 + 0.68 * vnoise(ep * 0.55 + vec2(time *  0.013, time *  0.009));
        float envB = 0.28 + 0.72 * vnoise(ep * 1.30 + vec2(time * -0.021, time *  0.027));
        float envC = 0.38 + 0.62 * vnoise(ep * 2.60 + vec2(time *  0.034, time * -0.017));

        float hA = (sin(pa0)*0.30 + sin(pa1)*0.25 + sin(pa2)*0.18 + sin(pa3)*0.14) * envA;
        float hB = (sin(pb0)*0.12 + sin(pb1)*0.10 + sin(pb2)*0.08) * envB;
        float hC = (sin(pc0)*0.048+ sin(pc1)*0.038+ sin(pc2)*0.028) * envC;

        float h = hA + hB + hC;

        // Shore attenuation + open-water amplitude boost.
        float waveMask = smoothstep(0.0, 1.0, aShoreDistance) * 2.3;

        h *= waveMask;

        // Partial derivatives for normals (envelope treated as locally constant)
        float dhdx =
            envA*(cos(pa0)*0.110*0.30 + cos(pa2)*0.078*0.18 + cos(pa3)*(-0.048)*0.14) +
            envB*(cos(pb0)*0.530*0.12 + cos(pb1)*(-0.245)*0.10 + cos(pb2)*0.375*0.08) +
            envC*(cos(pc0)*1.340*0.048+ cos(pc1)*(-0.850)*0.038+ cos(pc2)*1.610*0.028);

        float dhdz =
            envA*(cos(pa1)*0.135*0.25 + cos(pa2)*0.082*0.18 + cos(pa3)*0.112*0.14) +
            envB*(cos(pb0)*0.185*0.12 + cos(pb1)*0.555*0.10 + cos(pb2)*(-0.428)*0.08) +
            envC*(cos(pc0)*0.730*0.048+ cos(pc1)*1.470*0.038+ cos(pc2)*(-0.550)*0.028);

        dhdx *= waveMask;
        dhdz *= waveMask;

        WaveHeight = h;
        pos.y     += h;
        WaveNorm   = normalize(vec3(-dhdx, 1.0, -dhdz));
    } else {
        WaveHeight = 0.0;
        WaveNorm   = aNormal;
    }

    vec4 worldPos4  = model * vec4(pos, 1.0);
    WorldPos        = worldPos4.xyz;
    TexCoord        = aTexCoord;
    SkyLight        = aSkyLight;
    BlockLight      = aBlockLight;
    FaceNormal      = aNormal;
    ShoreDist       = aShoreDistance;

    v_reflClipPos = u_reflProjView * worldPos4;
    gl_Position   = projection * view * worldPos4;
}
