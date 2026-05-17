#version 330 core

in vec2  TexCoord;
in float SkyLight;
in float BlockLight;
in vec3  WorldPos;
in vec3  WaveNorm;

out vec4 FragColor;

uniform sampler2D atlas;
uniform float sunFactor;
uniform vec3  skyAmbient;
uniform vec3  camPos;
uniform float time;
uniform float timeOfDay;

const float PI = 3.14159265359;

void main() {
    vec3 base = texture(atlas, TexCoord).rgb;

    // Animated surface normal from two overlapping sine waves
    float nx = sin(WorldPos.x * 2.8 + time * 1.4) * 0.22
             + sin(WorldPos.z * 1.9 + time * 0.8) * 0.14;
    float nz = sin(WorldPos.z * 2.4 + time * 1.1) * 0.22
             + sin(WorldPos.x * 1.6 + time * 1.3) * 0.14;
    vec3 wNorm = normalize(WaveNorm + vec3(nx, 0.0, nz));

    vec3 viewDir = normalize(camPos - WorldPos);

    // Fresnel (Schlick): shallow reflection at near-normal, strong at grazing
    float cosTheta = max(dot(viewDir, wNorm), 0.0);
    float fresnel   = 0.02 + 0.98 * pow(1.0 - cosTheta, 5.0);
    fresnel = clamp(fresnel, 0.0, 0.55);

    // Sun specular highlight
    float sunAngle = (timeOfDay - 0.25) * 2.0 * PI;
    vec3  sunDir   = normalize(vec3(cos(sunAngle), sin(sunAngle), 0.25));
    vec3  reflDir  = reflect(-sunDir, wNorm);
    float spec     = pow(max(dot(viewDir, reflDir), 0.0), 140.0) * sunFactor;

    // Lighting (same model as chunk shader)
    vec3 skyContrib   = SkyLight   * sunFactor * skyAmbient;
    vec3 blockContrib = BlockLight * vec3(1.00, 0.76, 0.40);
    vec3 light = max(skyContrib, blockContrib);
    light = max(light, vec3(0.015, 0.014, 0.020));

    // Water colour: blend atlas blue-teal tile with deep-water colour
    vec3 deepWater  = vec3(0.03, 0.14, 0.30) * light;
    vec3 waterColor = mix(base * light, deepWater, 0.55);

    // Add sun specular and fresnel sky-reflection tint
    waterColor += spec * skyAmbient * 0.75;
    waterColor  = mix(waterColor, skyAmbient * max(sunFactor, 0.15), fresnel * 0.38);

    // Distance fog matching chunk.frag
    float dist    = length(camPos - WorldPos);
    float fogDist = max(dist - 32.0, 0.0);
    float fog     = exp(-fogDist * 0.0018);
    vec3  fogCol  = skyAmbient * max(sunFactor, 0.15) * 0.85;
    waterColor = mix(fogCol, waterColor, clamp(fog, 0.0, 1.0));

    waterColor = pow(max(waterColor, vec3(0.0)), vec3(1.0 / 2.2));

    float alpha = 0.68 + fresnel * 0.22;
    FragColor = vec4(waterColor, alpha);
}
