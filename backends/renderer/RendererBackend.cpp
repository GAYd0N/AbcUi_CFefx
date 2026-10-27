// abcui_render_renderer.dll - the Renderer-backed AbcUi render backend
// (issue #24). This DLL is the ONLY place in the AbcUi tree that includes
// Renderer headers; the framework talks to it exclusively through the
// sdk/abcui/render_backend.h C ABI table.
//
// Behaviour-equal port of the former framework bridge (src/render/Render.cpp,
// the GPU halves of RadarRender.cpp / Backdrop.cpp and the RendererCallbacks
// frame beat from RmlUiModule.cpp):
//   - frame beat: IMetaRendererCallbacks::OnRenderEndFrame drives
//     sink->renderFrame, OnFreeFrameBuffers frees the backend-owned FBOs and
//     notifies sink->invalidateTargets, OnPreRenderView fires
//     sink->worldPassBeat (the radar world pass runs inside the Renderer's
//     frame, exactly where the old RadarCallbacks ran it);
//   - 2D drawing: DrawTexturedRect / DrawTexturedRectMask / DrawFilledRect
//     with the semantic blend mode mapped back onto Renderer's DRAW_* bits;
//   - world pass: the old top-down ortho RenderScene into an owned 1024^2
//     FBO (cvar save/restore and camera math stay framework-side);
//   - blur pass: the old down/ping/pong capture + gaussian chain;
//   - offscreen targets: viewport-capture textures for the in-house model
//     preview pass. These are the two fixed-pipeline GL niches with no
//     IMetaRenderer equivalent (scissor clear + copy-out); they run inside
//     this backend DLL with full save/restore of the touched GL state so
//     Renderer's cached bindings stay valid.

#include <metahook.h>

#include <GL/gl.h>

#include <IMetaRenderer.h>

#include <abcui/render_backend.h>

#include "RenderCommon.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <unordered_map>
#include <vector>

// GL/gl.h is the 1.1 header; the 1.2 clamp constant is used for capture
// textures (no GLEW dependency wanted here).
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {
void freeWorldTarget();
void freeBlurTargets();
void endPreviewBuffers();
void freePreviewBuffers();
} // namespace

// The ABI vertex POD must stay byte-identical to Renderer's vertex types -
// the draw calls cast the caller's arrays through. The framework used to pin
// this in AbcRenderInterface.cpp; the assert moved here with the types
// (R-14 lesson: mirror layouts are pinned where they are consumed).
static_assert(sizeof(AbcUiRenderVertex) == sizeof(texturedrectvertex_t));
static_assert(offsetof(AbcUiRenderVertex, texcoord) ==
              offsetof(texturedrectvertex_t, texcoord));
static_assert(offsetof(AbcUiRenderVertex, color) ==
              offsetof(texturedrectvertex_t, col));
static_assert(sizeof(AbcUiRenderFilledVertex) == sizeof(filledrectvertex_t));
static_assert(offsetof(AbcUiRenderFilledVertex, color) ==
              offsetof(filledrectvertex_t, col));

