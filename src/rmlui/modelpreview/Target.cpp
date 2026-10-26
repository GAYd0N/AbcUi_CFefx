// modelpreview::Target - see Target.h.
//
// Since issue #24 the preview renders through the in-house engine
// V_RenderView route (framework/standaloneview, copied from
// MetaHookSv/StandaloneView) instead of Renderer's IEntityViewRenderer: the
// pass draws the model in the view's triangle callback, the engine owns the
// 3D matrices, and the result is captured from the viewport region into a
// backend offscreen target (clear + copy-out). The draw bridge supplies the
// preview's directional light at the engine's StudioSetupLighting seam,
// scoped to the private entity draw rather than the game's world lighting.
//
// The main page preview is a three-entity chain (aiment normalization): the
// action group entity owns sequence/frame/controllers and is the animation
// source, while the displayed player model and the optional weapon model are
// MOVETYPE_FOLLOW entities. The draw bridge explicitly evaluates the action
// group's bones before each follower; private entities have no engine-array
// slot for an aiment lookup. The action group's mesh is never drawn.
#include <metahook.h>
#include "Target.h"
#include "Framing.h"

#include <com_model.h>
#include <const.h>
#include <r_studioint.h>
#include <studio.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Plugin.h>

#include "../../render/BackendHost.h"
#include "../../render/IRenderBackend.h"
#include "../../hooks.h"
#include "../../render/StandaloneView.h"
#include "../AbcRenderInterface.h"
#include "../RmlUiModule.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

#undef min
#undef max

namespace modelpreview {
namespace {
std::vector<Target *> &targets() {
  static std::vector<Target *> Targets;
  return Targets;
}

bool RouteWarned = false;

abc::render::IOffscreenTargets *captureTargets() {
  return abc::render::targets();
}

// The engine's R_SetupGL places ref_params.viewport at
// vid.height - (y + height) in GL's bottom-left coordinates (Quake's viewport
// formula). The preview asks for (0, 0, w, h), so the pass lands at the TOP
// of the screen: the scissor clear and the copy-out must use that same row,
// otherwise they hit the empty bottom-left corner and capture a black box.
int captureRowY(int height) {
  abc::render::IRenderBackend *backend = abc::render::backend();
  const int surface = backend ? backend->surfaceHeight() : 0;
  const int row = surface - height;
  return row > 0 ? row : 0;
}

bool routeAvailable() {
  if (framework::standaloneview::available() && captureTargets())
    return true;
  if (!RouteWarned) {
    RouteWarned = true;
    abc::ConsoleWarn("[AbcUi model] preview disabled (no engine view route "
                     "on this build, or the backend lacks offscreen "
                     "targets)\n");
  }
  return false;
}

// One loaded Studio model. The sequence table is asset data: every read is
// bounds-checked against the header length (seqindex comes from the .mdl).
struct ModelRef {
  model_s *Model = nullptr;
  studiohdr_t *Header = nullptr;

  bool usable() const {
    return Model != nullptr && Header != nullptr;
  }

  const mstudioseqdesc_t *sequence(int Index) const {
    if (!Header || Header->numseq <= 0 || Header->length <= 0)
      return nullptr;
    const auto Size = static_cast<std::size_t>(Header->length);
    const auto Offset = static_cast<std::size_t>(Header->seqindex);
    const auto Bytes =
        static_cast<std::size_t>(Header->numseq) * sizeof(mstudioseqdesc_t);
    if (Offset > Size || Bytes > Size - Offset)
      return nullptr;
    if (Index < 0 || Index >= Header->numseq)
      return nullptr;
    return reinterpret_cast<const mstudioseqdesc_t *>(
               reinterpret_cast<const byte *>(Header) + Header->seqindex) +
           Index;
  }

  int clampSequence(int Wanted) const {
    if (!Header || Header->numseq <= 0 || Header->length <= 0)
      return 0;
    return std::clamp(Wanted, 0, Header->numseq - 1);
  }

