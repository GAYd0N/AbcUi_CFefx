#include "GlApi.h"

#include <abcui/render_backend.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <vector>

namespace {

struct Image {
  GLuint texture = 0;
  GLuint framebuffer = 0;
  int width = 0;
  int height = 0;
};

struct Backend {
  AbcUiRenderBackendHost host{};
  AbcUiRenderFrameSink sink{};
  gl::Api api;
  HGLRC context = nullptr;
  GLuint program = 0;
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
  GLuint sampler = 0;
  GLint size = -1;
  GLint translation = -1;
  GLint mode = -1;
  GLint fullscreen = -1;
  GLint flip = -1;
  GLint direction = -1;
  int width = 0;
  int height = 0;
  int clip[4]{};
  std::vector<std::array<float, 2>> translations;
  std::unordered_map<uint32_t, Image> targets;
  uint32_t nextTarget = 0;
  std::array<Image, 3> blur{};
  Image capture;
  Image captureDepth;
  Image source;
  bool ready = false;
};

Backend &backend() {
  static Backend instance;
  return instance;
}

void enable(GLenum capability, bool value) {
  if (value)
    glEnable(capability);
  else
    glDisable(capability);
}

// State is scoped to each operation, including draws. VGUI may upload an atlas
// or movie between any two draws: never leave its cached texture binding stale.
// VAO isolation keeps the engine's fixed-pipeline vertex arrays untouched.
class State {
public:
  explicit State(gl::Api &api) : api(api) {
    glGetIntegerv(gl::kCurrentProgram, &program);
    glGetIntegerv(gl::kVertexArrayBinding, &vao);
    glGetIntegerv(gl::kArrayBufferBinding, &buffer);
    glGetIntegerv(gl::kPixelUnpackBufferBinding, &unpackBuffer);
    glGetIntegerv(gl::kReadFramebufferBinding, &read);
    glGetIntegerv(gl::kDrawFramebufferBinding, &draw);
    glGetIntegerv(gl::kActiveTexture, &active);
    for (int i = 0; i < 2; ++i) {
      api.ActiveTexture(gl::kTexture0 + i);
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
      glGetIntegerv(gl::kSamplerBinding, &samplers[i]);
    }
    api.ActiveTexture(active);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetIntegerv(GL_POLYGON_MODE, polygon);
    glGetIntegerv(gl::kBlendSrcRgb, &srcRgb);
    glGetIntegerv(gl::kBlendDstRgb, &dstRgb);
    glGetIntegerv(gl::kBlendSrcAlpha, &srcAlpha);
    glGetIntegerv(gl::kBlendDstAlpha, &dstAlpha);
    glGetIntegerv(gl::kBlendEquationRgb, &equationRgb);
    glGetIntegerv(gl::kBlendEquationAlpha, &equationAlpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    for (size_t i = 0; i < capabilities.size(); ++i)
      enabled[i] = glIsEnabled(capabilities[i]);
  }

  ~State() {
    api.UseProgram(program);
    api.BindVertexArray(vao);
    api.BindBuffer(gl::kArrayBuffer, buffer);
    api.BindBuffer(gl::kPixelUnpackBuffer, unpackBuffer);
    api.BindFramebuffer(gl::kReadFramebuffer, read);
    api.BindFramebuffer(gl::kDrawFramebuffer, draw);
    for (int i = 0; i < 2; ++i) {
      api.ActiveTexture(gl::kTexture0 + i);
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      api.BindSampler(i, samplers[i]);
    }
    api.ActiveTexture(active);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    glPolygonMode(GL_FRONT, polygon[0]);
    glPolygonMode(GL_BACK, polygon[1]);
    api.BlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
    api.BlendEquationSeparate(equationRgb, equationAlpha);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthMask);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClearDepth(clearDepth);
    for (size_t i = 0; i < capabilities.size(); ++i)
      enable(capabilities[i], enabled[i] != GL_FALSE);
  }

