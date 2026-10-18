// rmlui::RmlUiModule implementation - see RmlUiModule.h.
#include <metahook.h>

#include "RmlUiModule.h"
#include "../services/steam/SteamAPI.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <abc/CvarCmd.h>
#include <abc/Console.h>
#include <abc/Plugin.h>

#include <keydefs.h>

#include "../framework/Assets.h"
#include "../framework/FatalExit.h"
#include "../framework/Localization.h"
#include "../framework/PanelManager.h"
#include "../framework/Theme.h"
#include "../render/Render.h"
#include "texture/Avatars.h"
#include "../data/HudState.h"
#include "../data/WeaponResource.h"
#include "../data/PlayerResource.h"
#include "texture/SpriteResource.h"

#include "AbcRenderInterface.h"
#include "backdrop/ElementBackdrop.h"
#include "backdrop/Backdrop.h"
#include "modelpreview/Target.h"
#include "model/ElementModel.h"
#include "video/ElementVideo.h"
#include "AbcSystemInterface.h"
#include "InputBridge.h"
#include "PaintGate.h"
#include "TextInput.h"
#include "VGUI2ExtensionImport.h"
#include "../services/GameUiBridge.h"

namespace rmlui {

namespace {
LifecycleSink Sink;
}

void SetLifecycleSink(LifecycleSink sink) {
  Sink = std::move(sink);
}

namespace {

// System fonts, loaded straight from the Windows font dir (plan 05 §3.4 -
// no redistribution). TTC multi-face support is a Phase 3 verification item
// (R-10): failures warn per-face. T21 strict policy: if NO face loads, text
// cannot render anywhere (menu AND emergency page are unreadable - the black
// locked screen the fault policy forbids), so init() stops the process
// through framework::fatalExit. The Debug-only "-abcui_fonts <dir>" override
// exists so the fault matrix can exercise that path against an empty dir.
constexpr const char *kFontFiles[] = {
    "verdana.ttf",  // old scheme body/number face
    "verdanab.ttf", // Verdana bold (hud numbers)
    "tahoma.ttf",   // old "UiBold" face
    "msyh.ttc",     // Microsoft YaHei regular (fallback face)
    "msyhbd.ttc",   // Microsoft YaHei bold
    "consola.ttf",  // Consolas (fallback face)
};
constexpr const char *kFontDirectory = "C:\\Windows\\Fonts\\";

// Context/renderer singletons (declared before RendererCallbacks: its
// renderFrame reads them).
Rml::Context *Context = nullptr;

#ifndef NDEBUG
// abc_ui_perf instrument (Phase 8): QPC brackets around the Update passes
// (onFrame/onRedraw) and the Render pass (renderFrame). The command prints
// avg/max over the accumulated window and restarts it.
struct PerfWindow {
  int Frames = 0;
  double UpdateTotalMs = 0.0, UpdateMaxMs = 0.0;
  double RenderTotalMs = 0.0, RenderMaxMs = 0.0;
};

PerfWindow Perf;
double PerfInvFrequencyMs = 0.0;

class PerfTimer {
public:
  PerfTimer() {
    QueryPerformanceCounter(&Start);
  }
  double milliseconds() const {
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    return static_cast<double>(end.QuadPart - Start.QuadPart) *
           PerfInvFrequencyMs;
  }

private:
  LARGE_INTEGER Start;
};

void PerfAccumulateUpdate(double ms) {
  Perf.UpdateTotalMs += ms;
  Perf.UpdateMaxMs = Perf.UpdateMaxMs > ms ? Perf.UpdateMaxMs : ms;
}

void PerfAccumulateRender(double ms) {
  Perf.RenderTotalMs += ms;
  Perf.RenderMaxMs = Perf.RenderMaxMs > ms ? Perf.RenderMaxMs : ms;
  Perf.Frames++;
}

#endif

#ifndef NDEBUG
// Defined with the fault instrumentation below; used by renderFrame's tail.
void captureFirstFrameEvidence();
#endif

class RendererCallbacks : public IMetaRendererCallbacks {
public:
  void OnBeginRendering(int *, int *, int *, int *) override {}
  void OnRenderFrameStart() override {}
  void OnBeginRenderingPost(int *, int *, int *, int *) override {}
  void OnFreeFrameBuffers() override {
    modelpreview::invalidateAll();
  }
  void OnGenerateFrameBuffers() override {}
  void OnPreRenderView() override {}
  void OnPreRenderViewPost() override {}
  void OnPostRenderView() override {}
  void OnPostRenderViewPost() override {}
  void OnRenderEndFrame() override {
    renderFrame();
  }

