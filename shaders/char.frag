#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec4 Color;
in vec4 FragPosLightSpace;

out vec4 FragColor;

uniform vec3  u_sunDir;
uniform float sunFactor;
uniform vec3  skyAmbient;
uniform sampler2D shadowMap;
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
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float closestDepth = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += (proj.z - bias > closestDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec3 norm   = normalize(Normal);
    float NdotL = max(dot(norm, u_sunDir), 0.0);
    float diffuse = smoothstep(0.05, 0.55, NdotL);

    float shadowFade = clamp(sunFactor * 3.0 - 0.2, 0.0, 1.0);
    float shadow = calcShadow(FragPosLightSpace, NdotL) * shadowFade;

    vec3 skyAmb    = sunFactor * skyAmbient * 0.28;
    vec3 sunContrib = diffuse * (1.0 - shadow * 0.82) * sunFactor * skyAmbient * 0.95;

    float ldist   = length(FragPos - u_lanternPos);
    float falloff = max(0.0, 1.0 - ldist / u_lanternRadius);
    falloff = falloff * falloff;
    vec3 lanternContrib = falloff * u_lanternIntensity * vec3(1.00, 0.76, 0.40);

    vec3 light = skyAmb + sunContrib;
    light = max(light, lanternContrib);
    light = max(light, vec3(0.013, 0.011, 0.016));

    vec3 result = Color.rgb * light;
    result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(result, Color.a);
}
