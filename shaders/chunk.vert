#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aTexCoord;
layout(location = 3) in float aMaterialID;
layout(location = 4) in float aSkyLight;
layout(location = 5) in float aBlockLight;
layout(location = 7) in float aSnowable;    // 1.0 = top of a roof/chimney, accepts snow
layout(location = 9) in float aAO;          // per-vertex ambient occlusion (1 = open)

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform vec4 u_clipPlane;   // (0,0,0,1) = no clip; (0,1,0,-waterY) = clip below water

out vec2  TexCoord;
out float SkyLight;
out float BlockLight;
out vec3  FragWorldPos;
out vec3  FragNormal;
out vec4  FragPosLightSpace;
out float Snowable;
out float MaterialID;
out float AO;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    TexCoord          = aTexCoord;
    SkyLight          = aSkyLight;
    BlockLight        = aBlockLight;
    FragWorldPos      = worldPos.xyz;
    FragNormal        = aNormal;
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    Snowable          = aSnowable;
    MaterialID        = aMaterialID;
    AO                = aAO;

    gl_ClipDistance[0] = dot(worldPos.xyz, u_clipPlane.xyz) + u_clipPlane.w;
    gl_Position        = projection * view * worldPos;
}