namespace {

AbcUiRenderBackendHost Host = {};  // copy of the host table
IMetaRenderer *Renderer = nullptr; // resolved in initialize, never nulled
AbcUiRenderFrameSink Sink = {};    // copy of the installed frame sink
bool CallbacksRegistered = false;

void hostError(const char *format, ...) {
  if (!Host.error)
    return;
  va_list args;
  va_start(args, format);
  // One level of indirection keeps the varargs site in this DLL.
  char text[512];
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  Host.error("%s", text);
}

// ------------------------------------------------------------------ 2D -----

uint64_t ABCUI_CALL programStateFor(int blend, uint64_t base) {
  switch (blend) {
  case ABCUI_RB_BLEND_ADDITIVE:
    return base == DRAW_TEXTURED_RECT_ALPHA_BLEND_ENABLED
               ? DRAW_TEXTURED_RECT_ALPHA_BASED_ADDITIVE_ENABLED
               : DRAW_FILLED_RECT_ADDITIVE_BLEND_ENABLED;
  case ABCUI_RB_BLEND_PREMULTIPLIED_ALPHA:
    // Renderer's public program-state table (API-002) has no premultiplied
    // bit: it composites with SRC_ALPHA only. The capture path below
    // normalizes offscreen targets to straight alpha, so straight alpha is
    // the correct program here; a future backend that can express ONE,
    // ONE_MINUS_SRC_ALPHA maps this mode itself.
    return DRAW_TEXTURED_RECT_ALPHA_BLEND_ENABLED;
  default:
    return base;
  }
}

int32_t ABCUI_CALL cbSurfaceWidth(void *) {
  return Renderer ? static_cast<int>(Renderer->GetSwapChainWidth()) : 0;
}
int32_t ABCUI_CALL cbSurfaceHeight(void *) {
  return Renderer ? static_cast<int>(Renderer->GetSwapChainHeight()) : 0;
}

void ABCUI_CALL cbBeginFrame(void *) {
  if (!Renderer)
    return;
  Renderer->PushFrameBuffer();
  Renderer->BindFrameBuffer(Renderer->GetFinalBufferFBO());
}

void ABCUI_CALL cbEndFrame(void *) {
  if (Renderer)
    Renderer->PopFrameBuffer();
}

void ABCUI_CALL cbSet2D(void *, int32_t x, int32_t y, int32_t w, int32_t h) {
  if (!Renderer)
    return;
  if (x == 0 && y == 0 &&
      w == static_cast<int>(Renderer->GetSwapChainWidth()) &&
      h == static_cast<int>(Renderer->GetSwapChainHeight()))
    Renderer->Set2D();
  else
    Renderer->Set2DEx(x, y, w, h);
}

void ABCUI_CALL cbFinish2D(void *) {
  if (Renderer)
    Renderer->Finish2D();
}

void ABCUI_CALL cbPushTranslation(void *, float x, float y) {
  if (!Renderer)
    return;
  Renderer->PushWorldMatrix();
  Renderer->TranslateWorldMatrix(x, y, 0.0f);
}

void ABCUI_CALL cbPopTranslation(void *) {
  if (Renderer)
    Renderer->PopWorldMatrix();
}

void ABCUI_CALL cbDrawTextured(void *, const AbcUiRenderDraw *draw) {
  if (!Renderer || !draw)
    return;
  const auto *vertices =
      reinterpret_cast<const texturedrectvertex_t *>(draw->vertices);
  uint64_t program =
      programStateFor(draw->blend, DRAW_TEXTURED_RECT_ALPHA_BLEND_ENABLED);
  if (draw->maskTexture != 0) {
    program |= DRAW_TEXTURED_RECT_MASK_TEXTURE_ENABLED;
    Renderer->DrawTexturedRectMask(draw->texture, draw->maskTexture, vertices,
                                   draw->vertexCount, draw->indices,
                                   draw->indexCount, program, draw->label);
  } else {
    Renderer->DrawTexturedRect(draw->texture, vertices, draw->vertexCount,
                               draw->indices, draw->indexCount, program,
                               draw->label);
    // DrawTexturedRect unbinds with raw GL, leaving the engine's cached
    // currenttexture stale. A rebuilt atlas can reuse that deleted id:
    // the surface's GL_Bind then skips binding and uploads into texture 0.
    // Synchronize both bindings through Renderer before surface work resumes.
    Renderer->BindTextureUnit(0, GL_TEXTURE_2D, 0);
  }
}

void ABCUI_CALL cbDrawFilled(void *, const AbcUiRenderFilledDraw *draw) {
  if (!Renderer || !draw)
    return;
  const auto *vertices =
      reinterpret_cast<const filledrectvertex_t *>(draw->vertices);
  const uint64_t program =
      programStateFor(draw->blend, DRAW_FILLED_RECT_ALPHA_BLEND_ENABLED);
  Renderer->DrawFilledRect(vertices, draw->vertexCount, draw->indices,
                           draw->indexCount, program, draw->label);
}

// ---------------------------------------------------------- frame beat -----

class BackendCallbacks : public IMetaRendererCallbacks {
public:
  void OnBeginRendering(int *, int *, int *, int *) override {}
  void OnRenderFrameStart() override {}
  void OnBeginRenderingPost(int *, int *, int *, int *) override {}
  void OnFreeFrameBuffers() override {
    // The engine regenerates every FBO here; the backend-owned targets die
    // with them. Recration is lazy (next ensure/render call), and the sink
    // drops the framework-side registrations.
    freeWorldTarget();
    freeBlurTargets();
    freePreviewBuffers();
    if (Sink.invalidateTargets)
      Sink.invalidateTargets(Sink.userdata);
  }
  void OnGenerateFrameBuffers() override {}
  void OnPreRenderView() override {
    if (Sink.worldPassBeat)
      Sink.worldPassBeat(Sink.userdata);
  }
  void OnPreRenderViewPost() override {}
  void OnPostRenderView() override {}
  void OnPostRenderViewPost() override {}
  void OnRenderEndFrame() override {
    endPreviewBuffers();
    if (Sink.renderFrame)
      Sink.renderFrame(Sink.userdata);
  }
};

BackendCallbacks Callbacks;

// The Renderer backend is driven by its render callbacks; the framework's
// HUD_Redraw pump has nothing to do.
void ABCUI_CALL cbPumpRedraw(void *, double) {}

void ABCUI_CALL cbInstallFrameSink(void *, const AbcUiRenderFrameSink *sink) {
  if (!Renderer || !sink || sink->structSize < sizeof(AbcUiRenderFrameSink))
    return;
  Sink = *sink;
  if (!CallbacksRegistered) {
    CallbacksRegistered = true;
    Renderer->RegisterRenderCallbacks(&Callbacks);
  }
}

void ABCUI_CALL cbRemoveFrameSink(void *) {
  Sink = {};
  if (Renderer && CallbacksRegistered) {
    CallbacksRegistered = false;
    Renderer->UnregisterRenderCallbacks(&Callbacks);
  }
}

// ------------------------------------------------------------ world pass ---

FBO_Container_t WorldFbo = {};
bool WorldReady = false;

void freeWorldTarget() {
  if (!WorldReady)
    return;
  if (Renderer)
    Renderer->FreeFBO(&WorldFbo);
  WorldFbo = {};
  WorldReady = false;
}

uint32_t ABCUI_CALL cbWorldEnsureTarget(void *, int32_t width, int32_t height) {
  if (!Renderer || width <= 0 || height <= 0)
    return 0;
  if (WorldReady && WorldFbo.iWidth == width && WorldFbo.iHeight == height)
    return WorldFbo.s_hBackBufferTex;
  freeWorldTarget();
  WorldFbo.iWidth = width;
  WorldFbo.iHeight = height;
  Renderer->GenFrameBuffer(&WorldFbo, "AbcUiRadarFBO");
  Renderer->FrameBufferColorTexture(&WorldFbo, GL_RGBA8);
  Renderer->FrameBufferDepthTexture(&WorldFbo, GL_DEPTH24_STENCIL8);
  WorldReady = WorldFbo.s_hBackBufferTex != 0;
  return WorldFbo.s_hBackBufferTex;
}

void ABCUI_CALL cbWorldFreeTarget(void *) {
  freeWorldTarget();
}

uint32_t ABCUI_CALL cbWorldTexture(void *) {
  return WorldReady ? WorldFbo.s_hBackBufferTex : 0;
}

void ABCUI_CALL cbWorldRender(void *, const AbcUiWorldPassRequest *request) {
  if (!Renderer || !request || !WorldReady)
    return;
  // Old CRadarPanel::RenderRadar GPU half (framework saves the draw cvars
  // around this call): ortho top-down view into the owned FBO, world class
  // only, via multiview so R_SetupGL leaves the matrices to us.
  Renderer->BeginDebugGroup("AbcUi::RadarRender");
  Renderer->BindFrameBuffer(&WorldFbo);
  Renderer->SetCurrentSceneFBO(&WorldFbo);

  Renderer->PushRefDef();
  Renderer->SetMultiviewEnabled(true);
  const int oldDrawClassify = Renderer->GetDrawClassify();
  Renderer->SetDrawClassify(DRAW_CLASSIFY_WORLD | DRAW_CLASSIFY_LIGHTMAP);
  Renderer->SetRefDefViewOrigin(request->origin);
  Renderer->SetRefDefViewAngles(request->angles);
  Renderer->UpdateRefDef();
  Renderer->LoadIdentityForProjectionMatrix();
  const float half = request->orthoHalfExtent;
  Renderer->SetupOrthoProjectionMatrix(
      -half, half, -half, half, request->orthoNear, request->orthoFar, true);
  Renderer->LoadIdentityForWorldMatrix();
  Renderer->SetupPlayerViewWorldMatrix(request->origin, request->angles);
  Renderer->SetViewport(0, 0, static_cast<float>(WorldFbo.iWidth),
                        static_cast<float>(WorldFbo.iHeight));

  camera_ubo_t cameraUbo = {};
  Renderer->SetupCameraView(&cameraUbo.views[0]);
  cameraUbo.numViews = 1;
  Renderer->UploadCameraUBOData(&cameraUbo);

  float clear[4] = {request->clearColor[0], request->clearColor[1],
                    request->clearColor[2], request->clearColor[3]};
  Renderer->ClearColorDepthStencil(clear, 1.0f, 0, STENCIL_MASK_ALL);
  Renderer->RenderScene();

  Renderer->SetDrawClassify(oldDrawClassify);
  Renderer->SetMultiviewEnabled(false);
  Renderer->PopRefDef();
  Renderer->EndDebugGroup();
}

// ------------------------------------------------------------- blur pass ---

struct BlurTargets {
  FBO_Container_t down{};
  FBO_Container_t ping{};
  FBO_Container_t pong{};
  bool valid = false;
  int width = 0;
  int height = 0;
} Blur;

void freeBlurTargets() {
  if (!Blur.valid)
    return;
  if (Renderer) {
    Renderer->PushFrameBuffer();
    for (FBO_Container_t *target : {&Blur.down, &Blur.ping, &Blur.pong})
      if (target->s_hBackBufferFBO)
        Renderer->FreeFBO(target);
    Renderer->PopFrameBuffer();
  }
  Blur = {};
}

uint32_t ABCUI_CALL cbBlurRender(void *, const AbcUiBlurPassRequest *request) {
  if (!Renderer || !request || request->width <= 0 || request->height <= 0)
    return 0;
  // Source 0 = the final buffer (menu backdrop live capture).
  unsigned int sourceTexture = request->sourceTexture;
  if (!sourceTexture) {
    if (FBO_Container_t * final = Renderer->GetFinalBufferFBO())
      sourceTexture = final->s_hBackBufferTex;
    if (!sourceTexture)
      return 0;
  }
  if (!Blur.valid || Blur.width != request->width ||
      Blur.height != request->height) {
    freeBlurTargets();
    Renderer->PushFrameBuffer();
    const struct {
      FBO_Container_t *target;
      const char *name;
    } targets[] = {{&Blur.down, "AbcUi backdrop source"},
                   {&Blur.ping, "AbcUi backdrop blur A"},
                   {&Blur.pong, "AbcUi backdrop blur B"}};
    for (const auto &target : targets) {
      target.target->iWidth = request->width;
      target.target->iHeight = request->height;
      Renderer->GenFrameBuffer(target.target, target.name);
      Renderer->FrameBufferColorTexture(target.target, GL_RGBA8);
    }
    Renderer->PopFrameBuffer();
    Blur.valid = true;
    Blur.width = request->width;
    Blur.height = request->height;
  }

  // Capture: an opaque white fill first, then the source alpha-blended over
  // it, so the blur chain's alpha is 1 whatever the source alpha holds.
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  Renderer->PushFrameBuffer();
  Renderer->BindFrameBuffer(&Blur.down);
  Renderer->Set2DEx(0, 0, request->width, request->height);
  Renderer->ClearColor(const_cast<float *>(white));
  Renderer->DrawFilledQuad(0, 0, request->width, request->height, white,
                           DRAW_FILLED_RECT_ALPHA_BLEND_ENABLED,
                           "abcui backdrop fill");
  Renderer->DrawTexturedQuad(
      static_cast<int>(sourceTexture), 0, 0, request->width, request->height,
      white, DRAW_TEXTURED_RECT_ALPHA_BLEND_ENABLED, "abcui backdrop capture");
  Renderer->Finish2D();
  Renderer->PopFrameBuffer();

  // Gaussian rounds: horizontal then vertical, ping-ponging (Renderer's own
  // bloom chain shape). Zero rounds keep the raw capture.
  FBO_Container_t *from = &Blur.down;
  const int rounds = request->rounds > 0 ? request->rounds : 0;
  for (int round = 0; round < rounds; round++) {
    Renderer->BlurPass(from, &Blur.ping, 1.0f, false);
    Renderer->BlurPass(&Blur.ping, &Blur.pong, 1.0f, true);
    from = &Blur.pong;
  }
  return from->s_hBackBufferTex;
}

void ABCUI_CALL cbBlurInvalidate(void *) {
  freeBlurTargets();
}

// ------------------------------------------------------ offscreen targets --

// Renderer creates its scene UBOs on map load, not at GL initialization.
// An engine-onlyClientDraw pass can run before that: its matrices are valid,
// but uploading to the absent UBOs leaves studio shaders without a camera and
// the final gamma pass without scene constants. Supply only missing indexed
// bindings, after engine setup and until the viewport has been copied out.
class PreviewBuffers {
public:
  void begin() {
    end();
    if (!getIndexed) {
      getIndexed =
          reinterpret_cast<GetIndexedFn>(wglGetProcAddress("glGetIntegeri_v"));
      bindBase =
          reinterpret_cast<BindBaseFn>(wglGetProcAddress("glBindBufferBase"));
    }
    if (!getIndexed || !bindBase || !Renderer)
      return;

    GLint generic = 0;
    glGetIntegerv(kUniformBufferBinding, &generic);
    for (unsigned int i = 0; i < 3; ++i) {
      GLint bound = 0;
      getIndexed(kUniformBufferBinding, i, &bound);
      if (bound)
        continue;
      if (!buffers[i])
        buffers[i] = Renderer->GenBuffer();
      if (!buffers[i])
        continue;
      if (i == BINDING_POINT_SCENE_UBO) {
        scene_ubo_t scene{};
        scene.r_g = scene.v_lightgamma = scene.v_lambert = 1.0f;
        scene.v_gamma = scene.v_texgamma = scene.r_lightscale = 1.0f;
        scene.r_lightmap_scale = scene.r_lightmap_pow = 1.0f;
        scene.r_g3 = 0.125f;
        scene.r_alphamin = 0.01f;
        Renderer->UploadDataToUBODynamicDraw(buffers[i], sizeof(scene), &scene);
      } else if (i == BINDING_POINT_CAMERA_UBO) {
        camera_ubo_t camera{};
        Renderer->SetupCameraView(&camera.views[0]);
        camera.numViews = 1;
        Renderer->UploadDataToUBODynamicDraw(buffers[i], sizeof(camera),
                                             &camera);
      } else {
        const dlight_ubo_t lights{};
        Renderer->UploadDataToUBODynamicDraw(buffers[i], sizeof(lights),
                                             &lights);
      }
      bindBase(kUniformBuffer, i, buffers[i]);
      replaced[i] = true;
    }
    // BindBufferBase also changes the generic binding. Restore it without
    // disturbing any indexed binding (including ranges owned by other passes).
    bindGeneric(static_cast<GLuint>(generic));
  }

