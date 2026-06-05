#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

// Wind sway — 0 leaves the model rigid (players, NPCs, furniture); a small
// positive value (set per-bush by the renderer) makes the model bend in the
// wind like the grass, weighted by the vertex's model-space height so the base
// stays planted and the foliage travels most.
uniform float time;
uniform float u_weather;   // 0 = calm .. 1 = storm
uniform float u_sway;      // per-object sway scale (0 = rigid)

out vec3 FragPos;
out vec3 Normal;
out vec4 Color;
out vec4 FragPosLightSpace;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    if (u_sway > 0.0) {
        float strength = (0.6 + u_weather * 1.8) * u_sway;
        vec2  windDir  = normalize(vec2(0.82, 0.30)
                       + 0.35 * vec2(sin(time * 0.07), cos(time * 0.09)));
        float phase    = dot(worldPos.xz, vec2(0.17, 0.13));
        float swell    = sin(time * 1.6 + phase) + 0.4 * sin(time * 3.3 + phase * 1.7);
        float flutter  = 0.22 * sin(time * 7.5 + phase * 2.4);
        float bend     = (swell + flutter) * strength * aPos.y;   // aPos.y = height up the model
        worldPos.xz   += windDir * bend;
    }
    FragPos           = worldPos.xyz;
    Normal            = mat3(transpose(inverse(model))) * aNormal;
    Color             = aColor;
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position       = projection * view * worldPos;
}
