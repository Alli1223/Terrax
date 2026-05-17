#pragma once
// Minimal OpenGL 3.3 core loader using glfwGetProcAddress.
// Works on X11 and Wayland (no GLEW/GLX required).

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stddef.h>

// --- GL base types ---
typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef double         GLdouble;
typedef ptrdiff_t      GLsizeiptr;
typedef ptrdiff_t      GLintptr;
typedef char           GLchar;
typedef unsigned char  GLubyte;

// --- GL constants ---
#define GL_FALSE                0
#define GL_TRUE                 1
#define GL_TEXTURE0             0x84C0
#define GL_RGBA8                0x8058
#define GL_TRIANGLES            0x0004
#define GL_LINES                0x0001
#define GL_DEPTH_BUFFER_BIT     0x00000100
#define GL_COLOR_BUFFER_BIT     0x00004000
#define GL_FLOAT                0x1406
#define GL_UNSIGNED_BYTE        0x1401
#define GL_DEPTH_TEST           0x0B71
#define GL_CULL_FACE            0x0B44
#define GL_BACK                 0x0405
#define GL_FRONT                0x0404
#define GL_FRONT_AND_BACK       0x0408
#define GL_MULTISAMPLE          0x809D
#define GL_LESS                 0x0201
#define GL_LINEAR               0x2601
#define GL_NEAREST              0x2600
#define GL_ARRAY_BUFFER         0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW          0x88E4
#define GL_DYNAMIC_DRAW         0x88E8
#define GL_VERTEX_SHADER        0x8B31
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_INFO_LOG_LENGTH      0x8B84
#define GL_TEXTURE_2D           0x0DE1
#define GL_RGBA                 0x1908
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_BLEND                0x0BE2
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303

// --- GL 1.x functions (direct symbols in libGL) ---
extern "C" {
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glClear(GLbitfield mask);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glDepthMask(GLboolean flag);
void glCullFace(GLenum mode);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glLineWidth(GLfloat width);
void glPolygonMode(GLenum face, GLenum mode);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
}

// --- GL 1.5+ function pointer typedefs (loaded at runtime) ---
typedef void   (*PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void   (*PFN_glBindBuffer)(GLenum, GLuint);
typedef void   (*PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void   (*PFN_glDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (*PFN_glGenVertexArrays)(GLsizei, GLuint*);
typedef void   (*PFN_glBindVertexArray)(GLuint);
typedef void   (*PFN_glDeleteVertexArrays)(GLsizei, const GLuint*);
typedef void   (*PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (*PFN_glEnableVertexAttribArray)(GLuint);
typedef GLuint (*PFN_glCreateShader)(GLenum);
typedef void   (*PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (*PFN_glCompileShader)(GLuint);
typedef void   (*PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (*PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (*PFN_glDeleteShader)(GLuint);
typedef GLuint (*PFN_glCreateProgram)(void);
typedef void   (*PFN_glAttachShader)(GLuint, GLuint);
typedef void   (*PFN_glLinkProgram)(GLuint);
typedef void   (*PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (*PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (*PFN_glUseProgram)(GLuint);
typedef void   (*PFN_glDeleteProgram)(GLuint);
typedef GLint  (*PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void   (*PFN_glUniform1i)(GLint, GLint);
typedef void   (*PFN_glUniform1f)(GLint, GLfloat);
typedef void   (*PFN_glUniform3fv)(GLint, GLsizei, const GLfloat*);
typedef void   (*PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (*PFN_glGenTextures)(GLsizei, GLuint*);
typedef void   (*PFN_glDeleteTextures)(GLsizei, const GLuint*);
typedef void   (*PFN_glBindTexture)(GLenum, GLuint);
typedef void   (*PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void   (*PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef void   (*PFN_glActiveTexture)(GLenum);
typedef void   (*PFN_glGenerateMipmap)(GLenum);

// --- Function pointer declarations ---
extern PFN_glGenBuffers             glGenBuffers;
extern PFN_glBindBuffer             glBindBuffer;
extern PFN_glBufferData             glBufferData;
extern PFN_glDeleteBuffers          glDeleteBuffers;
extern PFN_glGenVertexArrays        glGenVertexArrays;
extern PFN_glBindVertexArray        glBindVertexArray;
extern PFN_glDeleteVertexArrays     glDeleteVertexArrays;
extern PFN_glVertexAttribPointer    glVertexAttribPointer;
extern PFN_glEnableVertexAttribArray glEnableVertexAttribArray;
extern PFN_glCreateShader           glCreateShader;
extern PFN_glShaderSource           glShaderSource;
extern PFN_glCompileShader          glCompileShader;
extern PFN_glGetShaderiv            glGetShaderiv;
extern PFN_glGetShaderInfoLog       glGetShaderInfoLog;
extern PFN_glDeleteShader           glDeleteShader;
extern PFN_glCreateProgram          glCreateProgram;
extern PFN_glAttachShader           glAttachShader;
extern PFN_glLinkProgram            glLinkProgram;
extern PFN_glGetProgramiv           glGetProgramiv;
extern PFN_glGetProgramInfoLog      glGetProgramInfoLog;
extern PFN_glUseProgram             glUseProgram;
extern PFN_glDeleteProgram          glDeleteProgram;
extern PFN_glGetUniformLocation     glGetUniformLocation;
extern PFN_glUniform1i              glUniform1i;
extern PFN_glUniform1f              glUniform1f;
extern PFN_glUniform3fv             glUniform3fv;
extern PFN_glUniformMatrix4fv       glUniformMatrix4fv;
extern PFN_glGenTextures            glGenTextures;
extern PFN_glDeleteTextures         glDeleteTextures;
extern PFN_glBindTexture            glBindTexture;
extern PFN_glTexImage2D             glTexImage2D;
extern PFN_glTexParameteri          glTexParameteri;
extern PFN_glActiveTexture          glActiveTexture;
extern PFN_glGenerateMipmap         glGenerateMipmap;

// Call once after glfwMakeContextCurrent
bool gl_load();
