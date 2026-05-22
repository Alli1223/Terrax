#version 330 core

// Vertex packing (reuses the chunk Vertex layout):
//   loc 0: position
//   loc 1: per-vertex colour (packed into the normal slot)
//   loc 2: billboard UV
//   loc 4: per-vertex alpha (packed into skyLight)

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aColor;
layout(location = 2) in vec2  aUV;
layout(location = 4) in float aAlpha;

uniform mat4 view;
uniform mat4 projection;

out vec3  vColor;
out vec2  vUV;
out float vAlpha;
out float vViewDist;

void main() {
    vec4 viewPos = view * vec4(aPos, 1.0);
    vColor    = aColor;
    vUV       = aUV;
    vAlpha    = aAlpha;
    vViewDist = length(viewPos.xyz);
    gl_Position = projection * viewPos;
}
