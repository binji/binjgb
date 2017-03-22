/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "host-ui.h"

#include "host.h"
#include "host-gl.h"
#include "imgui.h"

/* Much cribbed from imgui_impl_sdl_gl3.cpp. */

struct HostUI {
  HostUI(SDL_Window*);
  ~HostUI();

  Result init();
  Result init_gl();
  void init_font();
  void event(union SDL_Event*);
  void upload_frame_buffer(FrameBuffer*);
  void render_draw_lists(ImDrawData*);
  void begin_frame();
  void end_frame();

  static void render_draw_lists_thunk(ImDrawData*);
  static void set_clipboard_text(void* user_data, const char* text);
  static const char* get_clipboard_text(void* user_data);

  SDL_Window* window;
  f64 time_sec;
  f32 mouse_wheel;
  f32 mouse_pressed[3];
  f32 proj_matrix[9];
  GLuint emulator_texture;
  GLuint font_texture;
  GLuint vao;
  GLuint vbo;
  GLuint ebo;
  GLuint program;
  GLint uProjMatrix;
  GLint uSampler;

  // Global so it can be accessed by render_draw_lists callback, which has no
  // user_data pointer.
  static HostUI* s_ui;
};

HostUI* HostUI::s_ui;

HostUI::HostUI(SDL_Window* window)
    : window(window),
      time_sec(0),
      mouse_wheel(0),
      emulator_texture(0),
      font_texture(0),
      vao(0),
      vbo(0),
      ebo(0),
      program(0),
      uProjMatrix(0),
      uSampler(0) {
  s_ui = this;
}

HostUI::~HostUI() {
  s_ui = nullptr;
}

Result HostUI::init() {
  if (!SUCCESS(init_gl())) {
    return ERROR;
  }

  init_font();

  ImGuiIO& io = ImGui::GetIO();
  io.KeyMap[ImGuiKey_Tab] = SDLK_TAB;
  io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
  io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
  io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
  io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
  io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
  io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
  io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
  io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
  io.KeyMap[ImGuiKey_Delete] = SDLK_DELETE;
  io.KeyMap[ImGuiKey_Backspace] = SDLK_BACKSPACE;
  io.KeyMap[ImGuiKey_Enter] = SDLK_RETURN;
  io.KeyMap[ImGuiKey_Escape] = SDLK_ESCAPE;
  io.KeyMap[ImGuiKey_A] = SDLK_a;
  io.KeyMap[ImGuiKey_C] = SDLK_c;
  io.KeyMap[ImGuiKey_V] = SDLK_v;
  io.KeyMap[ImGuiKey_X] = SDLK_x;
  io.KeyMap[ImGuiKey_Y] = SDLK_y;
  io.KeyMap[ImGuiKey_Z] = SDLK_z;

  io.RenderDrawListsFn = render_draw_lists_thunk;
  io.SetClipboardTextFn = set_clipboard_text;
  io.GetClipboardTextFn = get_clipboard_text;
  io.ClipboardUserData = nullptr;
  return OK;
}