  // Render the Rml scene onto the final buffer, once per engine frame,
  // right before it is blitted to screen (see module header note).
  void renderFrame() {
    auto *renderer = abc::MetaRenderer();
    if (!renderer || !RenderInterface || !Context)
      return;
    // Menu backdrop first: it captures the frame that was just presented
    // and blurs it, so the Rml pass below draws over the finished blur.
    if (backdrop::anyVisible()) {
      // Both steps always run: render() reports only texture changes while
      // the element sync carries layout and the image source.
      const bool refreshed = menubackdrop::menuBackdrop().render();
      const bool mirrored = backdrop::syncAll();
      if (refreshed || mirrored)
        Context->Update();
    }
    model::renderPreviews();
    renderer->PushFrameBuffer();
    renderer->BindFrameBuffer(renderer->GetFinalBufferFBO());
    RenderInterface->beginFrame();
#ifndef NDEBUG
    const PerfTimer renderTimer;
#endif
    // Also gate here: a disconnect may arrive after HUD_Frame, and there
    // need not be another HUD_Redraw before the main menu is rendered.
    framework::PanelManager::setSessionActive(
        services::nativeUiBridge().hasLevel());
    Context->Render();
#ifndef NDEBUG
    PerfAccumulateRender(renderTimer.milliseconds());
    // T21: the first frame whose buffer actually contains the menu is the
    // one that just completed this render pass (see the helper).
    captureFirstFrameEvidence();
#endif
    RenderInterface->endFrame();
    renderer->PopFrameBuffer();
  }