  GLint read = 0;
  GLint draw = 0;

private:
  gl::Api &api;
  GLint program = 0, vao = 0, buffer = 0, unpackBuffer = 0, active = 0;
  GLint textures[2]{}, samplers[2]{}, viewport[4]{}, scissor[4]{}, polygon[2]{};
  GLint srcRgb = 0, dstRgb = 0, srcAlpha = 0, dstAlpha = 0;
  GLint equationRgb = 0, equationAlpha = 0;
  GLboolean colorMask[4]{}, depthMask = GL_FALSE;
  GLfloat clearColor[4]{};
  GLdouble clearDepth = 1;
  static constexpr std::array<GLenum, 13> capabilities{GL_BLEND,
                                                       GL_DEPTH_TEST,
                                                       GL_STENCIL_TEST,
                                                       GL_CULL_FACE,
                                                       GL_SCISSOR_TEST,
                                                       GL_ALPHA_TEST,
                                                       GL_COLOR_LOGIC_OP,
                                                       gl::kFramebufferSrgb,
                                                       gl::kRasterizerDiscard,
                                                       GL_POLYGON_OFFSET_FILL,
                                                       0x809E,
                                                       0x80A0,
                                                       0x0B42};
  // SAMPLE_ALPHA_TO_COVERAGE, SAMPLE_COVERAGE, POLYGON_STIPPLE.
  std::array<GLboolean, capabilities.size()> enabled{};
};

constexpr const char *kVertexShader = R"glsl(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 color;
uniform vec2 size;
uniform vec2 translation;
uniform int fullscreen;
out vec2 uv;
out vec4 tint;
void main() {
  if (fullscreen != 0) {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    uv = p;
    tint = vec4(1.0);
  } else {
    vec2 p = (position + translation) / size;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    uv = texcoord;
    tint = color;
  }
}
)glsl";

constexpr const char *kFragmentShader = R"glsl(#version 330 core
uniform sampler2D image;
uniform sampler2D maskImage;
uniform int mode;
uniform int flip;
uniform vec2 direction;
in vec2 uv;
in vec4 tint;
out vec4 result;
void main() {
  vec2 p = vec2(uv.x, flip != 0 ? 1.0 - uv.y : uv.y);
  if (mode == 0) {
    result = tint;
  } else if (mode == 3) {
    result = texture(image, p) * 0.2270270270;
    result += texture(image, p + direction * 1.3846153846) * 0.3162162162;
    result += texture(image, p - direction * 1.3846153846) * 0.3162162162;
    result += texture(image, p + direction * 3.2307692308) * 0.0702702703;
    result += texture(image, p - direction * 3.2307692308) * 0.0702702703;
  } else if (mode == 4) {
    vec4 c = texture(image, p);
    float coverage = sqrt(clamp(c.a, 0.0, 1.0));
    result = coverage > 0.0 ? vec4(c.rgb / coverage, coverage) : vec4(0.0);
  } else if (mode == 5) {
    vec4 c = texture(image, p);
    result = vec4(mix(vec3(1.0), c.rgb, c.a), 1.0);
  } else if (mode == 6) {
    // RGB window framebuffers have no stored coverage. The native model
    // renderer still writes depth for opaque and alpha-tested surfaces.
    float coverage = texture(maskImage, p).r < 1.0 ? 1.0 : 0.0;
    result = vec4(texture(image, p).rgb, coverage);
  } else {
    result = texture(image, p) * tint;
    if (mode == 2)
      result.a *= texture(maskImage, p).a;
  }
}
)glsl";

GLuint compile(GLenum type, const char *text) {
  auto &b = backend();
  const GLuint shader = b.api.CreateShader(type);
  if (!shader) {
    b.host.error("backend \"gl\": could not allocate shader");
    return 0;
  }
  b.api.ShaderSource(shader, 1, &text, nullptr);
  b.api.CompileShader(shader);
  GLint ok = 0;
  b.api.GetShaderiv(shader, gl::kCompileStatus, &ok);
  if (!ok) {
    char log[2048]{};
    b.api.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
    b.host.error("backend \"gl\": shader compilation failed: %s", log);
    b.api.DeleteShader(shader);
    return 0;
  }
  return shader;
}

void freeImage(Image &image) {
  auto &api = backend().api;
  if (image.framebuffer)
    api.DeleteFramebuffers(1, &image.framebuffer);
  if (image.texture)
    glDeleteTextures(1, &image.texture);
  image = {};
}

