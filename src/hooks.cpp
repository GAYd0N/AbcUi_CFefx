// AbcUi hook spine (see hooks.h for the consumption map).
#include <metahook.h>

#include "hooks.h"

#include <iterator>

#include <abc/Console.h>
#include <abc/GameData.h>
#include <abc/Plugin.h>

#include <abcui/events.h>

#include <com_model.h>
#include <keydefs.h>
#include <usercmd.h>
#include <in_buttons.h>

#include "data/HudState.h"
#include "data/NetworkMessages.h"
#include "data/PlayerResource.h"
#include "data/PlayerTrace.h"
#include "data/TextFeed.h"
#include "data/WeaponResource.h"
#include "framework/Commands.h"
#include "framework/PanelManager.h"
#include "framework/ScoreSeat.h"
#include "modsys/EventBus.h"
#include "modsys/ModuleManager.h"
#include "modules/Module.h"
#include "render/BackendHost.h"
#include "render/StandaloneView.h"
#include "rmlui/InputBridge.h"
#include "rmlui/RmlUiModule.h"
#include "services/steam/SteamAPI.h"
#include "services/voice/VoiceStatus.h"
#include "services/GameUiBridge.h"

namespace hooks {

engine_studio_api_s *EngineStudioApi = nullptr;
NativePlayerInfoView NativeHudPanelInfo;
GetClientColorFn GetClientColor = nullptr;

namespace {

// Vanilla HUD suppression: gHUD begins with the native HUD list head.
// Resolve the object by its canonical global name, without decoding VidInit.
// Only the visible flag is changed; list ownership stays with the client.
//
// The supported Sven client initializes these list entries in this order:
// health, battery, ammo, train, flashlight, crosshair. Never hide the train.
// Suppression is a module veto and the visible bit is restored after redraw;
// removing a claim cannot leave a native element permanently disabled.
uint32_t VanillaHudFlagsOffset = 0;
constexpr int32_t kVanillaHudElements[] = {
    ABCUI_NATIVE_HUD_HEALTH,     ABCUI_NATIVE_HUD_BATTERY,
    ABCUI_NATIVE_HUD_AMMO,       -1, // train
    ABCUI_NATIVE_HUD_FLASHLIGHT, ABCUI_NATIVE_HUD_CROSSHAIR};
constexpr size_t kVanillaHudMaxPanels = std::size(kVanillaHudElements);

DWORD VanillaHudListAddr = 0;
DWORD VanillaHudPanels[kVanillaHudMaxPanels] = {};
size_t VanillaHudPanelCount = 0;

void findVanillaHudList() {
  VanillaHudListAddr = reinterpret_cast<DWORD>(abc::ResolveGameSymbol(
      abc::g_dwClientBase, "client", "gHUD", MH_GAMESYMBOL_KIND_GLOBAL));
  VanillaHudFlagsOffset =
      abc::GameStructMember(abc::g_dwClientBase, "AbcUiHudBaseView.flags");
}

void rebuildVanillaHudPanels() {
  VanillaHudPanelCount = 0;
  if (!VanillaHudListAddr)
    return;
  struct HudListNode {
    DWORD panel;
    DWORD next;
  };
  HudListNode *node =
      reinterpret_cast<HudListNode *>(*(DWORD *)VanillaHudListAddr);
  for (size_t i = 0; i < kVanillaHudMaxPanels && node; i++) {
    VanillaHudPanels[VanillaHudPanelCount++] = node->panel;
    node = reinterpret_cast<HudListNode *>(node->next);
  }
}

class NativeHudDrawScope {
public:
  NativeHudDrawScope() {
    for (size_t i = 0; i < VanillaHudPanelCount; ++i) {
      if (kVanillaHudElements[i] < 0 || !VanillaHudPanels[i])
        continue;
      const AbcUiNativeHudEvent event{kVanillaHudElements[i]};
      if (!framework::eventBus().publishVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW,
                                             event))
        continue;
      auto *flags = reinterpret_cast<DWORD *>(VanillaHudPanels[i] +
                                              VanillaHudFlagsOffset);
      if (*flags & 1) {
        Hidden[Count++] = flags;
        *flags &= ~DWORD{1};
      }
    }
  }
  ~NativeHudDrawScope() {
    for (size_t i = 0; i < Count; ++i)
      *Hidden[i] |= DWORD{1};
  }

private:
  DWORD *Hidden[kVanillaHudMaxPanels]{};
  size_t Count = 0;
};

} // namespace

