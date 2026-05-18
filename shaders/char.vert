#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

out vec3 FragPos;
out vec3 Normal;
out vec4 Color;
out vec4 FragPosLightSpace;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos           = worldPos.xyz;
    Normal            = mat3(transpose(inverse(model))) * aNormal;
    Color             = aColor;
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position       = projection * view * worldPos;
}