bool ensureImage(Image &image, int width, int height) {
  if (image.texture && image.width == width && image.height == height)
    return true;
  auto &b = backend();
  GLint limit = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
  if (width <= 0 || height <= 0 || width > limit || height > limit)
    return false;
  freeImage(image);
  b.api.ActiveTexture(gl::kTexture0);
  b.api.BindBuffer(gl::kPixelUnpackBuffer, 0);
  glGenTextures(1, &image.texture);
  glBindTexture(GL_TEXTURE_2D, image.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl::kClampToEdge);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl::kClampToEdge);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  b.api.GenFramebuffers(1, &image.framebuffer);
  b.api.BindFramebuffer(gl::kFramebuffer, image.framebuffer);
  b.api.FramebufferTexture2D(gl::kFramebuffer, gl::kColorAttachment0,
                             GL_TEXTURE_2D, image.texture, 0);
  if (!image.texture || !image.framebuffer ||
      b.api.CheckFramebufferStatus(gl::kFramebuffer) !=
          gl::kFramebufferComplete) {
    b.host.warn("backend \"gl\": RGBA8 framebuffer %dx%d unavailable", width,
                height);
    freeImage(image);
    return false;
  }
  image.width = width;
  image.height = height;
  return true;
}

void setup(int width, int height) {
  auto &b = backend();
  for (GLenum cap : std::array<GLenum, 12>{
           GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_SCISSOR_TEST,
           GL_ALPHA_TEST, GL_COLOR_LOGIC_OP, gl::kFramebufferSrgb,
           gl::kRasterizerDiscard, GL_POLYGON_OFFSET_FILL, GLenum(0x809E),
           GLenum(0x80A0), GLenum(0x0B42)})
    glDisable(cap);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_FALSE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glViewport(0, 0, width, height);
  b.api.UseProgram(b.program);
  b.api.BindVertexArray(b.vao);
  b.api.ActiveTexture(gl::kTexture0);
  b.api.BindSampler(0, b.sampler);
  b.api.BindSampler(1, b.sampler);
}

