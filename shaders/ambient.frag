#version 330 core

in vec3  vColor;
in vec2  vUV;
in float vAlpha;
in float vViewDist;

out vec4 FragColor;

void main() {
    // Soft round particle.
    float d = length(vUV - vec2(0.5));
    float a = vAlpha * smoothstep(0.50, 0.05, d);
    // Ease the very nearest and farthest particles out.
    a *= clamp((vViewDist - 0.8) / 1.4, 0.0, 1.0);
    a *= clamp(1.0 - (vViewDist - 22.0) / 8.0, 0.0, 1.0);
    if (a < 0.012) discard;
    // Output (colour, alpha) — paired with additive blend (SRC_ALPHA, ONE),
    // GL adds colour*alpha to the framebuffer, giving a soft emissive glow.
    FragColor = vec4(vColor, a);
}