  std::unique_ptr<AbcRenderInterface> RenderInterface;
};

RendererCallbacks Callbacks;
std::unique_ptr<AbcSystemInterface> SystemInterface;
std::unique_ptr<AbcFileInterface> FileInterface;
bool Initialised = false;
// T21 diagnostics: faces loaded by the last loadFonts() pass (0 = fatal).
int LoadedFonts = 0;

void loadFonts(const char *directory, int &loaded) {
  loaded = 0;
  for (const char *file : kFontFiles) {
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s%s", directory, file);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
      continue;
    if (Rml::LoadFontFace(path, true))
      loaded++;
    else
      abc::ConsoleWarn("[AbcUi] font load failed: %s\n", path);
  }
}

// T21 Debug-only fault seam: "-abcui_fonts <dir>" swaps the font root so the
// fault matrix can start against an empty/unreadable directory and observe
// the core-unavailable exit. Release builds always use the system root.
const char *fontDirectoryOverride() {
#ifndef NDEBUG
  const char *next = nullptr;
  if (gEngfuncs.CheckParm && gEngfuncs.CheckParm("-abcui_fonts", &next) &&
      next && *next)
    return next;
#endif
  return kFontDirectory;
}

// T21 first-frame evidence (Debug only). The acceptance forbids "a
// screenshot taken seconds after startup" as first-frame proof (05 doc
// V01/V16/V17): on the FIRST frame the replacement menu has actually been
// RENDERED, print the suppression counters of that exact frame (the console
// log then ties the first committed frame to paint blocked==attempts), and
// arm a delayed screenshot. The engine's `screenshot` command is issued a
// short delay later because a capture during the engine's own startup window
// (the first presented frames) reads a not-yet-stable back buffer and lands
// black - live-caught twice; the delay is honest because the menu is static
// here and the log records both the first-frame counters and the counters
// at capture time (equal unless activity moved them). Re-armed on every
// init() (map change / vid restart keep their own first rendered frame).
// Debug-only: the call sites, the declaration and this body are all inside
// NDEBUG guards; Release carries none of it.
#ifndef NDEBUG
bool FirstFrameEvidenceDone = false;
int FirstFrameScreenshotCountdown = -1;
void captureFirstFrameEvidence() {
  if (FirstFrameScreenshotCountdown > 0) {
    if (--FirstFrameScreenshotCountdown == 0) {
      const auto c = services::nativeUiBridge().counters();
      abc::ConsolePrint(
          "[AbcUi] first-frame: delayed capture counters paint gameui=%u/%u "
          "serverbrowser=%u/%u allowed-other=%u\n",
          c.PaintBlocked[(int)services::PaintBucket::kGameUi],
          c.PaintAttempts[(int)services::PaintBucket::kGameUi],
          c.PaintBlocked[(int)services::PaintBucket::kServerBrowser],
          c.PaintAttempts[(int)services::PaintBucket::kServerBrowser],
          c.PaintAttempts[(int)services::PaintBucket::kOther] -
              c.PaintBlocked[(int)services::PaintBucket::kOther]);
      gEngfuncs.pfnClientCmd("screenshot\n");
      abc::ConsolePrint("[AbcUi] first-frame: screenshot issued (capture frame "
                        "after the first rendered menu frame)\n");
    }
    return;
  }
  if (FirstFrameEvidenceDone || !Context)
    return;
  if (!services::nativeUiBridge().pageShown())
    return; // not presented yet: wait for the actual first rendered frame
  FirstFrameEvidenceDone = true;
  const auto c = services::nativeUiBridge().counters();
  abc::ConsolePrint(
      "[AbcUi] first-frame: gate=%d armed=%d page=1 paint gameui=%u/%u "
      "serverbrowser=%u/%u allowed-other=%u hittest-gameui=%u/%u\n",
      rmlui::paintGateInstalled() ? 1 : 0,
      services::nativeUiBridge().armed() ? 1 : 0,
      c.PaintBlocked[(int)services::PaintBucket::kGameUi],
      c.PaintAttempts[(int)services::PaintBucket::kGameUi],
      c.PaintBlocked[(int)services::PaintBucket::kServerBrowser],
      c.PaintAttempts[(int)services::PaintBucket::kServerBrowser],
      c.PaintAttempts[(int)services::PaintBucket::kOther] -
          c.PaintBlocked[(int)services::PaintBucket::kOther],
      c.HitTestBlocked[(int)services::PaintBucket::kGameUi],
      c.HitTestAttempts[(int)services::PaintBucket::kGameUi]);
  // ~2s at the shipped frame rate: the startup window settles, the UI stays
  // as first presented (no input is injected between the two lines).
  FirstFrameScreenshotCountdown = 120;
}
#endif // NDEBUG

void debuggerCommand() {
  const bool visible = Rml::Debugger::IsVisible();
  Rml::Debugger::SetVisible(!visible);
  abc::ConsolePrint("[AbcUi] debugger %s\n", !visible ? "enabled" : "disabled");
}

// T21 fault-matrix finding: the Rml debugger's log beacon (the small "!"
// alert, document "rmlui-debug-log-beacon") turns visible on ANY warning/
// error-level Rml log message while the debugger is closed - so exactly the
// fault paths this task certifies (missing/broken resources -> Rml load
// errors) painted developer chrome over the strict UI. The beacon is not
// takeover UI; keep it hidden while the debugger is closed (opening the
// debugger shows the log list itself and hides the beacon on its own).
void suppressDebugBeacon() {
  if (!Context || Rml::Debugger::IsVisible())
    return;
  if (Rml::ElementDocument *beacon =
          Context->GetDocument("rmlui-debug-log-beacon")) {
    if (beacon->IsVisible())
      beacon->Hide();
  }
}

} // namespace

// Debug tooling seam (ProbeCommand.cpp): the live context, null before
// init()/after shutdown().
Rml::Context *context() {
  return Context;
}

#ifndef NDEBUG
PerfSnapshot perfSnapshot() {
  PerfSnapshot out;
  out.Frames = Perf.Frames;
  out.UpdateAvgMs = Perf.Frames ? Perf.UpdateTotalMs / Perf.Frames : 0.0;
  out.UpdateMaxMs = Perf.UpdateMaxMs;
  out.RenderAvgMs = Perf.Frames ? Perf.RenderTotalMs / Perf.Frames : 0.0;
  out.RenderMaxMs = Perf.RenderMaxMs;
  Perf = {};
  return out;
}
#endif

AbcRenderInterface *renderInterface() {
  return Callbacks.RenderInterface.get();
}

int loadedFontCount() {
  return LoadedFonts;
}

void init() {
  if (Initialised)
    return;
  InputBridge::resetPointerState();
#ifndef NDEBUG
  // T21: a fresh context cycle presents its own first frame.
  FirstFrameEvidenceDone = false;
  FirstFrameScreenshotCountdown = -1;
#endif

#ifndef NDEBUG
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  PerfInvFrequencyMs = 1000.0 / static_cast<double>(frequency.QuadPart);
#endif

  vgui2ext::init();

  // T21 strict takeover gate: the entry seam (VGUI2Extension) must be
  // present, or the original GameUI can never be suppressed and the
  // replacement has no business presenting anything - stop with the
  // external diagnosis instead of drawing on an unsuppressed frame.
  if (!vgui2ext::nativeCallbacksRegistered())
    framework::fatalExit("VGUI2Extension entry seam unavailable; the original "
                         "GameUI cannot be suppressed (strict takeover)");
  if (!abc::MetaRenderer())
    framework::fatalExit(
        "Renderer interface unavailable; nothing can be drawn");

  SystemInterface = std::make_unique<AbcSystemInterface>();
  FileInterface = std::make_unique<AbcFileInterface>();
  Callbacks.RenderInterface = std::make_unique<AbcRenderInterface>();
  Rml::SetSystemInterface(SystemInterface.get());
  Rml::SetFileInterface(FileInterface.get());
  Rml::SetRenderInterface(Callbacks.RenderInterface.get());
  if (!Rml::Initialise())
    framework::fatalExit("Rml::Initialise failed (Rml core unavailable)");
  model::registerElement();
  video::registerElement();
  backdrop::registerElement();

  const auto width = static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth());
  const auto height =
      static_cast<int>(abc::MetaRenderer()->GetSwapChainHeight());
  // The text/IME handler is context-scoped (T07): WidgetTextInput calls its
  // OnActivate/OnDeactivate/OnDestroy as our editors gain/lose/lose focus.
  Context =
      Rml::CreateContext("abcui", Rml::Vector2i(width, height),
                         Callbacks.RenderInterface.get(), &rmlui::textInput());
  if (!Context)
    framework::fatalExit("Rml::CreateContext failed (Rml context unavailable)");