  void end() {
    if (!bindBase || !(replaced[0] || replaced[1] || replaced[2]))
      return;
    GLint generic = 0;
    glGetIntegerv(kUniformBufferBinding, &generic);
    for (unsigned int i = 0; i < 3; ++i) {
      if (replaced[i])
        bindBase(kUniformBuffer, i, 0);
      replaced[i] = false;
    }
    bindGeneric(static_cast<GLuint>(generic));
  }

  void free() {
    end();
    for (auto &buffer : buffers) {
      if (buffer && Renderer)
        Renderer->DeleteBuffer(buffer);
      buffer = 0;
    }
  }

private:
  using GetIndexedFn = void(APIENTRY *)(GLenum, GLuint, GLint *);
  using BindBaseFn = void(APIENTRY *)(GLenum, GLuint, GLuint);
  using BindFn = void(APIENTRY *)(GLenum, GLuint);
  static constexpr GLenum kUniformBuffer = 0x8A11;
  static constexpr GLenum kUniformBufferBinding = 0x8A28;

  void bindGeneric(GLuint buffer) {
    if (!bind)
      bind = reinterpret_cast<BindFn>(wglGetProcAddress("glBindBuffer"));
    if (bind)
      bind(kUniformBuffer, buffer);
  }

  GetIndexedFn getIndexed = nullptr;
  BindBaseFn bindBase = nullptr;
  BindFn bind = nullptr;
  GLuint buffers[3] = {};
  bool replaced[3] = {};
};

PreviewBuffers &previewBuffers() {
  static PreviewBuffers instance;
  return instance;
}

void endPreviewBuffers() {
  previewBuffers().end();
}

void freePreviewBuffers() {
  previewBuffers().free();
}

struct CaptureTarget {
  GLuint texture = 0;
  int width = 0;
  int height = 0;
  bool hasImage = false;
};
std::unordered_map<unsigned int, CaptureTarget> Targets;
unsigned int NextTargetHandle = 0;
// Reused readback buffer for the straight-alpha capture normalization.
std::vector<float> CapturePixels;

CaptureTarget *findTarget(unsigned int handle) {
  const auto it = Targets.find(handle);
  return it == Targets.end() ? nullptr : &it->second;
}

uint32_t ABCUI_CALL cbTargetCreate(void *, int32_t width, int32_t height) {
  if (width <= 0 || height <= 0)
    return 0;
  GLuint texture = 0;
  glGenTextures(1, &texture);
  if (!texture)
    return 0;
  const unsigned int handle = ++NextTargetHandle;
  Targets[handle] = CaptureTarget{texture, width, height, false};
  return handle;
}

void ABCUI_CALL cbTargetFree(void *, uint32_t handle) {
  const auto it = Targets.find(handle);
  if (it == Targets.end())
    return;
  glDeleteTextures(1, &it->second.texture);
  Targets.erase(it);
}

uint32_t ABCUI_CALL cbTargetTexture(void *, uint32_t handle) {
  const CaptureTarget *target = findTarget(handle);
  return target ? target->texture : 0;
}

void ABCUI_CALL cbClearViewportRegion(void *, int32_t x, int32_t y,
                                      int32_t width, int32_t height,
                                      const float *rgba) {
  if (!rgba || width <= 0 || height <= 0)
    return;
  // Scissor-limited clear of the CURRENT framebuffer region. Fixed-pipeline
  // GL: IMetaRenderer has no regional clear, and the model preview pass must
  // not stomp the frame outside its own viewport. Everything touched is
  // saved and restored so Renderer's cached GL state stays valid.
  GLint scissorBox[4] = {};
  GLboolean scissorTest = glIsEnabled(GL_SCISSOR_TEST);
  GLboolean colorMask[4] = {};
  GLboolean depthMask = GL_FALSE;
  GLfloat clearColor[4] = {};
  GLdouble clearDepth = 1;
  glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
  glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
  glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, width, height);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
  glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
  glClearDepth(1);
  // Do not clear stencil: the renderer owns the pass's stencil classification.
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
  glClearDepth(clearDepth);
  glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
  glDepthMask(depthMask);
  glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
  if (scissorTest == GL_FALSE)
    glDisable(GL_SCISSOR_TEST);
  previewBuffers().begin();
}