static void H_HUD_Init() {
  gExportfuncs.HUD_Init();
  framework::standaloneview::installClientHooks();
  // HUD_Init/HUD_Shutdown can repeat within one engine process (the client
  // is reloaded on some map-change/reconnect paths). Everything that
  // HUD_Shutdown tears down must be re-armed here.
  //
  // - usermsg hooks: init() installs the chain once. Re-hooking on every
  //   cycle would make AbcEssential's and this plugin's hooks point at each
  //   other, so the only re-install path is a detected client DLL reload
  //   (refreshAfterClientLoad probes a Ui-exclusive message).
  // - panels: PanelManager::shutdown() clears the list, so rebuild it when
  //   it is empty (a live list is left alone).
  netmsg::init();
  netmsg::refreshAfterClientLoad();
  // Module packages boot once per process, before the panel install, so
  // module-registered panels join the first HUD_Init cycle.
  framework::modmanager::boot();
  if (framework::PanelManager::panels().empty())
    framework::PanelManager::installPanels(framework::modmanager::takePanels());
  // Process-scoped registration (cvars/commands survive HUD_Shutdown).
  static bool CommandsRegistered = false;
  if (!CommandsRegistered) {
    CommandsRegistered = true;
    services::voice::status().init();
    hudstate::init();
    weaponres::init();
    playerres::init();
    textfeed::init();
    rmlui::registerCommands();
    // The render-backend cvar registers here (not at LoadClient): the boot
    // id is already resolved, and an engine-created archived value keeps it.
    abc::render::registerCommands();
    modules::registry().initAll();
  }
}

static int H_HUD_VidInit() {
  int result = gExportfuncs.HUD_VidInit();
  // Capture AFTER the client re-ran its own VidInit: if a client rebuilds
  // the gHUD chain per map, pointers grabbed before the call would be freed
  // and native suppression wrote into them every frame.
  rebuildVanillaHudPanels();
  hudstate::reset();
  weaponres::reset();
  playerres::resetAll();
  services::voice::status().resetForWorld();
  rmlui::onVidInit();
  return result;
}

static void H_HUD_Frame(double frametime) {
  gExportfuncs.HUD_Frame(frametime);
  // Old exportfuncs HUD_Frame tail: the player traces refresh once per
  // frame; render-phase consumers (crosshair third-person centre) read the
  // cache later in the frame. The level gate is the trace's world
  // precondition (see PlayerTrace.h): tracing while a map change replaces
  // the world faults inside the engine's own trace.
  playertrace::update(services::nativeUiBridge().hasLevel());
  services::voice::status().frame();
  rmlui::onFrame(frametime);
}

static int H_HUD_Redraw(float time, int intermission) {
  // Temporarily strip module-claimed native elements before chaining the
  // client redraw. The reposition pump runs in this phase (the old
  // vgui think phase - the only phase where pTriAPI->WorldToScreen sees the
  // live view matrices). HUD_Redraw's `time` is the absolute client clock
  // (not a delta like HUD_Frame's) - convert for the frame pumps.
  static float LastRedrawTime = 0.0f;
  float delta = time - LastRedrawTime;
  LastRedrawTime = time;
  if (delta < 0.0f || delta > 0.5f)
    delta = 0.0f;
  // Old Viewport::SetInterMission -> ShowPanel(intermission > 0); the
  // +/-showscores commands stay dead while it lasts.
  framework::scoreseat::setIntermission(intermission != 0);
  rmlui::onRedraw(delta);
  // RedrawBeat render backends (the GL backend) render their frame sink
  // here; callback-driven backends (Renderer) ignore the pump.
  abc::render::pumpRedraw(delta);
  const NativeHudDrawScope nativeDraw;
  return gExportfuncs.HUD_Redraw(time, intermission);
}

static int H_HUD_UpdateClientData(struct client_data_s *cdata, float time) {
  // Observe-first (the old exportfuncs order): data layer before the
  // engine's own handler.
  hudstate::onUpdateClientData(cdata);
  return gExportfuncs.HUD_UpdateClientData(cdata, time);
}

static void H_HUD_TxferLocalOverrides(struct entity_state_s *state,
                                      const struct clientdata_s *client) {
  // Old exportfuncs: gClientData = client (the prediction client data the
  // crosshair/effect panels read through hudstate).
  hudstate::onTxferLocalOverrides(client);
  gExportfuncs.HUD_TxferLocalOverrides(state, client);
}