Result HostUI::init_gl() {
  static const char* s_vertex_shader =
      "attribute vec2 aPos;\n"
      "attribute vec2 aUV;\n"
      "attribute vec4 aColor;\n"
      "varying vec2 vUV;\n"
      "varying vec4 vColor;\n"
      "uniform mat3 uProjMatrix;\n"
      "void main(void) {\n"
      "  gl_Position = vec4(uProjMatrix * vec3(aPos, 1.0), 1.0);\n"
      "  vUV = aUV;\n"
      "  vColor = aColor;\n"
      "}\n";

  static const char* s_fragment_shader =
      "varying vec2 vUV;\n"
      "varying vec4 vColor;\n"
      "uniform sampler2D uSampler;\n"
      "void main(void) {\n"
      "  gl_FragColor = vColor * texture2D(uSampler, vUV);\n"
      "}\n";

  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);

  glGenTextures(1, &emulator_texture);
  glBindTexture(GL_TEXTURE_2D, emulator_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, HOST_FRAME_BUFFER_TEXTURE_WIDTH,
               HOST_FRAME_BUFFER_TEXTURE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  GLuint vs, fs;
  if (!SUCCESS(host_gl_shader(GL_VERTEX_SHADER, s_vertex_shader, &vs)) ||
      !SUCCESS(host_gl_shader(GL_FRAGMENT_SHADER, s_fragment_shader, &fs)) ||
      !SUCCESS(host_gl_program(vs, fs, &program))) {
    return ERROR;
  }

  GLint aPos = glGetAttribLocation(program, "aPos");
  GLint aUV = glGetAttribLocation(program, "aUV");
  GLint aColor = glGetAttribLocation(program, "aColor");
  uProjMatrix = glGetUniformLocation(program, "uProjMatrix");
  uSampler = glGetUniformLocation(program, "uSampler");

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glEnableVertexAttribArray(aPos);
  glEnableVertexAttribArray(aUV);
  glEnableVertexAttribArray(aColor);
  glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
                        (void*)offsetof(ImDrawVert, pos));
  glVertexAttribPointer(aUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
                        (void*)offsetof(ImDrawVert, uv));
  glVertexAttribPointer(aColor, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                        sizeof(ImDrawVert), (void*)offsetof(ImDrawVert, col));

  return OK;
}

void HostUI::init_font() {
  ImGuiIO& io = ImGui::GetIO();

  // Load as RGBA 32-bits for OpenGL3 demo because it is more likely to be
  // compatible with user's existing shader.
  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  // Upload texture to graphics system
  glGenTextures(1, &font_texture);
  glBindTexture(GL_TEXTURE_2D, font_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);

  // Store our identifier
  io.Fonts->TexID = (void*)(intptr_t)font_texture;
}

void HostUI::event(union SDL_Event* event) {
  ImGuiIO& io = ImGui::GetIO();
  switch (event->type) {
    case SDL_WINDOWEVENT:
      switch (event->window.event) {
        case SDL_WINDOWEVENT_RESIZED: {
          ImGuiIO& io = ImGui::GetIO();

          f32 w = event->window.data1;
          f32 h = event->window.data2;
          int display_w, display_h;
          SDL_GL_GetDrawableSize(window, &display_w, &display_h);
          io.DisplaySize = ImVec2(w, h);
          io.DisplayFramebufferScale =
              ImVec2(w > 0 ? (display_w / w) : 0, h > 0 ? (display_h / h) : 0);

          memset(proj_matrix, 0, sizeof(proj_matrix));
          proj_matrix[0] = 2.0f / w;
          proj_matrix[4] = -2.0f / h;
          proj_matrix[6] = -1.0f;
          proj_matrix[7] = 1.0f;
          proj_matrix[8] = 1.0f;
          break;
        }
      }
      break;
    case SDL_MOUSEWHEEL:
      if (event->wheel.y > 0) mouse_wheel = 1;
      if (event->wheel.y < 0) mouse_wheel = -1;
      break;
    case SDL_MOUSEBUTTONDOWN:
      if (event->button.button == SDL_BUTTON_LEFT) mouse_pressed[0] = true;
      if (event->button.button == SDL_BUTTON_RIGHT) mouse_pressed[1] = true;
      if (event->button.button == SDL_BUTTON_MIDDLE) mouse_pressed[2] = true;
      break;
    case SDL_TEXTINPUT:
      io.AddInputCharactersUTF8(event->text.text);
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      int key = event->key.keysym.sym & ~SDLK_SCANCODE_MASK;
      io.KeysDown[key] = (event->type == SDL_KEYDOWN);
      io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
      io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
      io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
      io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);
      break;
    }
  }
}

void HostUI::upload_frame_buffer(FrameBuffer* frame_buffer) {
  glBindTexture(GL_TEXTURE_2D, emulator_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, GL_RGBA,
                  GL_UNSIGNED_BYTE, frame_buffer);
}

