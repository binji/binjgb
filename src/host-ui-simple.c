/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "host-ui.h"

#include "emulator.h"
#include "host.h"
#include "host-gl.h"

typedef struct {
  f32 pos[2];
  f32 tex_coord[2];
} Vertex;

typedef struct HostUI {
  SDL_Window* window;
  GLuint vao;
  GLuint vbo;
  GLuint program;
  GLint uSampler;
} HostUI;

static Result host_ui_init(struct HostUI* ui, SDL_Window* window) {
  static Vertex s_vertex_buffer[4] = {
    {{-1, +1}, {0, 0}},
    {{-1, -1}, {0, SCREEN_HEIGHT / 256.0f}},
    {{+1, +1}, {SCREEN_WIDTH / 256.0f, 0}},
    {{+1, -1}, {SCREEN_WIDTH / 256.0f, SCREEN_HEIGHT / 256.0f}},
  };

  static const char* s_vertex_shader =
      "attribute vec2 aPos;\n"
      "attribute vec2 aTexCoord;\n"
      "varying vec2 vTexCoord;\n"
      "void main(void) {\n"
      "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
      "  vTexCoord = aTexCoord;\n"
      "}\n";

  static const char* s_fragment_shader =
      "varying vec2 vTexCoord;\n"
      "uniform sampler2D uSampler;\n"
      "void main(void) {\n"
      "  gl_FragColor = texture2D(uSampler, vTexCoord);\n"
      "}\n";

  ui->window = window;

  glGenBuffers(1, &ui->vbo);
  glBindBuffer(GL_ARRAY_BUFFER, ui->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(s_vertex_buffer), s_vertex_buffer,
               GL_STATIC_DRAW);

  GLuint vs, fs;
  CHECK(SUCCESS(host_gl_shader(GL_VERTEX_SHADER, s_vertex_shader, &vs)));
  CHECK(SUCCESS(host_gl_shader(GL_FRAGMENT_SHADER, s_fragment_shader, &fs)));
  CHECK(SUCCESS(host_gl_program(vs, fs, &ui->program)));

  GLint aPos = glGetAttribLocation(ui->program, "aPos");
  GLint aTexCoord = glGetAttribLocation(ui->program, "aTexCoord");
  ui->uSampler = glGetUniformLocation(ui->program, "uSampler");

  glGenVertexArrays(1, &ui->vao);
  glBindVertexArray(ui->vao);
  glEnableVertexAttribArray(aPos);
  glEnableVertexAttribArray(aTexCoord);
  glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, pos));
  glVertexAttribPointer(aTexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, tex_coord));
  return OK;
  ON_ERROR_RETURN;
}

struct HostUI* host_ui_new(struct SDL_Window* window) {
  HostUI* ui = calloc(1, sizeof(HostUI));
  CHECK(SUCCESS(host_ui_init(ui, window)));
  return ui;
error:
  free(ui);
  return NULL;
}

void host_ui_delete(struct HostUI* ui) {
  free(ui);
}

void host_ui_event(struct HostUI* ui, union SDL_Event* event) {
  if (event->type == SDL_WINDOWEVENT &&
      event->window.event == SDL_WINDOWEVENT_RESIZED) {
    f32 w = event->window.data1;
    f32 h = event->window.data2;
    f32 aspect = w / h;
    f32 want_aspect = (f32)SCREEN_WIDTH / SCREEN_HEIGHT;
    f32 new_w = aspect < want_aspect ? w : h * want_aspect;
    f32 new_h = aspect < want_aspect ? w / want_aspect : h;
    f32 new_left = (w - new_w) * 0.5f;
    f32 new_top = (h - new_h) * 0.5f;
    glViewport(new_left, new_top, new_w, new_h);
  }
}

void host_ui_begin_frame(struct HostUI* ui, HostTexture* fb_texture) {
  glClearColor(0.1f, 0.1f, 0.1f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(ui->program);
  glUniform1i(ui->uSampler, 0);
  glBindVertexArray(ui->vao);
  glBindTexture(GL_TEXTURE_2D, fb_texture->handle);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void host_ui_end_frame(struct HostUI* ui) {
  SDL_GL_SwapWindow(ui->window);
}
