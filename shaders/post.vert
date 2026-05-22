#version 330 core

out vec2 vUV;

void main() {
    // Fullscreen triangle generated from gl_VertexID — no vertex buffer needed.
    vUV = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