// The engine's Studio pass blends with glBlendFunc(SRC_ALPHA,
// ONE_MINUS_SRC_ALPHA), so the colour written into the pre-composite scene
// is premultiplied while the alpha channel accumulates coverage squared.
// Recover the straight-alpha image the 2D channel expects: coverage is the
// square root of that accumulated alpha, and the stored colour is already
// multiplied by that coverage. One layer over a transparent clear is exact;
// nested surfaces approximate because only the aggregate alpha survives.
void normalizeCapture(std::vector<float> &pixels, std::size_t pixelCount) {
  for (std::size_t i = 0; i < pixelCount; ++i) {
    float *pixel = pixels.data() + i * 4;
    float alpha = pixel[3];
    if (!(alpha > 0.0f)) {
      pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0.0f;
      continue;
    }
    if (alpha >= 1.0f) {
      pixel[3] = 1.0f;
      continue;
    }
    const float coverage = std::sqrt(alpha);
    const float inverse = 1.0f / coverage;
    pixel[0] *= inverse;
    pixel[1] *= inverse;
    pixel[2] *= inverse;
    pixel[3] = coverage;
  }
}

void ABCUI_CALL cbCopyViewportToTarget(void *, uint32_t handle, int32_t x,
                                       int32_t y, int32_t width,
                                       int32_t height) {
  CaptureTarget *target = findTarget(handle);
  if (!target || width <= 0 || height <= 0)
    return;
  // V_RenderView has already composited the scene onto the opaque final
  // buffer. Capture its gamma-corrected scene instead: the final buffer has
  // lost the transparent background a model element needs.
  Renderer->PushFrameBuffer();
  Renderer->BindFrameBuffer(Renderer->GetBackBufferFBO());
  GLint previousTexture = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
  glBindTexture(GL_TEXTURE_2D, target->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // Read the float scene and normalize it before the 8-bit upload: the
  // premultiplied-to-straight step needs the extra alpha precision.
  const std::size_t pixelCount =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  CapturePixels.resize(pixelCount * 4);
  glReadPixels(x, y, width, height, GL_RGBA, GL_FLOAT, CapturePixels.data());
  normalizeCapture(CapturePixels, pixelCount);
  if (!target->hasImage || target->width != width || target->height != height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_FLOAT, CapturePixels.data());
    target->hasImage = true;
    target->width = width;
    target->height = height;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT,
                    CapturePixels.data());
  }
  glBindTexture(GL_TEXTURE_2D, previousTexture);
  Renderer->PopFrameBuffer();
  endPreviewBuffers();
}

