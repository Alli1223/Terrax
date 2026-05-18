// GL stubs for headless (no GPU) test builds.
// Provides all GL symbols used by the game as safe no-ops so VoxelVolume
// and character rig code can be exercised without an OpenGL context.

#include "gl_loader.h"

// --- Function pointer stubs ---
static void stub_glGenBuffers(GLsizei n, GLuint* ids) { for(int i=0;i<n;i++) ids[i]=(GLuint)(i+1); }
static void stub_glBindBuffer(GLenum, GLuint) {}
static void stub_glBufferData(GLenum, GLsizeiptr, const void*, GLenum) {}
static void stub_glDeleteBuffers(GLsizei, const GLuint*) {}
static void stub_glGenVertexArrays(GLsizei n, GLuint* ids) { for(int i=0;i<n;i++) ids[i]=(GLuint)(i+1); }
static void stub_glBindVertexArray(GLuint) {}
static void stub_glDeleteVertexArrays(GLsizei, const GLuint*) {}
static void stub_glVertexAttribPointer(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*) {}
static void stub_glEnableVertexAttribArray(GLuint) {}
static GLuint stub_glCreateShader(GLenum) { return 1; }
static void stub_glShaderSource(GLuint,GLsizei,const GLchar* const*,const GLint*) {}
static void stub_glCompileShader(GLuint) {}
static void stub_glGetShaderiv(GLuint,GLenum,GLint* p) { if(p) *p=1; }
static void stub_glGetShaderInfoLog(GLuint,GLsizei,GLsizei*,GLchar*) {}
static void stub_glDeleteShader(GLuint) {}
static GLuint stub_glCreateProgram() { return 1; }
static void stub_glAttachShader(GLuint,GLuint) {}
static void stub_glLinkProgram(GLuint) {}
static void stub_glGetProgramiv(GLuint,GLenum,GLint* p) { if(p) *p=1; }
static void stub_glGetProgramInfoLog(GLuint,GLsizei,GLsizei*,GLchar*) {}
static void stub_glUseProgram(GLuint) {}
static void stub_glDeleteProgram(GLuint) {}
static GLint stub_glGetUniformLocation(GLuint,const GLchar*) { return 0; }
static void stub_glUniform1i(GLint,GLint) {}
static void stub_glUniform1f(GLint,GLfloat) {}
static void stub_glUniform3fv(GLint,GLsizei,const GLfloat*) {}
static void stub_glUniform4fv(GLint,GLsizei,const GLfloat*) {}
static void stub_glUniformMatrix4fv(GLint,GLsizei,GLboolean,const GLfloat*) {}
static void stub_glGenTextures(GLsizei n, GLuint* ids) { for(int i=0;i<n;i++) ids[i]=(GLuint)(i+1); }
static void stub_glDeleteTextures(GLsizei,const GLuint*) {}
static void stub_glBindTexture(GLenum,GLuint) {}
static void stub_glTexImage2D(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*) {}
static void stub_glTexParameteri(GLenum,GLenum,GLint) {}
static void stub_glTexParameterfv(GLenum,GLenum,const GLfloat*) {}
static void stub_glActiveTexture(GLenum) {}
static void stub_glGenerateMipmap(GLenum) {}
static void stub_glGenFramebuffers(GLsizei n, GLuint* ids) { for(int i=0;i<n;i++) ids[i]=(GLuint)(i+1); }
static void stub_glDeleteFramebuffers(GLsizei,const GLuint*) {}
static void stub_glBindFramebuffer(GLenum,GLuint) {}
static void stub_glFramebufferTexture2D(GLenum,GLenum,GLenum,GLuint,GLint) {}
static void stub_glDrawBuffer(GLenum) {}
static void stub_glReadBuffer(GLenum) {}
static void stub_glGenRenderbuffers(GLsizei n, GLuint* ids) { for(int i=0;i<n;i++) ids[i]=(GLuint)(i+1); }
static void stub_glDeleteRenderbuffers(GLsizei,const GLuint*) {}
static void stub_glBindRenderbuffer(GLenum,GLuint) {}
static void stub_glRenderbufferStorage(GLenum,GLenum,GLsizei,GLsizei) {}
static void stub_glFramebufferRenderbuffer(GLenum,GLenum,GLenum,GLuint) {}

