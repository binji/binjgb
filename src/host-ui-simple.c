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

static const GLuint s_sgb_contents_vertex_start = 0;
static const GLuint s_sgb_border_vertex_start = 4;
static const GLuint s_fb_only_vertex_start = 8;

typedef struct {
  f32 pos[2];
  f32 tex_coord[2];
} Vertex;

typedef struct HostUI {
  SDL_Window* window;
  GLuint vao;
  GLuint program;
  GLint uSampler;
  GLint uUsePalette;
  GLint uPalette;
  int width, height;
  Bool use_sgb_border;
} HostUI;

static f32 InvLerpClipSpace(f32 x, f32 max) { return 2 * (x / max) - 1; }

static Result host_ui_init(struct HostUI* ui, SDL_Window* window,
                           Bool use_sgb_border) {
  const f32 left = InvLerpClipSpace(SGB_SCREEN_LEFT, SGB_SCREEN_WIDTH);
  const f32 right = InvLerpClipSpace(SGB_SCREEN_RIGHT, SGB_SCREEN_WIDTH);
  const f32 top = -InvLerpClipSpace(SGB_SCREEN_TOP, SGB_SCREEN_HEIGHT);
  const f32 bottom = -InvLerpClipSpace(SGB_SCREEN_BOTTOM, SGB_SCREEN_HEIGHT);

  const Vertex vertex_buffer[] = {
    // SGB contents
    {{left, top}, {0, 0}},
    {{left, bottom}, {0, SCREEN_HEIGHT / 256.0f}},
    {{right, top}, {SCREEN_WIDTH / 256.0f, 0}},
    {{right, bottom}, {SCREEN_WIDTH / 256.0f, SCREEN_HEIGHT / 256.0f}},

    // SGB border
    {{-1, +1}, {0, 0}},
    {{-1, -1}, {0, SGB_SCREEN_HEIGHT / 256.0f}},
    {{+1, +1}, {SGB_SCREEN_WIDTH / 256.0f, 0}},
    {{+1, -1}, {SGB_SCREEN_WIDTH / 256.0f, SGB_SCREEN_HEIGHT / 256.0f}},

    // FB only
    {{-1, +1}, {0, 0}},
    {{-1, -1}, {0, SCREEN_HEIGHT / 256.0f}},
    {{+1, +1}, {SCREEN_WIDTH / 256.0f, 0}},
    {{+1, -1}, {SCREEN_WIDTH / 256.0f, SCREEN_HEIGHT / 256.0f}},
  };

  static const char* s_vertex_shader =
      "in vec2 aPos;\n"
      "in vec2 aTexCoord;\n"
      "out vec2 vTexCoord;\n"
      "void main(void) {\n"
      "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
      "  vTexCoord = aTexCoord;\n"
      "}\n";

  static const char* s_fragment_shader =
      "in vec2 vTexCoord;\n"
      "out vec4 oColor;\n"
      "uniform int uUsePalette;\n"
      "uniform vec4 uPalette[4];\n"
      "uniform sampler2D uSampler;\n"
      "void main(void) {\n"
      "  vec4 color = texture(uSampler, vTexCoord);\n"
      "  if (uUsePalette != 0) {\n"
      "    color = uPalette[int(clamp(color.x * 256.0, 0.0, 3.0))];\n"
      "  }\n"
      "  oColor = color;\n"
      "}\n";

  ui->window = window;
  ui->use_sgb_border = use_sgb_border;
  ui->width = use_sgb_border ? SGB_SCREEN_WIDTH : SCREEN_WIDTH;
  ui->height = use_sgb_border ? SGB_SCREEN_HEIGHT : SCREEN_HEIGHT;

  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_buffer), vertex_buffer,
               GL_STATIC_DRAW);
  GLuint vs, fs;
  CHECK(SUCCESS(host_gl_shader(GL_VERTEX_SHADER, s_vertex_shader, &vs)));
  CHECK(SUCCESS(host_gl_shader(GL_FRAGMENT_SHADER, s_fragment_shader, &fs)));
  CHECK(SUCCESS(host_gl_program(vs, fs, &ui->program)));

  GLint aPos = glGetAttribLocation(ui->program, "aPos");
  GLint aTexCoord = glGetAttribLocation(ui->program, "aTexCoord");
  ui->uSampler = glGetUniformLocation(ui->program, "uSampler");
  ui->uUsePalette = glGetUniformLocation(ui->program, "uUsePalette");
  ui->uPalette = glGetUniformLocation(ui->program, "uPalette[0]");

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