static int H_HUD_AddEntity(int type, struct cl_entity_s *ent,
                           const char *modelname) {
  // Old Viewport::AddEntity routing (grenade indicator, item highlight) is
  // now a bus channel: modules observe the same pass the engine sees.
  AbcUiAddEntityEvent entity{};
  entity.type = type;
  if (ent) {
    entity.index = ent->index;
    entity.modelType = ent->model ? static_cast<int32_t>(ent->model->type) : 0;
    entity.messageNum = ent->curstate.messagenum;
    entity.effects = ent->curstate.effects;
    entity.modelIndex = ent->curstate.modelindex;
    entity.moveType = ent->baseline.movetype;
    entity.origin[0] = ent->curstate.origin[0];
    entity.origin[1] = ent->curstate.origin[1];
    entity.origin[2] = ent->curstate.origin[2];
  }
  if (modelname) {
    size_t i = 0;
    for (; i + 1 < sizeof(entity.model) && modelname[i]; ++i)
      entity.model[i] = modelname[i];
    entity.model[i] = '\0';
  }
  framework::eventBus().publish(ABCUI_CHANNEL_HUD_ADD_ENTITY, entity);
  return gExportfuncs.HUD_AddEntity(type, ent, modelname);
}

static void H_HUD_TxferPredictionData(struct entity_state_s *ps,
                                      const struct entity_state_s *pps,
                                      struct clientdata_s *pcd,
                                      const struct clientdata_s *ppcd,
                                      struct weapon_data_s *wd,
                                      const struct weapon_data_s *pwd) {
  weaponres::onTxferPredictionData(pwd);
  gExportfuncs.HUD_TxferPredictionData(ps, pps, pcd, ppcd, wd, pwd);
}

namespace {

// Wheel claims that are NOT ownership (T06): an open module document (the
// motd) scrolls without holding UI input. Modules claim through the
// input.wheel veto channel. Owner-backed wheel claims (scoreboard pointer
// mode, menu pages) ride the router's wheelClaimed() inside rmlui::routeKey.
bool moduleWheelClaim(int down, int keynum) {
  if (!down || (keynum != K_MWHEELUP && keynum != K_MWHEELDOWN))
    return false;
  const AbcUiKeyEvent event{1, keynum};
  return framework::eventBus().publishVeto(ABCUI_CHANNEL_INPUT_WHEEL, event);
}

} // namespace

static int H_HUD_Key_Event(int down, int keynum,
                           const char *pszCurrentBinding) {
  // Static interactive-HUD consumers stay the P6-proven pre-stage (the
  // module input.key veto channel - vote answers -, scoreboard pointer
  // enter). routeKey is then the single physical injection point: the
  // router decides UI feeds and the engine-consume flag (keydown/up
  // pairing, auto-repeat dedup, pre-held passthrough and focus-loss cleanup
  // live there - T06).
  const AbcUiKeyEvent keyEvent{down ? 1 : 0, keynum};
  const bool consumed =
      framework::eventBus().publishVeto(ABCUI_CHANNEL_INPUT_KEY, keyEvent) ||
      framework::scoreseat::onKeyEvent(down, keynum);
#ifndef NDEBUG
  if (keynum >= K_MOUSE2 || keynum == K_MWHEELUP || keynum == K_MWHEELDOWN)
    abc::ConsolePrint("[AbcUi] key: down=%d keynum=%d consumed=%d\n", down,
                      keynum, consumed ? 1 : 0);
#endif
  if (consumed || rmlui::routeKey(down, keynum, moduleWheelClaim(down, keynum)))
    return 0;
  return gExportfuncs.HUD_Key_Event(down, keynum, pszCurrentBinding);
}

#ifndef NDEBUG
int debugKeyEvent(int down, int keynum) {
  return H_HUD_Key_Event(down, keynum, "");
}
#endif

static int
H_HUD_GetStudioModelInterface(int version,
                              struct r_studio_interface_s **ppinterface,
                              struct engine_studio_api_s *pstudio) {
  // Old exportfuncs: mirror the engine studio API once (player info reads
  // in data::playerres; AbcEssential holds its own copy - D-01 no sharing).
  EngineStudioApi = pstudio;
  const int result =
      gExportfuncs.HUD_GetStudioModelInterface(version, ppinterface, pstudio);
  // The standalone-view route draws preview entities through the interface
  // the engine ended up with (the client's, or the engine's own default
  // when the client provided none).
  framework::standaloneview::captureStudioInterfaces(ppinterface, pstudio);
  return result;
}