  int clampSkin(int Wanted) const {
    if (!Header || Header->numskinfamilies <= 1)
      return 0;
    return std::clamp(Wanted, 0, Header->numskinfamilies - 1);
  }
};

// Sequence boxes are asset data too: a placeholder (some menu action groups
// ship +/-9999 when their pose has no real box) or a malformed table must not
// blow up the auto-framing distance.
bool usableBox(std::span<const float> Min, std::span<const float> Max) {
  if (Min.size() != 3 || Max.size() != 3 || !(Max[2] > Min[2]))
    return false;
  for (std::size_t i = 0; i < Min.size(); ++i) {
    if (!std::isfinite(Min[i]) || !std::isfinite(Max[i]) || Max[i] < Min[i] ||
        std::abs(Min[i]) > 2048.0f || std::abs(Max[i]) > 2048.0f)
      return false;
  }
  return true;
}

// Axis-aligned union of the boxes that must fit into the card.
struct Bounds {
  std::array<float, 3> Min{};
  std::array<float, 3> Max{};
  bool Valid = false;

  void add(const ModelRef &Ref, int Sequence, float Scale) {
    if (!Ref.Header)
      return;
    const auto *Desc = Ref.sequence(Sequence);
    std::span<const float> SourceMin;
    std::span<const float> SourceMax;
    if (Desc && usableBox(Desc->bbmin, Desc->bbmax)) {
      SourceMin = std::span<const float>(Desc->bbmin);
      SourceMax = std::span<const float>(Desc->bbmax);
    } else if (usableBox(Ref.Header->bbmin, Ref.Header->bbmax)) {
      SourceMin = std::span<const float>(Ref.Header->bbmin);
      SourceMax = std::span<const float>(Ref.Header->bbmax);
    } else {
      return;
    }
    for (std::size_t i = 0; i < Min.size(); ++i) {
      const float Low = SourceMin[i] * Scale;
      const float High = SourceMax[i] * Scale;
      if (!Valid) {
        Min[i] = Low;
        Max[i] = High;
      } else {
        Min[i] = std::min(Min[i], Low);
        Max[i] = std::max(Max[i], High);
      }
    }
    Valid = true;
  }
};

ModelRef loadModel(const std::string &Path) {
  ModelRef Ref;
  if (Path.empty() || !hooks::EngineStudioApi ||
      !hooks::EngineStudioApi->Mod_ForName ||
      !hooks::EngineStudioApi->Mod_Extradata)
    return Ref;
  // Some clients substitute their ERROR model for missing content. A non-null
  // model pointer is not proof that the requested native asset was loaded.
  if (!abc::g_pFileSystem || !abc::g_pFileSystem->FileExists(Path.c_str()))
    return Ref;
  Ref.Model = hooks::EngineStudioApi->Mod_ForName(Path.c_str(), 0);
  if (!Ref.Model || Ref.Model->type != mod_studio)
    return {};
  Ref.Header = static_cast<studiohdr_t *>(
      hooks::EngineStudioApi->Mod_Extradata(Ref.Model));
  if (!Ref.Header || Ref.Header->length < sizeof(studiohdr_t) ||
      Ref.Header->id != 0x54534449 || Ref.Header->version != 10)
    return {};
  return Ref;
}

template <class T>
std::span<const T> studioTable(const studiohdr_t *Header, int Offset, int Count,
                               int Limit) {
  if (!Header || Header->length < sizeof(studiohdr_t) ||
      Offset < static_cast<int>(sizeof(studiohdr_t)) || Count < 0 ||
      Count > Limit || Offset % alignof(T) != 0)
    return {};
  const auto Start = static_cast<size_t>(Offset);
  const auto Size = static_cast<size_t>(Header->length);
  if (Start > Size || static_cast<size_t>(Count) > (Size - Start) / sizeof(T))
    return {};
  return {reinterpret_cast<const T *>(reinterpret_cast<const byte *>(Header) +
                                      Start),
          static_cast<size_t>(Count)};
}

// Read the selected bodygroups through the engine's freshly evaluated bone
// matrices. Sequence boxes and a p-model's unmerged coordinates do not describe
// the visible character/held weapon; only these transformed vertices do.
bool appendStudioVertices(const cl_entity_t &Entity,
                          std::vector<FramePoint> &Points) {
  auto *Api = hooks::EngineStudioApi;
  if (!Api || !Api->Mod_Extradata || !Api->StudioGetBoneTransform ||
      !Entity.model)
    return false;
  const auto *Header =
      static_cast<const studiohdr_t *>(Api->Mod_Extradata(Entity.model));
  if (!Header || Header->length < sizeof(studiohdr_t) ||
      Header->numbones <= 0 || Header->numbones > MAXSTUDIOBONES)
    return false;
  using BoneMatrix = float[3][4];
  const auto *Bones =
      reinterpret_cast<const BoneMatrix *>(Api->StudioGetBoneTransform());
  if (!Bones)
    return false;
  const auto Parts = studioTable<mstudiobodyparts_t>(
      Header, Header->bodypartindex, Header->numbodyparts, MAXSTUDIOBODYPARTS);
  if (Parts.empty())
    return false;
  constexpr int MaxVertices = 131072;
  for (const auto &Part : Parts) {
    const auto Models = studioTable<mstudiomodel_t>(
        Header, Part.modelindex, Part.nummodels, MAXSTUDIOMODELS_SVENGINE);
    if (Models.empty() || Part.base <= 0)
      return false;
    const auto Index =
        (std::max(Entity.curstate.body, 0) / Part.base) % Part.nummodels;
    const auto &Model = Models[static_cast<size_t>(Index)];
    if (Model.numverts == 0 || Model.nummesh == 0)
      continue;
    using Vertex = float[3];
    const auto Vertices = studioTable<Vertex>(Header, Model.vertindex,
                                              Model.numverts, MaxVertices);
    const auto BoneIndices = studioTable<byte>(Header, Model.vertinfoindex,
                                               Model.numverts, MaxVertices);
    if (Vertices.empty() || Vertices.size() != BoneIndices.size() ||
        Points.size() + Vertices.size() > MaxVertices)
      return false;
    for (size_t v = 0; v < Vertices.size(); ++v) {
      const int Bone = BoneIndices[v];
      if (Bone >= Header->numbones)
        return false;
      FramePoint Point{};
      for (int i = 0; i < 3; ++i) {
        Point[i] = Bones[Bone][i][3];
        for (int j = 0; j < 3; ++j)
          Point[i] += Bones[Bone][i][j] * Vertices[v][j];
        if (!std::isfinite(Point[i]) || std::abs(Point[i]) > 1000000.0f)
          return false;
      }
      Points.push_back(Point);
    }
  }
  return true;
}

// Private follow entities share the source's world transform explicitly.
void placeEntity(cl_entity_t &Entity, const Options &Config) {
  std::ranges::copy(Config.Origin, Entity.origin);
  std::ranges::copy(Config.Angles, Entity.angles);
  std::ranges::copy(Config.Origin, Entity.curstate.origin);
  std::ranges::copy(Config.Angles, Entity.curstate.angles);
}
} // namespace

// The view participant: options are synced once per framework frame by
// render() (the beat AFTER the engine's view pass), the pass itself runs
// inside V_RenderView before the main view.
struct Target::State : framework::standaloneview::View {
  cl_entity_t Entity{}; // displayed player model
  cl_entity_t Action{}; // action group: animation source, never drawn
  cl_entity_t Weapon{}; // weapon model merged onto the action group
  ModelRef EntityModel;
  ModelRef ActionModel;
  ModelRef WeaponModel;
  std::string Source;
  std::string ActionSource;
  std::string WeaponSource;
  std::string Uri;
  unsigned int CaptureHandle = 0;
  unsigned int CaptureTexture = 0;
  int CaptureWidth = 0;
  int CaptureHeight = 0;
  bool TargetRegistered = false;
  bool Attempted = false;
  bool ModelMissing = false;
  bool HasAction = false;
  bool HasWeapon = false;
  bool ActionWarned = false;
  bool WeaponWarned = false;
  bool LastPassOk = false;
  unsigned int Frames = 0;
  bool Active = false;
  double LastRenderCall = 0;
  double LastTime = 0;
  float AnimationFrame = 0;
  int LastSequence = -1;
  float LastFrame = -1;
  // Camera resolved at sync time (auto-framing needs the model headers).
  float ViewOrigin[3] = {0, 0, 0};
  float ViewAngles[3] = {0, 0, 0};
  float Background[4] = {0, 0, 0, 0};
  framework::standaloneview::StudioLighting Lighting;
  Options FramingOptions;
  std::vector<FramePoint> PoseVertices;
  std::vector<FramePoint> PoseScratch;
  bool UsePoseFraming = false;
  bool TransparentPass = false;