  // T21: a face-less context can only produce an unreadable (effectively
  // black) UI - menu and emergency page alike - so zero loaded faces is
  // the core-unavailable case, not a degraded-but-usable one.
  loadFonts(fontDirectoryOverride(), LoadedFonts);
  if (LoadedFonts == 0)
    framework::fatalExit("no font face could be loaded; text cannot render");
  framework::Localization::load();
  framework::Theme::applyScale(Context);
  sprites::vidInit();
  avatars::init();
  framework::Assets::verifyInstalled();
  framework::PanelManager::initAll(Context);

  Rml::Debugger::Initialise(Context);
  Rml::Debugger::SetVisible(false);

  abc::MetaRenderer()->RegisterRenderCallbacks(&Callbacks);
  Initialised = true;
  abc::ConsolePrint("[AbcUi] RmlUi %s ready (context %dx%d)\n",
                    Rml::GetVersion().c_str(), width, height);

  // Last assembly step (see modules/Module.h): DOM writes are only legal
  // from here on, and the dispatch re-arms module shutdown for this cycle.
  if (Sink.contextReady)
    Sink.contextReady(Context);
}

void shutdown() {
  // Ownership first (T06): tokens release and the pairing cleanups run
  // while every owner can still clean up and the context (for the synth
  // UI keyups) still exists. Teardown form: HUD_Shutdown runs after the
  // engine unloaded GameUI.dll/vgui2.dll, so the sink's engine-side
  // exclusive-change effects (cursor lock, game-mouse restore) must stay
  // out of this path - they fault the exit (see Router::reset).
  uiRouter().reset();
  // Text/IME state next (T07): the handler's cached TextInputContext
  // pointers die with the DOM below; drop them while it is alive.
  textInputShutdown();
  if (!Initialised) {
    // HUD_Init can arm modules before the first HUD_Frame creates a context.
    if (Sink.shutdown)
      Sink.shutdown();
    return;
  }
  Initialised = false;
  if (auto *renderer = abc::MetaRenderer())
    renderer->UnregisterRenderCallbacks(&Callbacks);
  // Modules release what they own while the context still exists (their
  // documents die with it, never after - see modules/Module.h).
  if (Sink.shutdown)
    Sink.shutdown();
  framework::PanelManager::shutdown();

  // Rml::Shutdown first releases per-load borrowed/owned records; cache IDs
  // are deleted only afterwards, including never-delivered avatar uploads.
  // clearAdditiveTextures() before that keeps recycled ids off the additive
  // program if any handle outside the database ever existed.
  Callbacks.RenderInterface->clearAdditiveTextures();
  Context = nullptr;
  Rml::Shutdown();
  sprites::shutdown();
  avatars::shutdown();
  steamapi::shutdown();
  Callbacks.RenderInterface.reset();
  FileInterface.reset();
  SystemInterface.reset();
  vgui2ext::shutdown();
}

