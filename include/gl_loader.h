#pragma once

#ifndef TERRAX_TESTING
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// GLFW defines APIENTRY for its callback typedefs and never undefs it (it only
// records GLFW_APIENTRY_DEFINED). When Boost.Asio later pulls in <windows.h>,
// that redefines APIENTRY and warns (C4005). gl_loader uses GL_APIENTRY (below),
// not APIENTRY, so undo GLFW's leak here and let <windows.h> define it cleanly.
// Guarding on GLFW_APIENTRY_DEFINED means we only drop the definition GLFW
// itself added (no-op if <windows.h> was already included first).
#ifdef GLFW_APIENTRY_DEFINED
#undef APIENTRY
#undef GLFW_APIENTRY_DEFINED
#endif
#endif
#include <stddef.h>

#ifdef _WIN32
  #define GL_APIENTRY __stdcall
#else
  #define GL_APIENTRY
#endif

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
#define GL_CLAMP_TO_BORDER      0x812D
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_BLEND                0x0BE2
#define GL_ONE                  1
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303
#define GL_TEXTURE1             0x84C1
#define GL_TEXTURE2             0x84C2
#define GL_TEXTURE3             0x84C3
#define GL_DEPTH_COMPONENT      0x1902
#define GL_DEPTH_COMPONENT24    0x81A6
#define GL_DEPTH_ATTACHMENT     0x8D00
#define GL_FRAMEBUFFER          0x8D40
#define GL_NONE                 0
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_RENDERBUFFER         0x8D41
#define GL_RGB8                 0x8051
#define GL_RGB                  0x1907
#define GL_CLIP_DISTANCE0       0x3000
#define GL_TEXTURE_3D           0x806F
#define GL_TEXTURE_WRAP_R       0x8072
#define GL_R8                   0x8229
#define GL_RED                  0x1903

// --- GL 1.x functions (direct symbols in libGL / opengl32.lib) ---
extern "C" {
void GL_APIENTRY glEnable(GLenum cap);
void GL_APIENTRY glDisable(GLenum cap);
void GL_APIENTRY glClear(GLbitfield mask);
void GL_APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void GL_APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void GL_APIENTRY glDepthMask(GLboolean flag);
void GL_APIENTRY glCullFace(GLenum mode);
void GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count);
void GL_APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor);
void GL_APIENTRY glLineWidth(GLfloat width);
void GL_APIENTRY glPolygonMode(GLenum face, GLenum mode);
void GL_APIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
// Core 1.0 readback (used by the screenshot capture) — exported directly by
// libGL / opengl32, like the prototypes above.
void GL_APIENTRY glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                              GLenum format, GLenum type, void* pixels);
void GL_APIENTRY glPixelStorei(GLenum pname, GLint param);
}

#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05
#endif

// --- GL 1.5+ function pointer typedefs (loaded at runtime) ---
typedef void   (GL_APIENTRY *PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void   (GL_APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void   (GL_APIENTRY *PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void   (GL_APIENTRY *PFN_glDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (GL_APIENTRY *PFN_glGenVertexArrays)(GLsizei, GLuint*);
typedef void   (GL_APIENTRY *PFN_glBindVertexArray)(GLuint);
typedef void   (GL_APIENTRY *PFN_glDeleteVertexArrays)(GLsizei, const GLuint*);
typedef void   (GL_APIENTRY *PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (GL_APIENTRY *PFN_glEnableVertexAttribArray)(GLuint);
typedef GLuint (GL_APIENTRY *PFN_glCreateShader)(GLenum);
typedef void   (GL_APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (GL_APIENTRY *PFN_glCompileShader)(GLuint);
typedef void   (GL_APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (GL_APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (GL_APIENTRY *PFN_glDeleteShader)(GLuint);
typedef GLuint (GL_APIENTRY *PFN_glCreateProgram)(void);
typedef void   (GL_APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void   (GL_APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void   (GL_APIENTRY *PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (GL_APIENTRY *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (GL_APIENTRY *PFN_glUseProgram)(GLuint);
typedef void   (GL_APIENTRY *PFN_glDeleteProgram)(GLuint);
typedef GLint  (GL_APIENTRY *PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void   (GL_APIENTRY *PFN_glUniform1i)(GLint, GLint);
typedef void   (GL_APIENTRY *PFN_glUniform1f)(GLint, GLfloat);
typedef void   (GL_APIENTRY *PFN_glUniform1fv)(GLint, GLsizei, const GLfloat*);
typedef void   (GL_APIENTRY *PFN_glUniform3fv)(GLint, GLsizei, const GLfloat*);
typedef void   (GL_APIENTRY *PFN_glUniform4fv)(GLint, GLsizei, const GLfloat*);
typedef void   (GL_APIENTRY *PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (GL_APIENTRY *PFN_glGenTextures)(GLsizei, GLuint*);
typedef void   (GL_APIENTRY *PFN_glDeleteTextures)(GLsizei, const GLuint*);
typedef void   (GL_APIENTRY *PFN_glBindTexture)(GLenum, GLuint);
typedef void   (GL_APIENTRY *PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void   (GL_APIENTRY *PFN_glTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void   (GL_APIENTRY *PFN_glTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*);
typedef void   (GL_APIENTRY *PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef void   (GL_APIENTRY *PFN_glTexParameterfv)(GLenum, GLenum, const GLfloat*);
typedef void   (GL_APIENTRY *PFN_glActiveTexture)(GLenum);
typedef void   (GL_APIENTRY *PFN_glGenerateMipmap)(GLenum);
typedef void   (GL_APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void   (GL_APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void   (GL_APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void   (GL_APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void   (GL_APIENTRY *PFN_glDrawBuffer)(GLenum);
typedef void   (GL_APIENTRY *PFN_glReadBuffer)(GLenum);
typedef void   (GL_APIENTRY *PFN_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void   (GL_APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void   (GL_APIENTRY *PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void   (GL_APIENTRY *PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void   (GL_APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);

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
extern PFN_glUniform1fv             glUniform1fv;
extern PFN_glUniform3fv             glUniform3fv;
extern PFN_glUniform4fv             glUniform4fv;
extern PFN_glUniformMatrix4fv       glUniformMatrix4fv;
extern PFN_glGenTextures            glGenTextures;
extern PFN_glDeleteTextures         glDeleteTextures;
extern PFN_glBindTexture            glBindTexture;
extern PFN_glTexImage2D             glTexImage2D;
extern PFN_glTexImage3D             glTexImage3D;
extern PFN_glTexSubImage3D          glTexSubImage3D;
extern PFN_glTexParameteri          glTexParameteri;
extern PFN_glTexParameterfv         glTexParameterfv;
extern PFN_glActiveTexture          glActiveTexture;
extern PFN_glGenerateMipmap         glGenerateMipmap;
extern PFN_glGenFramebuffers        glGenFramebuffers;
extern PFN_glDeleteFramebuffers     glDeleteFramebuffers;
extern PFN_glBindFramebuffer        glBindFramebuffer;
extern PFN_glFramebufferTexture2D   glFramebufferTexture2D;
extern PFN_glDrawBuffer             glDrawBuffer;
extern PFN_glReadBuffer             glReadBuffer;
extern PFN_glGenRenderbuffers       glGenRenderbuffers;
extern PFN_glDeleteRenderbuffers    glDeleteRenderbuffers;
extern PFN_glBindRenderbuffer       glBindRenderbuffer;
extern PFN_glRenderbufferStorage    glRenderbufferStorage;
extern PFN_glFramebufferRenderbuffer glFramebufferRenderbuffer;

// Call once after glfwMakeContextCurrent
bool gl_load();
