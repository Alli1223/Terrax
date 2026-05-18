#version 330 core

in vec2  TexCoord;
in float SkyLight;
in float BlockLight;
in vec3  FragWorldPos;
in vec3  FragNormal;
in vec4  FragPosLightSpace;

out vec4 FragColor;

uniform sampler2D atlas;
uniform sampler2D shadowMap;
uniform float sunFactor;
uniform vec3  skyAmbient;
uniform vec3  camPos;
uniform vec3  u_sunDir;
uniform vec3  u_lanternPos;
uniform float u_lanternIntensity;
uniform float u_lanternRadius;

float calcShadow(vec4 fragPosLS, float NdotL) {
    vec3 proj = fragPosLS.xyz / fragPosLS.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;

    float bias = max(0.006 * (1.0 - NdotL), 0.0015);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    // 3x3 PCF kernel for soft shadows
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float closestDepth = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += (proj.z - bias > closestDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec4 texSample = texture(atlas, TexCoord);
    if (texSample.a < 0.5) discard;
    vec3 base = texSample.rgb;

    // Directional sun: NdotL with soft ramp
    float NdotL   = max(dot(FragNormal, u_sunDir), 0.0);
    float diffuse = smoothstep(0.05, 0.55, NdotL);

    // Shadow (only darkens the direct sun contribution)
    float shadowFade = clamp(sunFactor * 3.0 - 0.2, 0.0, 1.0);
    float shadow = calcShadow(FragPosLightSpace, NdotL) * shadowFade;

    // Sky ambient (indirect light, not shadowed)
    vec3 skyAmb = SkyLight * sunFactor * skyAmbient * 0.22;

    // Direct sun, attenuated by shadow
    vec3 sunContrib = diffuse * (1.0 - shadow * 0.82) * sunFactor * skyAmbient * 0.95;

    // Warm block / lantern light
    vec3 blockContrib = BlockLight * vec3(1.00, 0.76, 0.40) * 0.9;
    float ldist   = length(FragWorldPos - u_lanternPos);
    float falloff = max(0.0, 1.0 - ldist / u_lanternRadius);
    falloff = falloff * falloff;
    vec3 lanternContrib = falloff * u_lanternIntensity * vec3(1.00, 0.76, 0.40);

    vec3 light = skyAmb + sunContrib;
    light = max(light, blockContrib);
    light = max(light, lanternContrib);
    light = max(light, vec3(0.013, 0.011, 0.016));

    vec3 result = base * light;

    // Slight saturation boost (cartoonish punch)
    float lum = dot(result, vec3(0.299, 0.587, 0.114));
    result = mix(vec3(lum), result, 1.15);

    // Atmospheric fog
    float dist    = length(FragWorldPos - camPos);
    float fogDist = max(dist - 32.0, 0.0);
    float fog     = exp(-fogDist * 0.0018);
    vec3  fogCol  = skyAmbient * max(sunFactor, 0.15) * 0.85;
    result = mix(fogCol, result, clamp(fog, 0.0, 1.0));

    // Gamma correction (no Reinhard — avoids washed-out look)
    result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(result, 1.0);
}