void registerCommands() {
  abc::ADD_COMMAND("abc_ui_debugger", debuggerCommand);
  // Menu backdrop knobs (the element reads them every sync; an explicit
  // attribute on the <backdrop> node still wins).
  abc::CREATE_CVAR("abc_ui_menu_video", menubackdrop::kDefaultVideoSource,
                   abc::FCVAR_VALUE, nullptr);
  abc::CREATE_CVAR("abc_ui_menu_backdrop_blur", "2", abc::FCVAR_VALUE, nullptr);
}

void onFrame(double frameTime) {
  // Deferred assembly (see module header): HUD_VidInit proved too
  // early-bound (-windowed initializes video before the export patch
  // lands), so the first HUD_Frame carries the one-shot init.
  if (!Initialised) {
    init();
    if (!Initialised || !Context)
      return;
    framework::Theme::applyScale(Context);
    Context->SetDimensions(Rml::Vector2i(
        static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth()),
        static_cast<int>(abc::MetaRenderer()->GetSwapChainHeight())));
  }
  framework::PanelManager::setSessionActive(
      services::nativeUiBridge().hasLevel());
  InputBridge::update(Context);
  avatars::onFrame();
  // One scene gate for both video consumers. Even the pause-menu backdrop
  // uses the live world, so it must not keep a hidden movie decoding.
  const bool MenuVideoEnabled =
      backdrop::anyVisible() && !services::nativeUiBridge().hasLevel();
  menubackdrop::menuBackdrop().pumpVideo(frameTime, MenuVideoEnabled);
  // Poll completed CPU frames and upload on the render thread. FFmpeg open,
  // decode and conversion run only on the bounded worker.
  if (video::pumpFrames(frameTime, MenuVideoEnabled))
    Context->Update();
  framework::PanelManager::onFrame(frameTime);
  // T21: keep the debugger's log beacon off the strict UI (see the helper).
  suppressDebugBeacon();
  // Module frames precede the update pass so their DOM edits land in the
  // same Context::Update (order contract, modules/Module.h).
  if (Sink.frame)
    Sink.frame(frameTime);
#ifndef NDEBUG
  PerfTimer updateTimer;
#endif
  Context->Update();
#ifndef NDEBUG
  PerfAccumulateUpdate(updateTimer.milliseconds());
  captureFirstFrameEvidence();
#endif
}

void onDisconnected() {
  framework::PanelManager::setSessionActive(false);
  // Match the map-init data reset without destroying the menu context or
  // resetting the entire input router (the menu may already own it).
  hudstate::reset();
  weaponres::reset();
  playerres::resetAll();
}

void onRedraw(double frameTime) {
  // HUD_Redraw phase (see HudPanel::onRedraw): projection consumers
  // reposition, then a second Update so the same-frame render (which fires
  // later at OnRenderEndFrame) sees the fresh layout.
  if (!Initialised || !Context)
    return;
  framework::PanelManager::onRedraw(frameTime);
#ifndef NDEBUG
  PerfTimer updateTimer;
#endif
  Context->Update();
#ifndef NDEBUG
  PerfAccumulateUpdate(updateTimer.milliseconds());
#endif
}

void onVidInit() {
  modelpreview::invalidateAll();
  video::invalidateAll();
  backdrop::invalidateTargets();
  sprites::vidInit();
  if (Initialised)
    for (const auto &source : Rml::GetTextureSourceList())
      if (source.rfind("spr://", 0) == 0)
        Rml::ReleaseTexture(source);
  if (!Initialised)
    return;
  framework::Theme::applyScale(Context);
  Context->SetDimensions(Rml::Vector2i(
      static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth()),
      static_cast<int>(abc::MetaRenderer()->GetSwapChainHeight())));
  framework::PanelManager::vidInitAll();
  if (Sink.vidInit)
    Sink.vidInit();
}

void onKeyEvent(int down, int keynum) {
  if (!Initialised || !Context)
    return;
  InputBridge::onKeyEvent(Context, down, keynum);
}

void onWheel(int down, int keynum) {
  // The engine delivers the wheel as down-only keynum pulses (KeyMap note);
  // positive Rml wheel_delta scrolls down (pinned Win32 backend contract).
  if (!Initialised || !Context || !down)
    return;
  const float delta = keynum == K_MWHEELUP ? -1.0f : 1.0f;
  InputBridge_noteWheelDelivered();
  Context->ProcessMouseWheel(delta, 0);
}

void onHudShutdown() {
  shutdown();
}

} // namespace rmlui
