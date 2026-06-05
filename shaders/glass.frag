#version 330 core

in vec2  TexCoord;
in float SkyLight;
in float BlockLight;
in vec3  FragWorldPos;
in vec3  FragNormal;

out vec4 FragColor;

uniform sampler2D atlas;
uniform float sunFactor;
uniform vec3  skyAmbient;
uniform vec3  camPos;
uniform vec3  u_sunDir;
uniform float u_weather;   // 0 = clear .. 1 = full storm

void main() {
    vec3 tint = texture(atlas, TexCoord).rgb;
    vec3 N = normalize(FragNormal);
    vec3 V = normalize(camPos - FragWorldPos);

    // Diffuse + ambient lighting — kept light, glass is mostly transparent.
    float NdotL   = max(dot(N, u_sunDir), 0.0);
    float diffuse = smoothstep(0.05, 0.6, NdotL);
    vec3  skyAmb  = SkyLight * sunFactor * skyAmbient * 0.32;
    vec3  sunC    = diffuse * sunFactor * skyAmbient * 0.55;
    vec3  blockC  = BlockLight * vec3(1.00, 0.76, 0.40) * 0.9;
    vec3  light   = max(skyAmb + sunC, blockC);
    light = max(light, vec3(0.06));

    vec3 result = tint * light;

    // Sharp sun glint off the surface.
    vec3  H    = normalize(u_sunDir + V);
    float spec = pow(max(dot(N, H), 0.0), 80.0) * sunFactor;
    result += vec3(spec);

    // Fresnel: glass reads more solid and brighter at grazing angles.
    float fres  = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    result += skyAmbient * fres * (0.25 * sunFactor + 0.05);
    float alpha = mix(0.32, 0.85, fres);

    // Lit windows: as dusk falls the panes glow a warm amber, as if lamps burn
    // within, so a town sparkles at night seen from outside. It ramps off the
    // sun (invisible by day) and stands the glass up more opaque so it reads.
    float nightAmt = 1.0 - smoothstep(0.0, 0.22, sunFactor);
    result += vec3(1.00, 0.73, 0.42) * nightAmt * 0.55;
    alpha   = max(alpha, nightAmt * 0.80);

    // Atmospheric fog — keep glass consistent with the terrain haze.
    float fogDist  = length(FragWorldPos - camPos);
    float fogStart = mix(26.0, 10.0, u_weather);
    float fogDens  = mix(0.0030, 0.0125, u_weather);
    float fog      = exp(-max(fogDist - fogStart, 0.0) * fogDens);
    vec3  fogCol   = skyAmbient * max(sunFactor, 0.12) * mix(0.90, 0.72, u_weather);
    result = mix(fogCol, result, clamp(fog, 0.0, 1.0));

    result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(result, alpha);
}