  bool isActive() const override {
    return Active && !ModelMissing && CaptureHandle != 0 &&
           gEngfuncs.GetAbsoluteTime() - LastRenderCall < 1.0;
  }

  void calcRefdef(struct ref_params_s *pparams) override {
    // Only vieworg / viewangles / viewport: the cl_* fields belong to the
    // main view (the engine writes them back). The engine keeps its own
    // projection fov (ref_params has no fov field on this engine
    // generation); the auto-framing math reads the engine's scr_fov_value
    // instead of the card's fov attribute.
    std::ranges::copy(ViewOrigin, pparams->vieworg);
    std::ranges::copy(ViewAngles, pparams->viewangles);
    pparams->viewport[0] = 0;
    pparams->viewport[1] = 0;
    pparams->viewport[2] = CaptureWidth;
    pparams->viewport[3] = CaptureHeight;
  }

  void onBeforeRenderView() override {}

  void drawPass(framework::standaloneview::Drawfuncs *drawfuncs) {
    // The background clear must land inside the pass, on the framebuffer the
    // engine bound for this view - R_RenderView clears only depth (and color
    // just with gl_clear), so the region is cleared here, exactly over this
    // view's viewport.
    const auto clearViewport = [this]() {
      if (abc::render::IOffscreenTargets *targets = captureTargets())
        targets->clearViewportRegion(0, captureRowY(CaptureHeight),
                                     CaptureWidth, CaptureHeight, Background);
    };
    clearViewport();
    if (UsePoseFraming) {
      PoseScratch.clear();
      bool Complete =
          drawfuncs->setupStudioPose(&Entity, HasAction ? &Action : nullptr) &&
          appendStudioVertices(Entity, PoseScratch) && !PoseScratch.empty();
      if (Complete && HasWeapon)
        Complete = drawfuncs->setupStudioPose(&Weapon, &Action) &&
                   appendStudioVertices(Weapon, PoseScratch);
      if (Complete)
        PoseVertices.swap(PoseScratch);
      else
        PoseVertices.clear();
    }
    // Each follower consumes a fresh source pose, never a previous preview's
    // saved bones (or the saved bones of the displayed player's extra bones).
    drawfuncs->drawStudioModel(&Entity, HasAction ? &Action : nullptr,
                               &Lighting);
    if (HasWeapon)
      drawfuncs->drawStudioModel(&Weapon, &Action, &Lighting);
  }

