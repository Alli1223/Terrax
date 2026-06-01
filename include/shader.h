#pragma once
#include "gl_loader.h"
#include <glm/glm.hpp>
#include <string>

class Shader {
public:
    GLuint id;
    Shader() : id(0) {}
    Shader(const char* vertPath, const char* fragPath);
    void use() const { glUseProgram(id); }
    void setInt(const char* name, int v) const   { glUniform1i(glGetUniformLocation(id, name), v); }
    void setFloat(const char* name, float v) const { glUniform1f(glGetUniformLocation(id, name), v); }
    void setVec3(const char* name, const glm::vec3& v) const { glUniform3fv(glGetUniformLocation(id, name), 1, &v[0]); }
    void setVec4(const char* name, const glm::vec4& v) const { glUniform4fv(glGetUniformLocation(id, name), 1, &v[0]); }
    void setMat4(const char* name, const glm::mat4& m) const { glUniformMatrix4fv(glGetUniformLocation(id, name), 1, GL_FALSE, &m[0][0]); }
    void setLanternLights(int count, const glm::vec3* pos, const float* intensity,
                          const float* radius, const glm::vec3* color) const;
};
