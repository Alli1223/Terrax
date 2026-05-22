#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D u_scene;
uniform sampler2D u_depth;
uniform sampler2D u_shadowMap;
uniform mat4  u_invViewProj;
uniform mat4  u_lightSpace;
uniform vec3  u_camPos;
uniform vec3  u_sunDir;
uniform vec3  u_rayColor;
uniform float u_rayStrength;

// Interleaved-gradient noise: a cheap per-pixel dither that hides the banding
// from a low step-count ray march.
float ign(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 scene = texture(u_scene, vUV).rgb;

    // Volumetric light shafts: march the camera ray and accumulate how much of
    // it stands in sunlight, sampling the sun's shadow map so trees carve the
    // shafts. Skipped entirely at night / under heavy overcast.
    if (u_rayStrength > 0.001) {
        float depth = texture(u_depth, vUV).r;

        vec4 clip  = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
        vec4 world = u_invViewProj * clip;
        world.xyz /= world.w;

        vec3  ray    = world.xyz - u_camPos;
        float rayLen = length(ray);
        vec3  rayDir = ray / max(rayLen, 1e-4);

        const int STEPS = 20;
        float marchLen  = min(rayLen, 88.0);
        float stepLen   = marchLen / float(STEPS);
        float t         = stepLen * ign(gl_FragCoord.xy);

        float lit = 0.0;
        for (int i = 0; i < STEPS; i++) {
            vec3 p  = u_camPos + rayDir * t;
            vec4 ls = u_lightSpace * vec4(p, 1.0);
            vec3 sp = ls.xyz / ls.w * 0.5 + 0.5;
            if (sp.x > 0.0 && sp.x < 1.0 && sp.y > 0.0 && sp.y < 1.0 && sp.z < 1.0) {
                float sm = texture(u_shadowMap, sp.xy).r;
                lit += (sp.z - 0.0012 > sm) ? 0.0 : 1.0;
            } else {
                lit += 1.0;   // outside the shadow map — treat as open sky
            }
            t += stepLen;
        }
        lit /= float(STEPS);

        // Forward scattering: shafts blaze when looking toward the sun, with a
        // gentle floor so lit air still glows a little off-axis.
        float vd    = max(dot(rayDir, u_sunDir), 0.0);
        float phase = pow(vd, 6.0) * 0.90 + 0.10;

        scene += u_rayColor * (lit * phase * u_rayStrength * 0.85);
    }

    // Vignette — pulls the corners down for a moodier frame.
    float r = length((vUV - 0.5) * vec2(1.06, 1.0));
    scene *= mix(1.0, smoothstep(1.05, 0.30, r), 0.55);

    FragColor = vec4(scene, 1.0);
}