void fullscreen(Image &destination, GLuint source, int mode, bool flip = false,
                float dx = 0, float dy = 0) {
  auto &b = backend();
  setup(destination.width, destination.height);
  b.api.BindFramebuffer(gl::kDrawFramebuffer, destination.framebuffer);
  glDisable(GL_BLEND);
  glBindTexture(GL_TEXTURE_2D, source);
  b.api.Uniform1i(b.fullscreen, 1);
  b.api.Uniform1i(b.mode, mode);
  b.api.Uniform1i(b.flip, flip ? 1 : 0);
  b.api.Uniform2f(b.direction, dx, dy);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

bool current() {
  auto &b = backend();
  return b.ready && wglGetCurrentContext() == b.context;
}

void freeResources(bool destroy) {
  auto &b = backend();
  if (destroy) {
    for (auto &[handle, image] : b.targets)
      freeImage(image);
    for (auto &image : b.blur)
      freeImage(image);
    freeImage(b.capture);
    freeImage(b.captureDepth);
    freeImage(b.source);
    if (b.sampler)
      b.api.DeleteSamplers(1, &b.sampler);
    if (b.ebo)
      b.api.DeleteBuffers(1, &b.ebo);
    if (b.vbo)
      b.api.DeleteBuffers(1, &b.vbo);
    if (b.vao)
      b.api.DeleteVertexArrays(1, &b.vao);
    if (b.program)
      b.api.DeleteProgram(b.program);
  }
  b.targets.clear();
  b.blur = {};
  b.capture = {};
  b.captureDepth = {};
  b.source = {};
  b.program = b.vao = b.vbo = b.ebo = b.sampler = 0;
  b.ready = false;
  b.translations.clear();
}

bool createResources() {
  auto &b = backend();
  if (const char *missing = b.api.load()) {
    b.host.error("backend \"gl\": required OpenGL 3.3 function %s is missing",
                 missing);
    return false;
  }
  State saved(b.api);
  const GLuint vs = compile(gl::kVertexShader, kVertexShader);
  const GLuint fs = compile(gl::kFragmentShader, kFragmentShader);
  if (!vs || !fs) {
    if (vs)
      b.api.DeleteShader(vs);
    if (fs)
      b.api.DeleteShader(fs);
    return false;
  }
  b.program = b.api.CreateProgram();
  if (b.program) {
    b.api.AttachShader(b.program, vs);
    b.api.AttachShader(b.program, fs);
    b.api.LinkProgram(b.program);
  }
  b.api.DeleteShader(vs);
  b.api.DeleteShader(fs);
  GLint ok = 0;
  if (b.program)
    b.api.GetProgramiv(b.program, gl::kLinkStatus, &ok);
  if (!ok) {
    char log[2048]{};
    if (b.program)
      b.api.GetProgramInfoLog(b.program, sizeof(log), nullptr, log);
    b.host.error("backend \"gl\": shader program unavailable: %s", log);
    freeResources(true);
    return false;
  }
  b.api.UseProgram(b.program);
  b.size = b.api.GetUniformLocation(b.program, "size");
  b.translation = b.api.GetUniformLocation(b.program, "translation");
  b.mode = b.api.GetUniformLocation(b.program, "mode");
  b.fullscreen = b.api.GetUniformLocation(b.program, "fullscreen");
  b.flip = b.api.GetUniformLocation(b.program, "flip");
  b.direction = b.api.GetUniformLocation(b.program, "direction");
  b.api.Uniform1i(b.api.GetUniformLocation(b.program, "image"), 0);
  b.api.Uniform1i(b.api.GetUniformLocation(b.program, "maskImage"), 1);
  b.api.GenVertexArrays(1, &b.vao);
  b.api.GenBuffers(1, &b.vbo);
  b.api.GenBuffers(1, &b.ebo);
  b.api.GenSamplers(1, &b.sampler);
  b.api.SamplerParameteri(b.sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  b.api.SamplerParameteri(b.sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  b.api.SamplerParameteri(b.sampler, GL_TEXTURE_WRAP_S, gl::kClampToEdge);
  b.api.SamplerParameteri(b.sampler, GL_TEXTURE_WRAP_T, gl::kClampToEdge);
  if (!b.vao || !b.vbo || !b.ebo || !b.sampler ||
      !ensureImage(b.capture, 1, 1)) {
    b.host.error("backend \"gl\": could not allocate buffers or RGBA8 targets");
    freeResources(true);
    return false;
  }
  b.ready = true;
  return true;
}

int32_t ABCUI_CALL initialize(void *, const AbcUiRenderBackendHost *host) {
  auto &b = backend();
  b.host = *host;
  // Detection only; this DLL never acquires a Renderer interface or factory.
  if (GetModuleHandleA("Renderer.dll")) {
    b.host.error("backend \"gl\": Renderer.dll is loaded; remove it from "
                 "plugins.lst and fully restart the game");
    return 0;
  }
  b.context = wglGetCurrentContext();
  if (!b.context) {
    b.host.error("backend \"gl\": no current OpenGL context");
    return 0;
  }
  const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
  int major = 0, minor = 0;
  if (!version || sscanf_s(version, "%d.%d", &major, &minor) != 2 ||
      major < 3 || (major == 3 && minor < 3)) {
    b.host.error("backend \"gl\": OpenGL 3.3 or newer is required, found %s",
                 version ? version : "unknown");
    return 0;
  }
  if (!createResources())
    return 0;
  b.host.print("backend \"gl\": OpenGL %s, %s", version,
               reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
  return 1;
}

void ABCUI_CALL shutdown(void *) {
  auto &b = backend();
  b.sink = {};
  freeResources(current());
  b.context = nullptr;
}

int32_t ABCUI_CALL surfaceWidth(void *) {
  int32_t width = 0, height = 0;
  if (backend().host.engineScreenSize)
    backend().host.engineScreenSize(&width, &height);
  return width;
}

int32_t ABCUI_CALL surfaceHeight(void *) {
  int32_t width = 0, height = 0;
  if (backend().host.engineScreenSize)
    backend().host.engineScreenSize(&width, &height);
  return height;
}

void ABCUI_CALL installSink(void *, const AbcUiRenderFrameSink *sink) {
  backend().sink = sink ? *sink : AbcUiRenderFrameSink{};
}

void ABCUI_CALL removeSink(void *) {
  backend().sink = {};
}

void ABCUI_CALL pump(void *, double) {
  auto &b = backend();
  if (!wglGetCurrentContext())
    return;
  if (b.context != wglGetCurrentContext()) {
    // Old names belong to the lost context, not the replacement context.
    freeResources(false);
    b.context = wglGetCurrentContext();
    if (b.sink.invalidateTargets)
      b.sink.invalidateTargets(b.sink.userdata);
    if (!createResources()) {
      b.host.error("backend \"gl\": failed to recreate video resources");
      return;
    }
  }
  if (!b.ready)
    return;
  b.width = surfaceWidth(nullptr);
  b.height = surfaceHeight(nullptr);
  if (b.width > 0 && b.height > 0 && b.sink.renderFrame)
    b.sink.renderFrame(b.sink.userdata);
}

void ABCUI_CALL beginFrame(void *) {}
void ABCUI_CALL endFrame(void *) {
  backend().translations.clear();
}

void ABCUI_CALL set2D(void *, int32_t x, int32_t y, int32_t w, int32_t h) {
  auto &b = backend();
  b.clip[0] = x;
  b.clip[1] = y;
  b.clip[2] = w;
  b.clip[3] = h;
  b.translations.clear();
}

void ABCUI_CALL finish2D(void *) {
  backend().translations.clear();
}

void ABCUI_CALL pushTranslation(void *, float x, float y) {
  auto &stack = backend().translations;
  const auto previous = stack.empty() ? std::array<float, 2>{} : stack.back();
  stack.push_back({previous[0] + x, previous[1] + y});
}

void ABCUI_CALL popTranslation(void *) {
  auto &stack = backend().translations;
  if (!stack.empty())
    stack.pop_back();
}

template <class Vertex>
void draw(const Vertex *vertices, uint32_t vertexCount, const uint32_t *indices,
          uint32_t indexCount, int texture, int mask, int blend) {
  auto &b = backend();
  if (!current() || !vertices || !indices || !vertexCount || !indexCount ||
      indexCount % 3 || indexCount > std::numeric_limits<GLsizei>::max() ||
      b.clip[2] <= 0 || b.clip[3] <= 0 ||
      vertexCount >
          std::numeric_limits<std::ptrdiff_t>::max() / sizeof(Vertex) ||
      indexCount >
          std::numeric_limits<std::ptrdiff_t>::max() / sizeof(uint32_t))
    return;
  State saved(b.api);
  setup(b.clip[2], b.clip[3]);
  b.api.BindFramebuffer(gl::kDrawFramebuffer, 0);
  glViewport(b.clip[0], b.clip[1], b.clip[2], b.clip[3]);
  glEnable(GL_SCISSOR_TEST);
  glScissor(b.clip[0], b.clip[1], b.clip[2], b.clip[3]);
  glEnable(GL_BLEND);
  b.api.BlendEquationSeparate(gl::kFuncAdd, gl::kFuncAdd);
  b.api.BlendFuncSeparate(
      blend == ABCUI_RB_BLEND_PREMULTIPLIED_ALPHA ? GL_ONE : GL_SRC_ALPHA,
      blend == ABCUI_RB_BLEND_ADDITIVE ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA,
      GL_ONE,
      blend == ABCUI_RB_BLEND_ADDITIVE ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
  const auto offset =
      b.translations.empty() ? std::array<float, 2>{} : b.translations.back();
  b.api.Uniform2f(b.size, static_cast<float>(b.clip[2]),
                  static_cast<float>(b.clip[3]));
  b.api.Uniform2f(b.translation, offset[0], offset[1]);
  b.api.Uniform1i(b.fullscreen, 0);
  b.api.Uniform1i(b.flip, 0);
  constexpr bool textured = sizeof(Vertex) == sizeof(AbcUiRenderVertex);
  b.api.Uniform1i(b.mode, textured ? (mask ? 2 : 1) : 0);
  glBindTexture(GL_TEXTURE_2D, texture);
  b.api.ActiveTexture(gl::kTexture0 + 1);
  glBindTexture(GL_TEXTURE_2D, mask);
  b.api.BindBuffer(gl::kArrayBuffer, b.vbo);
  b.api.BufferData(gl::kArrayBuffer, vertexCount * sizeof(Vertex), vertices,
                   gl::kStreamDraw);
  b.api.BindBuffer(gl::kElementArrayBuffer, b.ebo);
  b.api.BufferData(gl::kElementArrayBuffer, indexCount * sizeof(uint32_t),
                   indices, gl::kStreamDraw);
  b.api.EnableVertexAttribArray(0);
  b.api.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
  b.api.EnableVertexAttribArray(2);
  b.api.VertexAttribPointer(
      2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
      reinterpret_cast<const void *>(offsetof(Vertex, color)));
  if constexpr (textured) {
    b.api.EnableVertexAttribArray(1);
    b.api.VertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<const void *>(offsetof(Vertex, texcoord)));
  } else {
    b.api.DisableVertexAttribArray(1);
  }
  glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
}

void ABCUI_CALL drawTextured(void *, const AbcUiRenderDraw *request) {
  if (request && request->structSize >= sizeof(*request))
    draw(request->vertices, request->vertexCount, request->indices,
         request->indexCount, request->texture, request->maskTexture,
         request->blend);
}

void ABCUI_CALL drawFilled(void *, const AbcUiRenderFilledDraw *request) {
  if (request && request->structSize >= sizeof(*request))
    draw(request->vertices, request->vertexCount, request->indices,
         request->indexCount, 0, 0, request->blend);
}

uint32_t ABCUI_CALL blurRender(void *, const AbcUiBlurPassRequest *request) {
  auto &b = backend();
  if (!current() || !request || request->structSize < sizeof(*request) ||
      request->width <= 0 || request->height <= 0)
    return 0;
  State saved(b.api);
  for (auto &image : b.blur)
    if (!ensureImage(image, request->width, request->height))
      return 0;
  GLuint source = request->sourceTexture;
  if (!source) {
    const int width = surfaceWidth(nullptr), height = surfaceHeight(nullptr);
    if (!ensureImage(b.source, width, height))
      return 0;
    glDisable(GL_SCISSOR_TEST);
    b.api.BindFramebuffer(gl::kReadFramebuffer, 0);
    b.api.BindFramebuffer(gl::kDrawFramebuffer, b.source.framebuffer);
    // UI textures use top-scanline-first rows, including live captures.
    b.api.BlitFramebuffer(0, 0, width, height, 0, height, width, 0,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    source = b.source.texture;
  }
  // The shared backdrop uploads movies in bottom-up FBO order. Live
  // captures were already flipped when copying the backbuffer above.
  fullscreen(b.blur[0], source, 5, request->sourceTexture != 0);
  Image *from = &b.blur[0];
  for (int round = 0; round < std::clamp(request->rounds, 0, 16); ++round) {
    fullscreen(b.blur[1], from->texture, 3, false, 1.0f / request->width, 0);
    fullscreen(b.blur[2], b.blur[1].texture, 3, false, 0,
               1.0f / request->height);
    from = &b.blur[2];
  }
  return from->texture;
}

void ABCUI_CALL blurInvalidate(void *) {
  auto &b = backend();
  if (current()) {
    State saved(b.api);
    for (auto &image : b.blur)
      freeImage(image);
    freeImage(b.source);
  } else {
    b.blur = {};
    b.source = {};
  }
}

uint32_t ABCUI_CALL targetCreate(void *, int32_t width, int32_t height) {
  auto &b = backend();
  if (!current())
    return 0;
  State saved(b.api);
  Image image;
  if (!ensureImage(image, width, height))
    return 0;
  do {
    ++b.nextTarget;
  } while (!b.nextTarget || b.targets.contains(b.nextTarget));
  b.targets.emplace(b.nextTarget, image);
  return b.nextTarget;
}

void ABCUI_CALL targetFree(void *, uint32_t handle) {
  auto &b = backend();
  auto it = b.targets.find(handle);
  if (it == b.targets.end())
    return;
  if (current())
    freeImage(it->second);
  b.targets.erase(it);
}

uint32_t ABCUI_CALL targetTexture(void *, uint32_t handle) {
  auto &b = backend();
  const auto it = b.targets.find(handle);
  return current() && it != b.targets.end() ? it->second.texture : 0;
}

void ABCUI_CALL clearViewport(void *, int32_t x, int32_t y, int32_t width,
                              int32_t height, const float *rgba) {
  if (!current() || !rgba || width <= 0 || height <= 0)
    return;
  State saved(backend().api);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, width, height);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
  glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
  glClearDepth(1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void ABCUI_CALL copyViewport(void *, uint32_t handle, int32_t x, int32_t y,
                             int32_t width, int32_t height) {
  auto &b = backend();
  const auto it = b.targets.find(handle);
  if (!current() || it == b.targets.end() || width <= 0 || height <= 0)
    return;
  State saved(b.api);
  GLint alphaBits = 0;
  GLint depthBits = 0;
  glGetIntegerv(GL_ALPHA_BITS, &alphaBits);
  glGetIntegerv(GL_DEPTH_BITS, &depthBits);
  if (!alphaBits && !depthBits)
    return;
  if (!ensureImage(b.capture, width, height) ||
      !ensureImage(it->second, width, height))
    return;
  glDisable(GL_SCISSOR_TEST);
  // StandaloneView owns all 3D work. Capture the framebuffer from its
  // OnAfterRenderView callback and normalize coverage on the GPU.
  b.api.BindFramebuffer(gl::kReadFramebuffer, saved.draw);
  b.api.BindFramebuffer(gl::kDrawFramebuffer, b.capture.framebuffer);
  b.api.BlitFramebuffer(x, y, x + width, y + height, 0, 0, width, height,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
  if (!alphaBits) {
    // Never redirect the native client's model pass into a private FBO:
    // it interprets that as a portal render and may access a missing player
    // during a map-free startup. Copy depth only AFTER StandaloneView draws.
    b.api.ActiveTexture(gl::kTexture0 + 1);
    auto &depth = b.captureDepth;
    if (!depth.texture)
      glGenTextures(1, &depth.texture);
    if (!depth.texture)
      return;
    glBindTexture(GL_TEXTURE_2D, depth.texture);
    if (depth.width != width || depth.height != height) {
      b.api.BindBuffer(gl::kPixelUnpackBuffer, 0);
      glTexImage2D(GL_TEXTURE_2D, 0, gl::kDepthComponent24, width, height, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
      depth.width = width;
      depth.height = height;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, width, height);
  }
  // Model elements already flip their bottom-left-origin capture once.
  fullscreen(it->second, b.capture.texture, alphaBits ? 4 : 6);
}

} // namespace

ABCUI_RENDER_BACKEND_EXPORT int32_t ABCUI_RENDER_BACKEND_CALL
AbcRenderBackend_Main(uint32_t abiVersion, const AbcUiRenderBackendHost *host,
                      AbcUiRenderBackendDesc *out) {
  if (abiVersion != ABCUI_RENDER_BACKEND_ABI_VERSION || !host || !out ||
      host->structSize < sizeof(*host) || !host->error || !host->warn ||
      !host->print || !host->engineScreenSize || out->structSize < sizeof(*out))
    return 0;
  *out = {};
  out->structSize = sizeof(*out);
  out->id = "gl";
  out->displayName = "OpenGL 3.3";
  out->capabilities = ABCUI_RB_CAP_REDRAW_BEAT | ABCUI_RB_CAP_BLUR_PASS |
                      ABCUI_RB_CAP_OFFSCREEN_TARGETS;
  out->initialize = initialize;
  out->shutdown = shutdown;
  out->surfaceWidth = surfaceWidth;
  out->surfaceHeight = surfaceHeight;
  out->installFrameSink = installSink;
  out->removeFrameSink = removeSink;
  out->pumpRedraw = pump;
  out->beginFrame = beginFrame;
  out->endFrame = endFrame;
  out->set2D = set2D;
  out->finish2D = finish2D;
  out->pushTranslation = pushTranslation;
  out->popTranslation = popTranslation;
  out->drawTextured = drawTextured;
  out->drawFilled = drawFilled;
  out->blurRender = blurRender;
  out->blurInvalidate = blurInvalidate;
  out->targetCreate = targetCreate;
  out->targetFree = targetFree;
  out->targetTexture = targetTexture;
  out->clearViewportRegion = clearViewport;
  out->copyViewportToTarget = copyViewport;
  return 1;
}
