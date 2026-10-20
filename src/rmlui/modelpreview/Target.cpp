// New implementation; see Target.h. The preview composes the Renderer's
// entity-view primitives: this service owns the source, the camera, the light
// and the animation, and the Renderer owns the isolated render scope. Model
// loading goes through Renderer::LoadStudioModel, including mounted game
// search paths.
#include <metahook.h>
#include "Target.h"

#include <com_model.h>
#include <studio.h>
#include <IEntityViewRenderer.h>
#include <abc/Console.h>
#include <abc/Plugin.h>
#include "../../render/Render.h"
#include "../../render/RenderCommon.h"
#include "../../rmlui/AbcRenderInterface.h"
#include "../../rmlui/RmlUiModule.h"

#include <algorithm>
#include <cmath>
#include <vector>

#undef min
#undef max

namespace modelpreview {
namespace {
std::vector<Target *> &targets() {
  static std::vector<Target *> Targets;
  return Targets;
}
IEntityViewRenderer *entityViewRenderer() {
  static IEntityViewRenderer *Views = nullptr;
  if (!Views) {
    mh_plugininfo_t Info{};
    if (g_pMetaHookAPI->GetPluginInfoByBaseFileName("Renderer", &Info)) {
      const auto Factory =
          Sys_GetFactory((HINTERFACEMODULE)Info.PluginModuleBase);
      if (Factory)
        Views = static_cast<IEntityViewRenderer *>(
            Factory(ENTITY_VIEW_RENDERER_INTERFACE_VERSION, nullptr));
    }
  }
  return Views;
}
// The Renderer draws the entity with exactly the light it is given, so the
// card's "lighting" switch is resolved here: off means the flat ambient light
// the card was tuned with.
entity_light_t buildLight(const PreviewView &View) {
  entity_light_t Light{};
  if (View.Lighting) {
    Light.ambientlight = View.Ambient;
    Light.shadelight = View.Shade;
    std::copy(View.LightColor.begin(), View.LightColor.end(), Light.color);
  } else {
    Light.ambientlight = 192;
    Light.shadelight = 0;
    Light.color[0] = Light.color[1] = Light.color[2] = 1.0f;
  }
  std::copy(View.LightDirection.begin(), View.LightDirection.end(),
            Light.direction);
  return Light;
}
} // namespace

struct Target::State {
  FBO_Container_t Fbo{};
  cl_entity_t Entity{};
  std::string Source;
  std::string Uri;
  unsigned int Frames = 0;
  bool Attempted = false;
  bool Warned = false;
  double LastTime = 0;
  float AnimationFrame = 0;
  int LastSequence = -1;
  float LastFrame = -1;
};

Target::Target() : Data(std::make_unique<State>()) {
  static unsigned long long NextId = 0;
  Data->Uri = "modelfbo://" + std::to_string(++NextId) + "/";
  targets().push_back(this);
}

Target::~Target() {
  invalidate();
  std::erase(targets(), this);
}

void Target::invalidate() {
  if (auto *Interface = rmlui::renderInterface())
    Interface->unregisterExternalTexture(Data->Uri);
  if (auto *Renderer = abc::MetaRenderer();
      Renderer && Data->Fbo.s_hBackBufferFBO)
    Renderer->FreeFBO(&Data->Fbo);
  Data->Fbo = {};
  Data->Entity = {};
  Data->Attempted = false;
  Data->LastSequence = -1;
  Data->LastTime = 0;
}

bool Target::render(const Options &Config, int Width, int Height) {
  auto *Renderer = abc::MetaRenderer();
  auto *Views = entityViewRenderer();
  if (!Renderer || !Views) {
    if (!Data->Warned) {
      abc::ConsoleWarn("[AbcUi model] Renderer lacks %s; preview disabled\n",
                       ENTITY_VIEW_RENDERER_INTERFACE_VERSION);
      Data->Warned = true;
    }
    return false;
  }
  if (Data->Source != Config.Source) {
    invalidate();
    Data->Source = Config.Source;
  }
  if (Config.Source.empty() || Width <= 0 || Height <= 0)
    return false;
  if (!Data->Attempted) {
    Data->Attempted = true;
    Data->Entity.model = Views->LoadStudioModel(Config.Source.c_str());
    if (!Data->Entity.model) {
      abc::ConsoleWarn("[AbcUi model] cannot load Studio model: %s\n",
                       Config.Source.c_str());
      return false;
    }
  }
  if (!Data->Entity.model)
    return false;
  Width = std::clamp(Width, 1, 2048);
  Height = std::clamp(Height, 1, 2048);
  if (Data->Fbo.iWidth != Width || Data->Fbo.iHeight != Height) {
    Renderer->PushFrameBuffer();
    if (Data->Fbo.s_hBackBufferFBO)
      Renderer->FreeFBO(&Data->Fbo);
    Data->Fbo = {};
    Data->Fbo.iWidth = Width;
    Data->Fbo.iHeight = Height;
    Renderer->GenFrameBuffer(&Data->Fbo, "AbcUi model");
    Renderer->FrameBufferColorTexture(&Data->Fbo, GL_RGBA8);
    Renderer->FrameBufferDepthTexture(&Data->Fbo, GL_DEPTH24_STENCIL8);
    Renderer->PopFrameBuffer();
  }
  auto &Entity = Data->Entity;
  auto &State = Entity.curstate;
  State.rendermode = Config.RenderMode;
  State.renderfx = Config.RenderFx;
  State.renderamt = Config.RenderAmount;
  State.rendercolor = {static_cast<byte>(Config.RenderColor[0]),
                       static_cast<byte>(Config.RenderColor[1]),
                       static_cast<byte>(Config.RenderColor[2])};
  State.sequence = static_cast<short>(std::clamp(Config.Sequence, 0, 32767));
  State.skin = static_cast<short>(std::clamp(Config.Skin, 0, 32767));
  State.body = Config.Body;
  State.scale = Config.Scale;
  State.colormap = Config.ColorMap;
  Entity.mouth.mouthopen = static_cast<byte>(Config.Mouth);
  for (int i = 0; i < 4; ++i) {
    State.controller[i] = static_cast<byte>(Config.Controllers[i]);
    Entity.latched.prevcontroller[i] = State.controller[i];
  }
  for (int i = 0; i < 2; ++i) {
    State.blending[i] = static_cast<byte>(Config.Blending[i]);
    Entity.latched.prevblending[i] = State.blending[i];
  }
  State.framerate =
      0; // Animation is advanced using UI time, even without a server.
  std::copy(Config.Origin.begin(), Config.Origin.end(), Entity.origin);
  std::copy(Config.Origin.begin(), Config.Origin.end(), State.origin);
  std::copy(Config.Angles.begin(), Config.Angles.end(), Entity.angles);
  std::copy(Config.Angles.begin(), Config.Angles.end(), State.angles);
  const double Time = gEngfuncs.GetAbsoluteTime();
  if (Data->LastSequence != Config.Sequence ||
      Data->LastFrame != Config.Frame) {
    Data->AnimationFrame = Config.Frame;
    Data->LastSequence = Config.Sequence;
    Data->LastFrame = Config.Frame;
  }
  // Use the model's own FPS/frame count, not a hard-coded 30 FPS.
  auto *Header = static_cast<studiohdr_t *>(Views->GetModelData(Entity.model));
  if (!Header || Header->numseq <= 0 || Header->length <= 0)
    return false;
  // Bound the sequence table against the model's own byte length before
  // indexing it: seqindex comes from the (untrusted) .mdl file.
  const auto HeaderSize = static_cast<std::size_t>(Header->length);
  const auto SeqOffset = static_cast<std::size_t>(Header->seqindex);
  const auto SeqBytes =
      static_cast<std::size_t>(Header->numseq) * sizeof(mstudioseqdesc_t);
  if (SeqOffset > HeaderSize || SeqBytes > HeaderSize - SeqOffset)
    return false;
  State.sequence =
      static_cast<short>(std::clamp(Config.Sequence, 0, Header->numseq - 1));
  State.skin = static_cast<short>(
      std::clamp(Config.Skin, 0, std::max(Header->numskinfamilies - 1, 0)));
  const auto *Sequence =
      reinterpret_cast<const mstudioseqdesc_t *>(
          reinterpret_cast<const byte *>(Header) + Header->seqindex) +
      State.sequence;
  if (Config.Animate && Data->LastTime > 0 && Sequence->numframes > 1) {
    const double Delta = std::clamp(Time - Data->LastTime, 0.0, 0.1);
    Data->AnimationFrame += static_cast<float>(Delta) * Config.FrameRate *
                            Sequence->fps * 256.0f / (Sequence->numframes - 1);
    Data->AnimationFrame = std::fmod(Data->AnimationFrame, 256.0f);
    if (Data->AnimationFrame < 0)
      Data->AnimationFrame += 256;
  }
  Data->LastTime = Time;
  State.frame = Data->AnimationFrame;
  State.animtime = static_cast<float>(gEngfuncs.GetClientTime());
  Entity.latched.prevanimtime = State.animtime;
  Entity.latched.prevframe = State.frame;
  Entity.latched.prevsequence = State.sequence;
  // Auto framing: place the camera so the CURRENT sequence's bounding box
  // fills the target box (times the margin). Player models differ wildly in
  // size (a humanoid is ~72 units, a creature may be half that, a monster
  // three times), so a fixed camera cannot frame them all.
  entity_view_t View{};
  View.target = &Data->Fbo;
  std::copy(Config.View.Origin.begin(), Config.View.Origin.end(), View.origin);
  std::copy(Config.View.Angles.begin(), Config.View.Angles.end(), View.angles);
  View.verticalFov = Config.View.VerticalFov;
  View.nearPlane = Config.View.NearPlane;
  View.farPlane = Config.View.FarPlane;
  std::copy(Config.View.Background.begin(), Config.View.Background.end(),
            View.background);
  View.light = buildLight(Config.View);
  if (Config.AutoFrame) {
    float top = Sequence->bbmax[2];
    float bottom = Sequence->bbmin[2];
    if (!(top > bottom)) {
      top = Header->bbmax[2];
      bottom = Header->bbmin[2];
    }
    if (top > bottom) {
      const float ModelHeight = (top - bottom) * Config.FrameMargin;
      const float Center = (top + bottom) * 0.5f;
      const float HalfFov =
          Config.View.VerticalFov * 0.5f * 3.14159265358979f / 180.0f;
      const float Distance =
          std::max(ModelHeight * 0.5f / std::tan(HalfFov), 8.0f);
      View.origin[0] = Distance;
      View.origin[1] = 0.0f;
      View.origin[2] = Center;
      View.angles[0] = 0.0f;
      View.angles[1] = 180.0f;
      View.angles[2] = 0.0f;
    }
  }
  // The Renderer owns the scope (saved state, camera, light); the entity and
  // the draw itself stay here.
  if (!Views->BeginView(View))
    return false;
  Renderer->SetCurrentEntity(&Entity);
  Renderer->DrawCurrentEntity(Entity.curstate.rendermode != kRenderNormal);
  Views->EndView();
  ++Data->Frames;
  if (auto *Interface = rmlui::renderInterface())
    Interface->registerExternalTexture(
        Data->Uri, Data->Fbo.s_hBackBufferTex, Width, Height, nullptr,
        rmlui::TextureBlend::PremultipliedAlpha);
  return true;
}

const std::string &Target::uri() const {
  return Data->Uri;
}
unsigned int Target::texture() const {
  return Data->Fbo.s_hBackBufferTex;
}
unsigned int Target::frames() const {
  return Data->Frames;
}
bool hasWorld() {
  auto *Views = entityViewRenderer();
  return Views && Views->HasWorld();
}
void invalidateAll() {
  for (auto *Target : targets())
    Target->invalidate();
}
} // namespace modelpreview
