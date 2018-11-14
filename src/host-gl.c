/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "host-gl.h"

#include <assert.h>

#define CHECK_LOG(var, kind, status_enum, kind_str)      \
  do {                                                   \
    GLint status;                                        \
    glGet##kind##iv(var, status_enum, &status);          \
    if (!status) {                                       \
      GLint length;                                      \
      glGet##kind##iv(var, GL_INFO_LOG_LENGTH, &length); \
      GLchar* log = malloc(length + 1); /* Leaks. */     \
      glGet##kind##InfoLog(var, length, NULL, log);      \
      PRINT_ERROR("%s ERROR: %s\n", kind_str, log);      \
      goto error;                                        \
    }                                                    \
  } while (0)

Result host_gl_init_procs(void) {
#define V(name, type)                  \
  name = SDL_GL_GetProcAddress(#name); \
  CHECK_MSG(name != 0, "Unable to get GL function: " #name);
  FOREACH_GLEXT_PROC(V)
#undef V
  return OK;
  ON_ERROR_RETURN;
}

Result host_gl_shader(GLenum type, const GLchar* source, GLuint* out_shader) {
  assert(type == GL_VERTEX_SHADER || type == GL_FRAGMENT_SHADER);
  GLuint shader = glCreateShader(type);
  char version[128];
  snprintf(version, sizeof(version) - 1, "#version %d\n",
           host_gl_shader_version());
  const GLchar* sources[] = {version, source};
  glShaderSource(shader, 2, sources, NULL);
  glCompileShader(shader);
  CHECK_LOG(shader, Shader, GL_COMPILE_STATUS, type == GL_VERTEX_SHADER
                                                   ? "GL_VERTEX_SHADER"
                                                   : "GL_FRAGMENT_SHADER");
  *out_shader = shader;
  return OK;
  ON_ERROR_RETURN;
}

Result host_gl_program(GLuint vert_shader, GLuint frag_shader,
                       GLuint* out_program) {
  GLuint program = glCreateProgram();
  glAttachShader(program, vert_shader);
  glAttachShader(program, frag_shader);
  glLinkProgram(program);
  CHECK_LOG(program, Program, GL_LINK_STATUS, "GL_PROGRAM");
  *out_program = program;
  return OK;
  ON_ERROR_RETURN;
}

int host_gl_shader_version(void) {
  const GLubyte* string = glGetString(GL_SHADING_LANGUAGE_VERSION);
  int major, minor;
  sscanf((const char*)string, "%d.%d", &major, &minor);
  return major * 100 + minor;
}