struct HostUI* host_ui_new(struct SDL_Window* window, Bool use_sgb_border) {
  HostUI* ui = xcalloc(1, sizeof(HostUI));
  CHECK(SUCCESS(host_ui_init(ui, window, use_sgb_border)));
  return ui;
error:
  xfree(ui);
  return NULL;
}

void host_ui_delete(struct HostUI* ui) {
  xfree(ui);
}

void host_ui_event(struct HostUI* ui, union SDL_Event* event) {
  if (event->type == SDL_WINDOWEVENT &&
      (event->window.event == SDL_WINDOWEVENT_SHOWN ||
       event->window.event == SDL_WINDOWEVENT_RESIZED)) {
    int iw, ih;
    SDL_GL_GetDrawableSize(ui->window, &iw, &ih);
    f32 w = iw, h = ih;
    f32 aspect = w / h;
    f32 want_aspect = (f32)ui->width / ui->height;
    f32 new_w = aspect < want_aspect ? w : h * want_aspect;
    f32 new_h = aspect < want_aspect ? w / want_aspect : h;
    f32 new_left = (w - new_w) * 0.5f;
    f32 new_top = (h - new_h) * 0.5f;
    glViewport(new_left, new_top, new_w, new_h);
  }
}

static void render_screen_texture(struct HostUI* ui, HostTexture* tex,
                                  GLuint start) {
  glUseProgram(ui->program);
  glUniform1i(ui->uSampler, 0);
  glBindVertexArray(ui->vao);
  glBindTexture(GL_TEXTURE_2D, tex->handle);
  glDrawArrays(GL_TRIANGLE_STRIP, start, 4);
}

void host_ui_begin_frame(struct HostUI* ui, HostTexture* fb_texture,
                         HostTexture* sgb_fb_texture) {
  glClearColor(0.1f, 0.1f, 0.1f, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  if (ui->use_sgb_border) {
    render_screen_texture(ui, fb_texture, s_sgb_contents_vertex_start);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    render_screen_texture(ui, sgb_fb_texture, s_sgb_border_vertex_start);
    glDisable(GL_BLEND);
  } else {
    render_screen_texture(ui, fb_texture, s_fb_only_vertex_start);
  }
}

void host_ui_end_frame(struct HostUI* ui) {
  SDL_GL_SwapWindow(ui->window);
}

void host_ui_set_palette(struct HostUI* ui, RGBA palette[4]) {
  float p[16];
  int i;
  for (i = 0; i < 4; ++i) {
    p[i * 4 + 0] = ((palette[i] >> 0) & 255) / 255.0f;
    p[i * 4 + 1] = ((palette[i] >> 8) & 255) / 255.0f;
    p[i * 4 + 2] = ((palette[i] >> 16) & 255) / 255.0f;
    p[i * 4 + 3] = 1.0f;
  }
  glUseProgram(ui->program);
  glUniform4fv(ui->uPalette, 4, p);
}

void host_ui_enable_palette(struct HostUI* ui, Bool enabled) {
  glUseProgram(ui->program);
  glUniform1i(ui->uUsePalette, enabled ? 1 : 0);
}

void host_ui_render_screen_overlay(struct HostUI* ui, HostTexture* tex) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  render_screen_texture(ui, tex,
                        ui->use_sgb_border ? s_sgb_contents_vertex_start
                                           : s_fb_only_vertex_start);
  glDisable(GL_BLEND);
}

Bool host_ui_capture_keyboard(struct HostUI* ui) {
  return FALSE;
}
