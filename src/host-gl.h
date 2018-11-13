/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_HOST_GL_H_
#define BINJGB_HOST_GL_H_

#include "common.h"

#include <SDL.h>
#include <SDL_opengl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FOREACH_GLEXT_PROC(V)                                    \
  V(glAttachShader, PFNGLATTACHSHADERPROC)                       \
  V(glBindBuffer, PFNGLBINDBUFFERPROC)                           \
  V(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC)                 \
  V(glBufferData, PFNGLBUFFERDATAPROC)                           \
  V(glCompileShader, PFNGLCOMPILESHADERPROC)                     \
  V(glCreateProgram, PFNGLCREATEPROGRAMPROC)                     \
  V(glCreateShader, PFNGLCREATESHADERPROC)                       \
  V(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
  V(glGenBuffers, PFNGLGENBUFFERSPROC)                           \
  V(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC)                 \
  V(glGetAttribLocation, PFNGLGETATTRIBLOCATIONPROC)             \
  V(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC)             \
  V(glGetProgramiv, PFNGLGETPROGRAMIVPROC)                       \
  V(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC)               \
  V(glGetShaderiv, PFNGLGETSHADERIVPROC)                         \
  V(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC)           \
  V(glLinkProgram, PFNGLLINKPROGRAMPROC)                         \
  V(glShaderSource, PFNGLSHADERSOURCEPROC)                       \
  V(glUniform1i, PFNGLUNIFORM1IPROC)                             \
  V(glUniform4fv, PFNGLUNIFORM4FVPROC)                           \
  V(glUniformMatrix3fv, PFNGLUNIFORMMATRIX3FVPROC)               \
  V(glUseProgram, PFNGLUSEPROGRAMPROC)                           \
  V(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)

#define V(name, type) type name;
FOREACH_GLEXT_PROC(V)
#undef V

Result host_gl_init_procs(void);
Result host_gl_shader(GLenum type, const GLchar* source, GLuint* out_shader);
Result host_gl_program(GLuint vert_shader, GLuint frag_shader,
                       GLuint* out_program);
int host_gl_shader_version(void);  // e.g. 1.30 is returned as 130.

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_HOST_GL_H_ */
