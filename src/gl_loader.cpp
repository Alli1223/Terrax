#include "gl_loader.h"
#include <iostream>

PFN_glGenBuffers             glGenBuffers             = nullptr;
PFN_glBindBuffer             glBindBuffer             = nullptr;
PFN_glBufferData             glBufferData             = nullptr;
PFN_glDeleteBuffers          glDeleteBuffers          = nullptr;
PFN_glGenVertexArrays        glGenVertexArrays        = nullptr;
PFN_glBindVertexArray        glBindVertexArray        = nullptr;
PFN_glDeleteVertexArrays     glDeleteVertexArrays     = nullptr;
PFN_glVertexAttribPointer    glVertexAttribPointer    = nullptr;
PFN_glEnableVertexAttribArray glEnableVertexAttribArray = nullptr;
PFN_glCreateShader           glCreateShader           = nullptr;
PFN_glShaderSource           glShaderSource           = nullptr;
PFN_glCompileShader          glCompileShader          = nullptr;
PFN_glGetShaderiv            glGetShaderiv            = nullptr;
PFN_glGetShaderInfoLog       glGetShaderInfoLog       = nullptr;
PFN_glDeleteShader           glDeleteShader           = nullptr;
PFN_glCreateProgram          glCreateProgram          = nullptr;
PFN_glAttachShader           glAttachShader           = nullptr;
PFN_glLinkProgram            glLinkProgram            = nullptr;
PFN_glGetProgramiv           glGetProgramiv           = nullptr;
PFN_glGetProgramInfoLog      glGetProgramInfoLog      = nullptr;
PFN_glUseProgram             glUseProgram             = nullptr;
PFN_glDeleteProgram          glDeleteProgram          = nullptr;
PFN_glGetUniformLocation     glGetUniformLocation     = nullptr;
PFN_glUniform1i              glUniform1i              = nullptr;
PFN_glUniform1f              glUniform1f              = nullptr;
PFN_glUniform1fv             glUniform1fv             = nullptr;
PFN_glUniform3fv             glUniform3fv             = nullptr;
PFN_glUniformMatrix4fv       glUniformMatrix4fv       = nullptr;
PFN_glGenTextures            glGenTextures            = nullptr;
PFN_glDeleteTextures         glDeleteTextures         = nullptr;
PFN_glBindTexture            glBindTexture            = nullptr;
PFN_glTexImage2D             glTexImage2D             = nullptr;
PFN_glTexParameteri          glTexParameteri          = nullptr;
PFN_glTexParameterfv         glTexParameterfv         = nullptr;
PFN_glActiveTexture          glActiveTexture          = nullptr;
PFN_glGenerateMipmap         glGenerateMipmap         = nullptr;
PFN_glGenFramebuffers        glGenFramebuffers        = nullptr;
PFN_glDeleteFramebuffers     glDeleteFramebuffers     = nullptr;
PFN_glBindFramebuffer        glBindFramebuffer        = nullptr;
PFN_glFramebufferTexture2D   glFramebufferTexture2D   = nullptr;
PFN_glDrawBuffer             glDrawBuffer             = nullptr;
PFN_glReadBuffer             glReadBuffer             = nullptr;
PFN_glUniform4fv             glUniform4fv             = nullptr;
PFN_glGenRenderbuffers       glGenRenderbuffers       = nullptr;
PFN_glDeleteRenderbuffers    glDeleteRenderbuffers    = nullptr;
PFN_glBindRenderbuffer       glBindRenderbuffer       = nullptr;
PFN_glRenderbufferStorage    glRenderbufferStorage    = nullptr;
PFN_glFramebufferRenderbuffer glFramebufferRenderbuffer = nullptr;

#define LOAD(name) \
    name = (PFN_##name)glfwGetProcAddress(#name); \
    if (!name) { std::cerr << "Missing GL function: " #name "\n"; ok = false; }

bool gl_load() {
    bool ok = true;
    LOAD(glGenBuffers)
    LOAD(glBindBuffer)
    LOAD(glBufferData)
    LOAD(glDeleteBuffers)
    LOAD(glGenVertexArrays)
    LOAD(glBindVertexArray)
    LOAD(glDeleteVertexArrays)
    LOAD(glVertexAttribPointer)
    LOAD(glEnableVertexAttribArray)
    LOAD(glCreateShader)
    LOAD(glShaderSource)
    LOAD(glCompileShader)
    LOAD(glGetShaderiv)
    LOAD(glGetShaderInfoLog)
    LOAD(glDeleteShader)
    LOAD(glCreateProgram)
    LOAD(glAttachShader)
    LOAD(glLinkProgram)
    LOAD(glGetProgramiv)
    LOAD(glGetProgramInfoLog)
    LOAD(glUseProgram)
    LOAD(glDeleteProgram)
    LOAD(glGetUniformLocation)
    LOAD(glUniform1i)
    LOAD(glUniform1f)
    LOAD(glUniform1fv)
    LOAD(glUniform3fv)
    LOAD(glUniformMatrix4fv)
    LOAD(glGenTextures)
    LOAD(glDeleteTextures)
    LOAD(glBindTexture)
    LOAD(glTexImage2D)
    LOAD(glTexParameteri)
    LOAD(glTexParameterfv)
    LOAD(glActiveTexture)
    LOAD(glGenerateMipmap)
    LOAD(glGenFramebuffers)
    LOAD(glDeleteFramebuffers)
    LOAD(glBindFramebuffer)
    LOAD(glFramebufferTexture2D)
    LOAD(glDrawBuffer)
    LOAD(glReadBuffer)
    LOAD(glUniform4fv)
    LOAD(glGenRenderbuffers)
    LOAD(glDeleteRenderbuffers)
    LOAD(glBindRenderbuffer)
    LOAD(glRenderbufferStorage)
    LOAD(glFramebufferRenderbuffer)
    return ok;
}
