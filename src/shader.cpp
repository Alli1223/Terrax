#include "shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

static GLuint compileShader(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        std::cerr << "Shader compile error:\n" << log << "\n";
    }
    return s;
}

static std::string readFile(const char* path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open " << path << "\n"; return ""; }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Shader::Shader(const char* vertPath, const char* fragPath) {
    std::string vs = readFile(vertPath);
    std::string fs = readFile(fragPath);
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    id = glCreateProgram();
    glAttachShader(id, v);
    glAttachShader(id, f);
    glLinkProgram(id);
    GLint ok;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(id, 1024, nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
    }
    glDeleteShader(v);
    glDeleteShader(f);
}

void Shader::setLanternLights(int count, const glm::vec3* pos, const float* intensity,
                              const float* radius, const glm::vec3* color) const {
    glUniform1i(glGetUniformLocation(id, "u_lanternCount"), count);
    if (count <= 0) return;
    glUniform3fv(glGetUniformLocation(id, "u_lanternPos"),       count, &pos[0][0]);
    glUniform1fv(glGetUniformLocation(id, "u_lanternIntensity"), count, intensity);
    glUniform1fv(glGetUniformLocation(id, "u_lanternRadius"),    count, radius);
    glUniform3fv(glGetUniformLocation(id, "u_lanternColor"),     count, &color[0][0]);
}