// --- Function pointer definitions ---
PFN_glGenBuffers             glGenBuffers              = stub_glGenBuffers;
PFN_glBindBuffer             glBindBuffer              = stub_glBindBuffer;
PFN_glBufferData             glBufferData              = stub_glBufferData;
PFN_glDeleteBuffers          glDeleteBuffers           = stub_glDeleteBuffers;
PFN_glGenVertexArrays        glGenVertexArrays         = stub_glGenVertexArrays;
PFN_glBindVertexArray        glBindVertexArray         = stub_glBindVertexArray;
PFN_glDeleteVertexArrays     glDeleteVertexArrays      = stub_glDeleteVertexArrays;
PFN_glVertexAttribPointer    glVertexAttribPointer     = stub_glVertexAttribPointer;
PFN_glEnableVertexAttribArray glEnableVertexAttribArray = stub_glEnableVertexAttribArray;
PFN_glCreateShader           glCreateShader            = stub_glCreateShader;
PFN_glShaderSource           glShaderSource            = stub_glShaderSource;
PFN_glCompileShader          glCompileShader           = stub_glCompileShader;
PFN_glGetShaderiv            glGetShaderiv             = stub_glGetShaderiv;
PFN_glGetShaderInfoLog       glGetShaderInfoLog        = stub_glGetShaderInfoLog;
PFN_glDeleteShader           glDeleteShader            = stub_glDeleteShader;
PFN_glCreateProgram          glCreateProgram           = stub_glCreateProgram;
PFN_glAttachShader           glAttachShader            = stub_glAttachShader;
PFN_glLinkProgram            glLinkProgram             = stub_glLinkProgram;
PFN_glGetProgramiv           glGetProgramiv            = stub_glGetProgramiv;
PFN_glGetProgramInfoLog      glGetProgramInfoLog       = stub_glGetProgramInfoLog;
PFN_glUseProgram             glUseProgram              = stub_glUseProgram;
PFN_glDeleteProgram          glDeleteProgram           = stub_glDeleteProgram;
PFN_glGetUniformLocation     glGetUniformLocation      = stub_glGetUniformLocation;
PFN_glUniform1i              glUniform1i               = stub_glUniform1i;
PFN_glUniform1f              glUniform1f               = stub_glUniform1f;
PFN_glUniform3fv             glUniform3fv              = stub_glUniform3fv;
PFN_glUniform4fv             glUniform4fv              = stub_glUniform4fv;
PFN_glUniformMatrix4fv       glUniformMatrix4fv        = stub_glUniformMatrix4fv;
PFN_glGenTextures            glGenTextures             = stub_glGenTextures;
PFN_glDeleteTextures         glDeleteTextures          = stub_glDeleteTextures;
PFN_glBindTexture            glBindTexture             = stub_glBindTexture;
PFN_glTexImage2D             glTexImage2D              = stub_glTexImage2D;
PFN_glTexParameteri          glTexParameteri           = stub_glTexParameteri;
PFN_glTexParameterfv         glTexParameterfv          = stub_glTexParameterfv;
PFN_glActiveTexture          glActiveTexture           = stub_glActiveTexture;
PFN_glGenerateMipmap         glGenerateMipmap          = stub_glGenerateMipmap;
PFN_glGenFramebuffers        glGenFramebuffers         = stub_glGenFramebuffers;
PFN_glDeleteFramebuffers     glDeleteFramebuffers      = stub_glDeleteFramebuffers;
PFN_glBindFramebuffer        glBindFramebuffer         = stub_glBindFramebuffer;
PFN_glFramebufferTexture2D   glFramebufferTexture2D    = stub_glFramebufferTexture2D;
PFN_glDrawBuffer             glDrawBuffer              = stub_glDrawBuffer;
PFN_glReadBuffer             glReadBuffer              = stub_glReadBuffer;
PFN_glGenRenderbuffers       glGenRenderbuffers        = stub_glGenRenderbuffers;
PFN_glDeleteRenderbuffers    glDeleteRenderbuffers     = stub_glDeleteRenderbuffers;
PFN_glBindRenderbuffer       glBindRenderbuffer        = stub_glBindRenderbuffer;
PFN_glRenderbufferStorage    glRenderbufferStorage     = stub_glRenderbufferStorage;
PFN_glFramebufferRenderbuffer glFramebufferRenderbuffer = stub_glFramebufferRenderbuffer;

// --- Direct GL 1.x symbol stubs ---
extern "C" {
void glEnable(GLenum) {}
void glDisable(GLenum) {}
void glClear(GLbitfield) {}
void glClearColor(GLfloat,GLfloat,GLfloat,GLfloat) {}
void glViewport(GLint,GLint,GLsizei,GLsizei) {}
void glDepthMask(GLboolean) {}
void glCullFace(GLenum) {}
void glDrawArrays(GLenum,GLint,GLsizei) {}
void glBlendFunc(GLenum,GLenum) {}
void glLineWidth(GLfloat) {}
void glPolygonMode(GLenum,GLenum) {}
void glScissor(GLint,GLint,GLsizei,GLsizei) {}
}

bool gl_load() { return true; }
