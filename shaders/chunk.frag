#version 330 core

in vec2  TexCoord;
in float SkyLight;
in float BlockLight;
in vec3  FragWorldPos;

out vec4 FragColor;

uniform sampler2D atlas;
uniform float sunFactor;
uniform vec3  skyAmbient;
uniform vec3  camPos;

void main() {
    vec3 base = texture(atlas, TexCoord).rgb;

    vec3 skyContrib   = SkyLight   * sunFactor * skyAmbient;
    vec3 blockContrib = BlockLight * vec3(1.00, 0.76, 0.40);
    vec3 light = max(skyContrib, blockContrib);
    light = max(light, vec3(0.020, 0.018, 0.025));

    vec3 result = base * light;

    // Atmospheric distance fog: blends into sky colour at range
    float dist    = length(FragWorldPos - camPos);
    float fogDist = max(dist - 32.0, 0.0);
    float fog     = exp(-fogDist * 0.0018);
    vec3  fogCol  = skyAmbient * max(sunFactor, 0.15) * 0.85;
    result = mix(fogCol, result, clamp(fog, 0.0, 1.0));

    result = pow(max(result, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(result, 1.0);
}