static void H_HUD_Shutdown() {
  VanillaHudPanelCount = 0;
  // Remove hooks before a possible client unload. Ordinary HUD cycles keep
  // the studio slot and re-arm portal hooks from H_HUD_Init.
  framework::standaloneview::uninstallClientHooks();
  services::voice::status().shutdown();
  rmlui::onHudShutdown();
  gExportfuncs.HUD_Shutdown();
  // Old UninstallClientHook order: the hook comes down after the client's
  // own shutdown ran (it may still open viewport windows on the way out).
  unhookVanillaMenuRoute();
}

// Keep relative mouse motion and already-held attacks out of UI mode.
static void H_IN_Accumulate() {
  if (!rmlui::uiMouseOwner())
    gExportfuncs.IN_Accumulate();
}
static void H_IN_MouseEvent(int state) {
  static int previous = 0;
  if (rmlui::uiMouseOwner()) {
    // Some clients generate their mouse key edges inside IN_MouseEvent.
    // Route those edges before neutralising the game-side button mask.
    for (int i = 0; i < 3; ++i)
      if ((state ^ previous) & (1 << i))
        rmlui::onKeyEvent((state & (1 << i)) != 0, K_MOUSE1 + i);
    gExportfuncs.IN_MouseEvent(0);
  } else {
    gExportfuncs.IN_MouseEvent(state);
  }
  previous = state;
}
static void H_CL_CreateMove(float time, usercmd_s *cmd, int active) {
  const bool owner = rmlui::uiMouseOwner();
  float angles[3]{};
  if (owner)
    gEngfuncs.GetViewAngles(angles);
  gExportfuncs.CL_CreateMove(time, cmd, active);
  // Old exportfuncs CL_CreateMove: the scoreboard latch rides the usercmd
  // button stream as IN_SCORE - the server streams every player's
  // ping/loss only to clients it believes hold the scoreboard (issue #9;
  // without this the engine player tables sit at 0/0 the whole session).
  if (framework::scoreseat::held())
    cmd->buttons |= IN_SCORE;
  if (owner) {
    cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2);
    for (int i = 0; i < 3; ++i)
      cmd->viewangles[i] = angles[i];
    gEngfuncs.SetViewAngles(angles);
  }
}

// Standalone-view routing (issue #24 comment): while a preview view is
// current, the calc-refdef / triangle exports belong to it (onlyClientDraw
// pass); otherwise they chain into the client originals as always.
static void H_V_CalcRefdef(struct ref_params_s *pparams) {
  if (framework::standaloneview::hookCalcRefdef(pparams))
    return;
  gExportfuncs.V_CalcRefdef(pparams);
}

static void H_HUD_DrawNormalTriangles() {
  if (framework::standaloneview::hookDrawNormalTriangles())
    return;
  gExportfuncs.HUD_DrawNormalTriangles();
}

static void H_HUD_DrawTransparentTriangles() {
  if (framework::standaloneview::hookDrawTransparentTriangles())
    return;
  gExportfuncs.HUD_DrawTransparentTriangles();
}

void attachExports(cl_exportfuncs_t *pExportFuncs) {
  // LoadClient, unlike HUD_Init, identifies a new client-export lifetime.
  EngineStudioApi = nullptr;
  framework::standaloneview::captureStudioInterfaces(nullptr, nullptr);
  pExportFuncs->IN_ActivateMouse = rmlui::activateGameMouse;
  pExportFuncs->IN_Accumulate = H_IN_Accumulate;
  pExportFuncs->IN_MouseEvent = H_IN_MouseEvent;
  pExportFuncs->CL_CreateMove = H_CL_CreateMove;
  pExportFuncs->HUD_Init = H_HUD_Init;
  pExportFuncs->HUD_VidInit = H_HUD_VidInit;
  pExportFuncs->HUD_Redraw = H_HUD_Redraw;
  pExportFuncs->HUD_Frame = H_HUD_Frame;
  pExportFuncs->HUD_Key_Event = H_HUD_Key_Event;
  pExportFuncs->HUD_GetStudioModelInterface = H_HUD_GetStudioModelInterface;
  // These wrappers pass through while no standalone view owns the call.
  // Install even when startup resolution failed, so a HUD_Init retry can
  // recover without replacing the engine's export chain mid-process.
  pExportFuncs->V_CalcRefdef = H_V_CalcRefdef;
  pExportFuncs->HUD_DrawNormalTriangles = H_HUD_DrawNormalTriangles;
  pExportFuncs->HUD_DrawTransparentTriangles = H_HUD_DrawTransparentTriangles;
  pExportFuncs->HUD_UpdateClientData = H_HUD_UpdateClientData;
  pExportFuncs->HUD_TxferPredictionData = H_HUD_TxferPredictionData;
  pExportFuncs->HUD_TxferLocalOverrides = H_HUD_TxferLocalOverrides;
  pExportFuncs->HUD_AddEntity = H_HUD_AddEntity;
  pExportFuncs->HUD_Shutdown = H_HUD_Shutdown;
}