// ------------------------------------------------------------- lifecycle ---

int32_t ABCUI_CALL cbInitialize(void *, const AbcUiRenderBackendHost *host) {
  if (!host || !host->sysGetFactory || !host->pluginModule ||
      host->structSize <
          offsetof(AbcUiRenderBackendHost, pluginModule) + sizeof(void *))
    return 0;
  Host = *host;
  void *module = Host.pluginModule("Renderer");
  if (!module) {
    hostError("backend \"renderer\": the Renderer plugin is not loaded - "
              "AbcUi needs Renderer.dll in metahook/plugins, or select "
              "another backend with render_backend in abc/abcui/config.json");
    return 0;
  }
  const auto factory =
      reinterpret_cast<CreateInterfaceFn>(Host.sysGetFactory(module));
  if (!factory) {
    hostError("backend \"renderer\": could not get factory from Renderer");
    return 0;
  }
  Renderer = static_cast<IMetaRenderer *>(
      factory(METARENDERER_INTERFACE_VERSION, nullptr));
  if (!Renderer) {
    hostError("backend \"renderer\": could not get interface %s from Renderer",
              METARENDERER_INTERFACE_VERSION);
    return 0;
  }
  return 1;
}

void ABCUI_CALL cbShutdown(void *) {
  cbRemoveFrameSink(nullptr);
  freeWorldTarget();
  freeBlurTargets();
  freePreviewBuffers();
  for (auto &entry : Targets)
    glDeleteTextures(1, &entry.second.texture);
  Targets.clear();
  // Renderer stays loaded (the MetaHook host owns the plugin); the pointer
  // deliberately survives so late teardown calls remain safe.
}

} // namespace

