#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aTexCoord;
layout(location = 3) in float aMaterialID;
layout(location = 4) in float aSkyLight;
layout(location = 5) in float aBlockLight;

uniform mat4  model;
uniform mat4  view;
uniform mat4  projection;
uniform float time;

out vec2  TexCoord;
out float SkyLight;
out float BlockLight;
out vec3  WorldPos;
out vec3  WaveNorm;

void main() {
    vec3 pos = aPos;

    // Gentle vertex wave on top faces only (normal.y > 0.5)
    if (aNormal.y > 0.5) {
        float w = sin(pos.x * 1.8 + time * 1.2) * 0.05
                + sin(pos.z * 2.1 + time * 0.9) * 0.04;
        pos.y += w;
    }

    TexCoord   = aTexCoord;
    SkyLight   = aSkyLight;
    BlockLight = aBlockLight;
    WorldPos   = (model * vec4(pos, 1.0)).xyz;
    WaveNorm   = aNormal;

    gl_Position = projection * view * model * vec4(pos, 1.0);
}