void HostUI::begin_frame() {
  ImGuiIO& io = ImGui::GetIO();

  // Setup time step
  f64 now_sec = SDL_GetTicks();
  io.DeltaTime = (f32)(time_sec ? now_sec - time_sec : 1.0 / 60.0);
  time_sec = now_sec;

  // Setup inputs (we already got mouse wheel, keyboard keys & characters from
  // SDL_PollEvent())
  int mx, my;
  Uint32 mouse_mask = SDL_GetMouseState(&mx, &my);
  if (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS) {
    // Mouse position, in pixels (set to -1,-1 if no mouse / on another screen,
    // etc.)
    io.MousePos = ImVec2((f32)mx, (f32)my);
  } else {
    io.MousePos = ImVec2(-1, -1);
  }

  // If a mouse press event came, always pass it as "mouse held this frame", so
  // we don't miss click-release events that are shorter than 1 frame.
  io.MouseDown[0] =
      mouse_pressed[0] || (mouse_mask & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
  io.MouseDown[1] =
      mouse_pressed[1] || (mouse_mask & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
  io.MouseDown[2] =
      mouse_pressed[2] || (mouse_mask & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
  mouse_pressed[0] = mouse_pressed[1] = mouse_pressed[2] = false;

  io.MouseWheel = mouse_wheel;
  mouse_wheel = 0.0f;

  SDL_ShowCursor(io.MouseDrawCursor ? 0 : 1);
  ImGui::NewFrame();
}

void HostUI::end_frame() {
  ImGuiIO& io = ImGui::GetIO();
  glViewport(0, 0, io.DisplaySize.x, io.DisplaySize.y);
  glClearColor(0.1f, 0.1f, 0.1f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui::Render();
  SDL_GL_SwapWindow(window);
}

void HostUI::render_draw_lists_thunk(ImDrawData* draw_data) {
  s_ui->render_draw_lists(draw_data);
}

void HostUI::render_draw_lists(ImDrawData* draw_data) {
  ImGuiIO& io = ImGui::GetIO();
  int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
  int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
  if (fb_width == 0 || fb_height == 0) {
    return;
  }

  draw_data->ScaleClipRects(io.DisplayFramebufferScale);

  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE0);

  glUseProgram(program);
  glUniform1i(uSampler, 0);
  glUniformMatrix3fv(uProjMatrix, 1, GL_FALSE, proj_matrix);
  glBindVertexArray(vao);

  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    const ImDrawIdx* idx_buffer_offset = 0;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert),
                 (GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx),
                 (GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

    for (int i = 0; i < cmd_list->CmdBuffer.Size; i++) {
      const ImDrawCmd* cmd = &cmd_list->CmdBuffer[i];
      if (cmd->UserCallback) {
        cmd->UserCallback(cmd_list, cmd);
      } else {
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd->TextureId);
        glScissor((int)cmd->ClipRect.x, (int)(fb_height - cmd->ClipRect.w),
                  (int)(cmd->ClipRect.z - cmd->ClipRect.x),
                  (int)(cmd->ClipRect.w - cmd->ClipRect.y));
        glDrawElements(
            GL_TRIANGLES, (GLsizei)cmd->ElemCount,
            sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
            idx_buffer_offset);
      }
      idx_buffer_offset += cmd->ElemCount;
    }
  }

  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
}

void HostUI::set_clipboard_text(void* user_data, const char* text) {
  SDL_SetClipboardText(text);
}

const char* HostUI::get_clipboard_text(void* user_data) {
  return SDL_GetClipboardText();
}

HostUI* host_ui_new(struct SDL_Window* window) {
  HostUI* ui = new HostUI(window);
  if (!SUCCESS(ui->init())) {
    delete ui;
    return nullptr;
  }

  return ui;
}

void host_ui_delete(struct HostUI* ui) {
  delete ui;
}

void host_ui_event(struct HostUI* ui, union SDL_Event* event) {
  ui->event(event);
}

void host_ui_upload_frame_buffer(struct HostUI* ui, FrameBuffer* frame_buffer) {
  ui->upload_frame_buffer(frame_buffer);
}

void host_ui_begin_frame(HostUI* ui) {
  ui->begin_frame();
}

void host_ui_end_frame(HostUI* ui) {
  ui->end_frame();
}

intptr_t host_ui_get_frame_buffer_texture(struct HostUI* ui) {
  return ui->emulator_texture;
}
