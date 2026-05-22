#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 2) in vec2  aUV;
layout(location = 4) in float aAlpha;

uniform mat4 view;
uniform mat4 projection;

out vec2  vUV;
out float vAlpha;
out float vViewDist;

void main() {
    vec4 viewPos = view * vec4(aPos, 1.0);
    vUV         = aUV;
    vAlpha      = aAlpha;
    vViewDist   = length(viewPos.xyz);
    gl_Position = projection * viewPos;
}
