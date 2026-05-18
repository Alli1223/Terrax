#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aTexCoord;
layout(location = 3) in float aMaterialID;
layout(location = 4) in float aSkyLight;
layout(location = 5) in float aBlockLight;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

out vec2  TexCoord;
out float SkyLight;
out float BlockLight;
out vec3  FragWorldPos;
out vec3  FragNormal;
out vec4  FragPosLightSpace;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    TexCoord          = aTexCoord;
    SkyLight          = aSkyLight;
    BlockLight        = aBlockLight;
    FragWorldPos      = worldPos.xyz;
    FragNormal        = aNormal;
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position       = projection * view * worldPos;
}