  void drawNormalTriangles(
      framework::standaloneview::Drawfuncs *drawfuncs) override {
    if (!TransparentPass)
      drawPass(drawfuncs);
  }
  void drawTransparentTriangles(
      framework::standaloneview::Drawfuncs *drawfuncs) override {
    if (TransparentPass)
      drawPass(drawfuncs);
  }

  void onAfterRenderView() override {
    // Copy the viewport region out of the currently bound framebuffer into
    // the capture texture (StandaloneView's documented capture contract).
    abc::render::IOffscreenTargets *targets = captureTargets();
    if (!targets || !CaptureHandle)
      return;
    targets->copyViewportToTarget(CaptureHandle, 0, captureRowY(CaptureHeight),
                                  CaptureWidth, CaptureHeight);
    const unsigned int texture = targets->texture(CaptureHandle);
    if (!texture)
      return;
    CaptureTexture = texture;
    LastPassOk = true;
    ++Frames;
    if (auto *renderInterface = rmlui::renderInterface();
        renderInterface && !TargetRegistered) {
      // The backend normalizes the captured scene to straight alpha; the
      // Renderer program-state table has no premultiplied 2D blend.
      renderInterface->registerExternalTexture(
          Uri, CaptureTexture, CaptureWidth, CaptureHeight, nullptr,
          rmlui::TextureBlend::StraightAlpha);
      TargetRegistered = true;
    }
  }

