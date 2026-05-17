#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec4 Color;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 skyAmbient;

void main() {
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, normalize(lightDir)), 0.0);
    vec3 lighting = skyAmbient + diff * lightColor;
    FragColor = vec4(Color.rgb * lighting, Color.a);
}
