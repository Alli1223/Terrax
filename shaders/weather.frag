#version 330 core

in vec2  vUV;
in float vAlpha;
in float vViewDist;

out vec4 FragColor;

uniform int  u_kind;   // 0 = rain, 1 = snow
uniform vec3 u_tint;

void main() {
    float a = vAlpha;
    if (u_kind == 1) {
        // Soft round snowflake.
        float d = length(vUV - vec2(0.5));
        a *= smoothstep(0.50, 0.04, d);
    } else {
        // Rain streak: thin across its width, tapered at both ends.
        float across = smoothstep(0.50, 0.16, abs(vUV.x - 0.5));
        float along  = smoothstep(0.0, 0.22, vUV.y) * smoothstep(1.0, 0.74, vUV.y);
        a *= across * along;
    }
    // Fade the very nearest particles (avoid a flake filling the screen) and
    // dissolve the farthest ones into the distance.
    a *= clamp((vViewDist - 1.5) / 3.0, 0.0, 1.0);
    a *= clamp(1.0 - (vViewDist - 30.0) / 16.0, 0.0, 1.0);
    if (a < 0.012) discard;
    FragColor = vec4(u_tint, a);
}
