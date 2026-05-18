#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aTexCoord;
layout(location = 3) in float aMaterialID;
layout(location = 4) in float aSkyLight;
layout(location = 5) in float aBlockLight;

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
out vec4  v_reflClipPos;

// Value noise for spatially-varying wave envelope
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
        // 8 waves at varied irrational directions + speeds
        // Each row: (kx, kz, omega, amplitude)
        // Directions spread across 0-360° so wave fronts cancel/reinforce
        float ph0 = pos.x * 0.620 + pos.z * 0.000 + time * 1.15;
        float ph1 = pos.x * 0.000 + pos.z * 0.740 + time * 0.85;
        float ph2 = pos.x * 0.340 + pos.z * 0.340 + time * 1.05;
        float ph3 = pos.x * 0.715 - pos.z * 0.495 + time * 1.38;
        float ph4 =-pos.x * 0.495 + pos.z * 0.715 + time * 0.93;
        float ph5 = pos.x * 0.880 + pos.z * 0.420 + time * 1.62;
        float ph6 =-pos.x * 0.310 + pos.z * 0.860 + time * 0.74;
        float ph7 = pos.x * 0.460 - pos.z * 0.890 + time * 1.95;

        float h = sin(ph0)*0.28 + sin(ph1)*0.22 + sin(ph2)*0.16
                + sin(ph3)*0.10 + sin(ph4)*0.10 + sin(ph5)*0.07
                + sin(ph6)*0.08 + sin(ph7)*0.05;

        float dhdx = cos(ph0)*0.620*0.28 + cos(ph1)*0.000*0.22
                   + cos(ph2)*0.340*0.16 + cos(ph3)*0.715*0.10
                   + cos(ph4)*(-0.495)*0.10 + cos(ph5)*0.880*0.07
                   + cos(ph6)*(-0.310)*0.08 + cos(ph7)*0.460*0.05;

        float dhdz = cos(ph0)*0.000*0.28 + cos(ph1)*0.740*0.22
                   + cos(ph2)*0.340*0.16 + cos(ph3)*(-0.495)*0.10
                   + cos(ph4)*0.715*0.10 + cos(ph5)*0.420*0.07
                   + cos(ph6)*0.860*0.08 + cos(ph7)*(-0.890)*0.05;

        // Spatially-varying envelope: creates calmer hollows and rougher swells
        // Two noise octaves drifting at different speeds break up uniformity
        vec2 ep = pos.xz * 0.065;
        float env = 0.55
                  + 0.28 * vnoise(ep + vec2(time * 0.022,  time * 0.016))
                  + 0.17 * vnoise(ep * 2.3 + vec2(time * -0.031, time * 0.041));

        WaveHeight = h * env;
        pos.y += WaveHeight;
        WaveNorm = normalize(vec3(-dhdx * env, 1.0, -dhdz * env));
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

    v_reflClipPos = u_reflProjView * worldPos4;
    gl_Position   = projection * view * worldPos4;
}