  void releaseCapture() {
    if (auto *renderInterface = rmlui::renderInterface();
        renderInterface && TargetRegistered)
      renderInterface->unregisterExternalTexture(Uri);
    TargetRegistered = false;
    CaptureTexture = 0;
    if (CaptureHandle && captureTargets())
      captureTargets()->free(CaptureHandle);
    CaptureHandle = 0;
    LastPassOk = false;
  }
};

Target::Target() : Data(std::make_unique<State>()) {
  static unsigned long long NextId = 0;
  Data->Uri = "modelfbo://" + std::to_string(++NextId) + "/";
  targets().push_back(this);
  framework::standaloneview::registerView(Data.get());
}

Target::~Target() {
  framework::standaloneview::unregisterView(Data.get());
  invalidate();
  std::erase(targets(), this);
}

void Target::invalidate() {
  Data->releaseCapture();
  Data->Entity = {};
  Data->Action = {};
  Data->Weapon = {};
  Data->EntityModel = {};
  Data->ActionModel = {};
  Data->WeaponModel = {};
  Data->HasAction = false;
  Data->HasWeapon = false;
  Data->PoseVertices.clear();
  Data->PoseScratch.clear();
  Data->UsePoseFraming = false;
  Data->ActionWarned = false;
  Data->WeaponWarned = false;
  Data->Attempted = false;
  Data->ModelMissing = false;
  Data->LastSequence = -1;
  Data->LastTime = 0;
}

bool Target::render(const Options &Config, int Width, int Height) {
  State &state = *Data;
  if (!routeAvailable()) {
    state.Active = false;
    return false;
  }
  if (state.Source != Config.Source ||
      state.ActionSource != Config.ActionSource ||
      state.WeaponSource != Config.WeaponSource) {
    invalidate();
    state.Source = Config.Source;
    state.ActionSource = Config.ActionSource;
    state.WeaponSource = Config.WeaponSource;
  }
  if (Config.Source.empty() || Width <= 0 || Height <= 0) {
    state.Active = false;
    return false;
  }
  state.Active = true;
  state.LastRenderCall = gEngfuncs.GetAbsoluteTime();
  Width = std::clamp(Width, 1, 2048);
  Height = std::clamp(Height, 1, 2048);

  // Model loading through the engine studio API (mounted search paths
  // included); the action group and the weapon are loaded next to the
  // displayed model.
  if (!state.Attempted) {
    // A delayed studio export is not a missing model. Retry next frame
    // instead of latching ModelMissing for this source until a context reset.
    if (!hooks::EngineStudioApi || !hooks::EngineStudioApi->Mod_ForName ||
        !hooks::EngineStudioApi->Mod_Extradata || !abc::g_pFileSystem) {
      state.Active = false;
      return false;
    }
    state.Attempted = true;
    state.EntityModel = loadModel(Config.Source);
    state.ModelMissing = !state.EntityModel.usable();
    if (state.ModelMissing) {
      abc::ConsoleWarn("[AbcUi model] cannot load Studio model: %s\n",
                       Config.Source.c_str());
      return false;
    }
    if (!Config.ActionSource.empty()) {
      state.ActionModel = loadModel(Config.ActionSource);
      state.HasAction = state.ActionModel.usable();
      if (!state.HasAction && !state.ActionWarned) {
        state.ActionWarned = true;
        abc::ConsoleWarn("[AbcUi model] action group unavailable: %s\n",
                         Config.ActionSource.c_str());
      }
    }
    if (state.HasAction && !Config.WeaponSource.empty()) {
      state.WeaponModel = loadModel(Config.WeaponSource);
      state.HasWeapon = state.WeaponModel.usable();
      if (!state.HasWeapon && !state.WeaponWarned) {
        state.WeaponWarned = true;
        abc::ConsoleWarn("[AbcUi model] weapon model unavailable: %s\n",
                         Config.WeaponSource.c_str());
      }
    }
    state.Entity.model = state.EntityModel.Model;
    state.Action.model = state.ActionModel.Model;
    state.Weapon.model = state.WeaponModel.Model;
  }
  if (state.ModelMissing || !state.EntityModel.usable())
    return false;

  // The capture texture rides the backend's offscreen targets; a size
  // change re-creates it (the handle changes, the URI registration rides
  // onAfterRenderView).
  if (abc::render::IOffscreenTargets *targets = captureTargets()) {
    if (state.CaptureHandle &&
        (state.CaptureWidth != Width || state.CaptureHeight != Height))
      state.releaseCapture();
    if (!state.CaptureHandle) {
      state.CaptureHandle = targets->create(Width, Height);
      state.CaptureWidth = Width;
      state.CaptureHeight = Height;
      if (!state.CaptureHandle)
        return false;
    }
  } else {
    return false;
  }

  // The action group's sequence table owns the animation; without a group the
  // displayed model animates itself exactly as before.
  const ModelRef &Animation =
      state.HasAction ? state.ActionModel : state.EntityModel;
  const int SequenceIndex = Animation.clampSequence(Config.Sequence);
  const auto *Sequence = Animation.sequence(SequenceIndex);

  const double Time = gEngfuncs.GetAbsoluteTime();
  if (state.LastSequence != Config.Sequence ||
      state.LastFrame != Config.Frame) {
    state.AnimationFrame = Config.Frame;
    state.LastSequence = Config.Sequence;
    state.LastFrame = Config.Frame;
  }
  // The .mdl sequence descriptor is asset data: a garbage fps (0, 1e30,
  // NaN) must not stop the animation or push inf/NaN into the render state.
  const float SequenceFps = Sequence && std::isfinite(Sequence->fps)
                                ? std::clamp(Sequence->fps, 0.0f, 240.0f)
                                : 0.0f;
  if (Config.Animate && state.LastTime > 0 && Sequence &&
      Sequence->numframes > 1 && SequenceFps > 0.0f) {
    const double Delta = std::clamp(Time - state.LastTime, 0.0, 0.1);
    state.AnimationFrame += static_cast<float>(Delta) * Config.FrameRate *
                            SequenceFps * 256.0f / (Sequence->numframes - 1);
    state.AnimationFrame = std::fmod(state.AnimationFrame, 256.0f);
    if (state.AnimationFrame < 0)
      state.AnimationFrame += 256;
  }
  state.LastTime = Time;
  const float Frame = state.AnimationFrame;
  const float AnimTime = static_cast<float>(gEngfuncs.GetClientTime());

  auto setControllers = [&Config](cl_entity_t &Ent) {
    for (int i = 0; i < 4; i++) {
      Ent.curstate.controller[i] = static_cast<byte>(Config.Controllers[i]);
      Ent.latched.prevcontroller[i] = Ent.curstate.controller[i];
    }
    for (int i = 0; i < 2; i++) {
      Ent.curstate.blending[i] = static_cast<byte>(Config.Blending[i]);
      Ent.latched.prevblending[i] = Ent.curstate.blending[i];
    }
  };

  // The action group: model, pose and transform of the whole chain.
  if (state.HasAction) {
    auto &Action = state.Action;
    auto &ActionState = Action.curstate;
    ActionState.rendermode = kRenderNormal;
    ActionState.renderfx = 0;
    ActionState.renderamt = 255;
    ActionState.sequence = static_cast<short>(SequenceIndex);
    ActionState.skin = static_cast<short>(state.ActionModel.clampSkin(0));
    ActionState.body = 0;
    ActionState.scale = Config.Scale;
    ActionState.colormap = 0;
    ActionState.framerate = 0;
    ActionState.frame = Frame;
    ActionState.animtime = AnimTime;
    ActionState.movetype = MOVETYPE_NONE;
    ActionState.aiment = 0;
    setControllers(Action);
    Action.mouth.mouthopen = static_cast<byte>(Config.Mouth);
    std::ranges::copy(Config.Origin, Action.origin);
    std::ranges::copy(Config.Angles, Action.angles);
    std::ranges::copy(Config.Origin, ActionState.origin);
    std::ranges::copy(Config.Angles, ActionState.angles);
    Action.latched.prevframe = Frame;
    Action.latched.prevsequence = ActionState.sequence;
    Action.latched.prevanimtime = AnimTime;
  }

  // The displayed player model: own render mode/colour, but its bones come
  // from the action group while the follow chain is active.
  auto &Entity = state.Entity;
  auto &EntityState = Entity.curstate;
  EntityState.rendermode = Config.RenderMode;
  EntityState.renderfx = Config.RenderFx;
  EntityState.renderamt = Config.RenderAmount;
  EntityState.rendercolor = {static_cast<byte>(Config.RenderColor[0]),
                             static_cast<byte>(Config.RenderColor[1]),
                             static_cast<byte>(Config.RenderColor[2])};
  EntityState.sequence =
      static_cast<short>(state.EntityModel.clampSequence(SequenceIndex));
  EntityState.skin =
      static_cast<short>(state.EntityModel.clampSkin(Config.Skin));
  EntityState.body = Config.Body;
  EntityState.scale = Config.Scale;
  EntityState.colormap = Config.ColorMap;
  EntityState.framerate = 0;
  EntityState.frame = Frame;
  EntityState.animtime = AnimTime;
  EntityState.movetype = state.HasAction ? MOVETYPE_FOLLOW : MOVETYPE_NONE;
  EntityState.aiment = 0; // private source passed directly to the draw bridge
  setControllers(Entity);
  Entity.mouth.mouthopen = static_cast<byte>(Config.Mouth);
  placeEntity(Entity, Config);
  Entity.latched.prevanimtime = AnimTime;
  Entity.latched.prevframe = Frame;
  Entity.latched.prevsequence = EntityState.sequence;

  // The weapon model: same follow target, so its bones are merged with the
  // action group's pose as well.
  if (state.HasWeapon) {
    auto &Weapon = state.Weapon;
    auto &WeaponState = Weapon.curstate;
    WeaponState.rendermode = kRenderNormal;
    WeaponState.renderfx = 0;
    WeaponState.renderamt = 255;
    WeaponState.sequence =
        static_cast<short>(state.WeaponModel.clampSequence(0));
    WeaponState.skin = static_cast<short>(state.WeaponModel.clampSkin(0));
    WeaponState.body = 0;
    WeaponState.scale = Config.Scale;
    WeaponState.colormap = 0;
    WeaponState.framerate = 0;
    WeaponState.frame = 0;
    WeaponState.animtime = AnimTime;
    WeaponState.movetype = MOVETYPE_FOLLOW;
    WeaponState.aiment = 0;
    placeEntity(Weapon, Config);
  }

  state.TransparentPass = Config.RenderMode != kRenderNormal;
  state.Lighting.Ambient = Config.View.Ambient;
  state.Lighting.Shade = Config.View.Shade;
  state.Lighting.Color = Config.View.LightColor;
  state.Lighting.Direction = Config.View.LightDirection;
  state.Lighting.Enabled = Config.View.Lighting;

  // Pose sampling happens in the engine pass; use the last completed pose on
  // this frame's sync. Discard it when the caller changes the pose/placement.
  const auto &Previous = state.FramingOptions;
  if (Previous.Source != Config.Source ||
      Previous.Sequence != Config.Sequence || Previous.Frame != Config.Frame ||
      Previous.Body != Config.Body || Previous.Origin != Config.Origin ||
      Previous.Angles != Config.Angles || Previous.Scale != Config.Scale ||
      Previous.Mouth != Config.Mouth ||
      Previous.Controllers != Config.Controllers ||
      Previous.Blending != Config.Blending ||
      Previous.AutoFrame != Config.AutoFrame)
    state.PoseVertices.clear();
  state.FramingOptions = Config;
  state.UsePoseFraming = Config.AutoFrame;

  // Fit only visible posed geometry. Until the first pose (or on an
  // unavailable Studio bone API), bootstrap from the displayed player's box,
  // never the invisible action group or an unmerged weapon's metadata.
  std::ranges::copy(Config.View.Origin, state.ViewOrigin);
  std::ranges::copy(Config.View.Angles, state.ViewAngles);
  std::ranges::copy(Config.View.Background, state.Background);
  if (Config.AutoFrame) {
    float EngineFov = 90.0f;
    if (cvar_t *fov = abc::CVAR_GET_POINTER("scr_fov_value"))
      EngineFov = std::clamp(fov->value, 30.0f, 170.0f);
    const float Aspect = static_cast<float>(Width) / Height;
    auto Camera =
        frameCameraPoints(Config, state.PoseVertices, EngineFov, Aspect);
    if (!Camera) {
      Bounds Box;
      Box.add(state.EntityModel, state.EntityModel.clampSequence(SequenceIndex),
              Config.Scale);
      if (Box.Valid)
        Camera = frameCamera(Config, Box.Min, Box.Max, EngineFov, Aspect);
    }
    if (Camera) {
      std::ranges::copy(Camera->Origin, state.ViewOrigin);
      std::ranges::copy(Camera->Angles, state.ViewAngles);
    }
  }
  // Readiness follows the last pass; the pass runs one engine frame later
  // (V_RenderView precedes the end-frame beat that syncs options).
  return state.LastPassOk && state.CaptureTexture != 0;
}

const std::string &Target::uri() const {
  return Data->Uri;
}
unsigned int Target::texture() const {
  return Data->CaptureTexture;
}
unsigned int Target::frames() const {
  return Data->Frames;
}
bool hasWorld() {
  return framework::standaloneview::available() && gEngfuncs.pfnGetLevelName &&
         gEngfuncs.pfnGetLevelName()[0] != '\0';
}
void invalidateAll() {
  for (auto *Target : targets())
    Target->invalidate();
}
} // namespace modelpreview