ABCUI_RENDER_BACKEND_EXPORT int32_t ABCUI_RENDER_BACKEND_CALL
AbcRenderBackend_Main(uint32_t abiVersion, const AbcUiRenderBackendHost *host,
                      AbcUiRenderBackendDesc *out) {
  if (abiVersion != ABCUI_RENDER_BACKEND_ABI_VERSION || !host || !out ||
      out->structSize < sizeof(AbcUiRenderBackendDesc))
    return 0;

  out->id = "renderer";
  out->displayName = "Renderer.dll (IMetaRenderer)";
  out->capabilities = ABCUI_RB_CAP_RENDER_CALLBACKS | ABCUI_RB_CAP_WORLD_PASS |
                      ABCUI_RB_CAP_BLUR_PASS | ABCUI_RB_CAP_OFFSCREEN_TARGETS;
  out->userdata = nullptr;
  out->initialize = cbInitialize;
  out->shutdown = cbShutdown;
  out->surfaceWidth = cbSurfaceWidth;
  out->surfaceHeight = cbSurfaceHeight;
  out->installFrameSink = cbInstallFrameSink;
  out->removeFrameSink = cbRemoveFrameSink;
  out->pumpRedraw = cbPumpRedraw; // no-op: the callback beat drives us
  out->beginFrame = cbBeginFrame;
  out->endFrame = cbEndFrame;
  out->set2D = cbSet2D;
  out->finish2D = cbFinish2D;
  out->pushTranslation = cbPushTranslation;
  out->popTranslation = cbPopTranslation;
  out->drawTextured = cbDrawTextured;
  out->drawFilled = cbDrawFilled;

  out->worldEnsureTarget = cbWorldEnsureTarget;
  out->worldFreeTarget = cbWorldFreeTarget;
  out->worldTexture = cbWorldTexture;
  out->worldRender = cbWorldRender;

  out->blurRender = cbBlurRender;
  out->blurInvalidate = cbBlurInvalidate;

  out->targetCreate = cbTargetCreate;
  out->targetFree = cbTargetFree;
  out->targetTexture = cbTargetTexture;
  out->clearViewportRegion = cbClearViewportRegion;
  out->copyViewportToTarget = cbCopyViewportToTarget;
  return 1;
}