void locatePlayerColor() {
  GetClientColor = reinterpret_cast<GetClientColorFn>(
      abc::ResolveGameSymbol(abc::g_dwClientBase, "client", "GetClientColor",
                             MH_GAMESYMBOL_KIND_FUNCTION));
}

void locateNativeHudPanelInfo() {
  NativeHudPanelInfo.base = static_cast<const std::byte *>(
      abc::ResolveGameSymbol(abc::g_dwClientBase, "client", "g_PlayerExtraInfo",
                             MH_GAMESYMBOL_KIND_GLOBAL));
  NativeHudPanelInfo.stride =
      abc::GameScalar(abc::g_dwClientBase, "AbcUi_PlayerExtraInfo_stride");
  NativeHudPanelInfo.donorOffset = abc::GameStructMember(
      abc::g_dwClientBase, "AbcUiPlayerExtraInfoView.donor");
  const auto base = reinterpret_cast<uintptr_t>(abc::g_dwClientBase);
  const auto table = reinterpret_cast<uintptr_t>(NativeHudPanelInfo.base);
  const auto size = abc::g_dwClientSize;
  if (!NativeHudPanelInfo.valid() || table < base || table - base >= size ||
      NativeHudPanelInfo.stride > (size - (table - base)) / 33)
    abc::SysError("[AbcUi] Invalid g_PlayerExtraInfo layout or array outside "
                  "client image");
}

void locateVanillaHudList() {
  findVanillaHudList();
}

namespace {

// Native viewport menu route (from: ABCEnchance exportfuncs.cpp
// TFV_ShowVGUIMenu). The SC client opens every built-in viewport window
// through CTeamFortressViewport::ShowVGUIMenu(id); the hook republishes each
// request on the menu.vguimenu veto channel so the owning module decides
// (the motd module suppresses the vanilla mission brief (4) and, while
// hud_motd is non-zero, the vanilla MOTD window (5)). An unconsumed request
// chains untouched.

using ShowVguiMenuFn = void(__fastcall *)(void *pthis, int dummy, int menu);
ShowVguiMenuFn OriginalShowVguiMenu = nullptr;
hook_t *ShowVguiMenuHook = nullptr;

static void __fastcall H_TFV_ShowVGUIMenu(void *pthis, int dummy, int menu) {
  const AbcUiVguiMenuEvent event{menu};
  if (framework::eventBus().publishVeto(ABCUI_CHANNEL_MENU_VGUI_MENU, event))
    return; // consumed: the vanilla window must not open
  if (OriginalShowVguiMenu)
    OriginalShowVguiMenu(pthis, dummy, menu);
}

} // namespace

void hookVanillaMenuRoute() {
  const auto target = abc::ResolveGameSymbol(
      abc::g_dwClientBase, "client", "TeamFortressViewport_ShowVGUIMenu",
      MH_GAMESYMBOL_KIND_FUNCTION);
  if (!target) {
    abc::SysError("[AbcUi] Failed to resolve "
                  "TeamFortressViewport_ShowVGUIMenu");
    return;
  }
  ShowVguiMenuHook = g_pMetaHookAPI->InlineHook(
      target, H_TFV_ShowVGUIMenu,
      reinterpret_cast<void **>(&OriginalShowVguiMenu));
  // Fail fast like locateNativeHudPanelInfo: without this hook the vanilla
  // mission brief/MOTD windows stay reachable and silently shadow the panels.
  if (!ShowVguiMenuHook)
    abc::SysError("[AbcUi] Failed to hook TeamFortressViewport_ShowVGUIMenu");
}

void unhookVanillaMenuRoute() {
  if (ShowVguiMenuHook) {
    g_pMetaHookAPI->UnHook(ShowVguiMenuHook);
    ShowVguiMenuHook = nullptr;
    OriginalShowVguiMenu = nullptr;
  }
}

} // namespace hooks
