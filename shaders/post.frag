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
uniform float u_viewDist;   // chunk-load radius in blocks (0 disables the fog)
uniform vec3  u_fogColor;   // far-fog colour, gamma space (matches the horizon)

// Interleaved-gradient noise: a cheap per-pixel dither that hides the banding
// from a low step-count ray march.
float ign(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 scene = texture(u_scene, vUV).rgb;

    // Reconstruct this pixel's world position and camera distance from depth —
    // shared by the light shafts and the distance fog below.
    float depth  = texture(u_depth, vUV).r;
    vec4  clip   = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4  world  = u_invViewProj * clip;
    world.xyz   /= world.w;
    vec3  ray    = world.xyz - u_camPos;
    float rayLen = length(ray);
    vec3  rayDir = ray / max(rayLen, 1e-4);

    // Volumetric light shafts: march the camera ray and accumulate how much of
    // it stands in sunlight, sampling the sun's shadow map so trees carve the
    // shafts. Skipped entirely at night / under heavy overcast.
    if (u_rayStrength > 0.001) {
        const int STEPS = 28;
        float marchLen  = min(rayLen, 130.0);
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
        float phase = pow(vd, 4.5) * 0.92 + 0.12;   // broader, brighter shafts

        scene += u_rayColor * (lit * phase * u_rayStrength * 1.20);
    }

    // Distance fog: fade solid geometry into the horizon colour as it approaches
    // the chunk-load radius, so newly streamed terrain is hidden in fog rather
    // than popping into view. Sky / far-plane pixels (depth ~1) are skipped so
    // the sky stays crisp and the fogged terrain blends into it.
    if (u_viewDist > 1.0 && depth < 0.9999) {
        float fog = smoothstep(u_viewDist * 0.55, u_viewDist * 0.90, rayLen);
        scene = mix(scene, u_fogColor, fog);
    }

    // Global contrast + a touch of saturation for a punchier image. Applied to
    // the composited scene, before the vignette frames it.
    scene = (scene - 0.5) * 1.16 + 0.5;
    float lum = dot(scene, vec3(0.299, 0.587, 0.114));
    scene = clamp(mix(vec3(lum), scene, 1.10), 0.0, 1.0);

    // Vignette — pulls the corners down for a moodier frame.
    float r = length((vUV - 0.5) * vec2(1.06, 1.0));
    scene *= mix(1.0, smoothstep(1.05, 0.30, r), 0.55);

    FragColor = vec4(scene, 1.0);
}
