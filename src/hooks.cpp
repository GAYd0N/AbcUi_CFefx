// AbcUi hook spine (see hooks.h for the consumption map).
#include <metahook.h>

#include "hooks.h"

#include <abc/Console.h>
#include <abc/GameData.h>
#include <abc/Plugin.h>

#include <keydefs.h>
#include <usercmd.h>
#include <in_buttons.h>

#include "data/HudState.h"
#include "data/NetworkMessages.h"
#include "data/PlayerResource.h"
#include "data/PlayerTrace.h"
#include "data/TextFeed.h"
#include "data/WeaponResource.h"
#include "framework/PanelManager.h"
#include "modules/HudPanelCatalog.h"
#include "modules/Module.h"
#include "panels/gindicator/GIndicatorPanel.h"
#include "panels/itemhighlight/ItemHighlightPanel.h"
#include "panels/motd/MotdPanel.h"
#include "panels/scoreboard/ScoreboardPanel.h"
#include "panels/vote/VotePanel.h"
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
// Index 3 is SKIPPED on purpose: every historical variant of the old code
// left exactly one list slot visible (the dynamic_cast era commented out its
// clear; the index era's switch is 0,1,2,4). Which SC panel that slot is was
// not fully pinned down (candidates: CHudAmmoSecondary / CHudMenu); the port
// reproduces the old code's observable behavior.
uint32_t VanillaHudFlagsOffset = 0;
constexpr size_t kVanillaHudMaxPanels = 5;
constexpr size_t kVanillaHudSkipIndex =
    3; // deliberately NOT suppressed (old code parity)

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
    if (node->panel && i != kVanillaHudSkipIndex)
      VanillaHudPanels[VanillaHudPanelCount++] = node->panel;
    node = reinterpret_cast<HudListNode *>(node->next);
  }
}

void hideVanillaHudPanels() {
  for (size_t i = 0; i < VanillaHudPanelCount; i++)
    (*(DWORD *)(VanillaHudPanels[i] + VanillaHudFlagsOffset)) &= ~1;
}

} // namespace

static void H_HUD_Init() {
  gExportfuncs.HUD_Init();
  // Command/cvar registration is HUD_Init-time only (plan 08 §4.9);
  // guarded like AbcEssential in case the engine re-runs HUD_Init.
  static bool CommandsRegistered = false;
  if (!CommandsRegistered) {
    CommandsRegistered = true;
    netmsg::init();
    services::voice::status().init();
    hudstate::init();
    weaponres::init();
    playerres::init();
    textfeed::init();
    modules::installHudPanels();
    modules::registerHudPanelCvars();
    rmlui::registerCommands();
    modules::registry().initAll();
  }
}

static int H_HUD_VidInit() {
  rebuildVanillaHudPanels();
  int result = gExportfuncs.HUD_VidInit();
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
  // Old order: strip the vanilla panels' visible bit before the client
  // redraws, then chain. The reposition pump runs in this phase (the old
  // vgui think phase - the only phase where pTriAPI->WorldToScreen sees the
  // live view matrices). HUD_Redraw's `time` is the absolute client clock
  // (not a delta like HUD_Frame's) - convert for the frame pumps.
  static float LastRedrawTime = 0.0f;
  float delta = time - LastRedrawTime;
  LastRedrawTime = time;
  if (delta < 0.0f || delta > 0.5f)
    delta = 0.0f;
  hideVanillaHudPanels();
  // Old Viewport::SetInterMission -> ShowPanel(intermission > 0); the
  // +/-showscores commands stay dead while it lasts.
  panels::scoreboard::setIntermission(intermission != 0);
  rmlui::onRedraw(delta);
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
  // Old Viewport::AddEntity routing: grenade indicator + item highlight.
  panels::gindicator::onAddEntity(ent, modelname);
  panels::itemhighlight::onAddEntity(ent, modelname);
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

// Wheel claims that are NOT ownership (T06): the open motd scrolls (old
// per-page auto-advance died with the pagination - D-17 ledger) without
// holding UI input. Owner-backed wheel claims (scoreboard pointer mode,
// menu pages) ride the router's wheelClaimed() inside rmlui::routeKey.
bool uiOwnsWheel(int down, int keynum) {
  if (!down || (keynum != K_MWHEELUP && keynum != K_MWHEELDOWN))
    return false;
  return panels::motd::wantsWheel();
}

} // namespace

static int H_HUD_Key_Event(int down, int keynum,
                           const char *pszCurrentBinding) {
  // Static interactive-HUD consumers stay the P6-proven pre-stage (vote
  // answers, scoreboard pointer enter). routeKey is then the single
  // physical injection point: the router decides UI feeds and the
  // engine-consume flag (keydown/up pairing, auto-repeat dedup, pre-held
  // passthrough and focus-loss cleanup live there - T06).
  const bool consumed = panels::vote::onKeyEvent(down, keynum) ||
                        panels::scoreboard::onKeyEvent(down, keynum);
#ifndef NDEBUG
  if (keynum >= K_MOUSE2 || keynum == K_MWHEELUP || keynum == K_MWHEELDOWN)
    abc::ConsolePrint("[AbcUi] key: down=%d keynum=%d consumed=%d\n", down,
                      keynum, consumed ? 1 : 0);
#endif
  if (consumed || rmlui::routeKey(down, keynum, uiOwnsWheel(down, keynum)))
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
  return gExportfuncs.HUD_GetStudioModelInterface(version, ppinterface,
                                                  pstudio);
}

static void H_HUD_Shutdown() {
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
  if (panels::scoreboard::inScore())
    cmd->buttons |= IN_SCORE;
  if (owner) {
    cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2);
    for (int i = 0; i < 3; ++i)
      cmd->viewangles[i] = angles[i];
    gEngfuncs.SetViewAngles(angles);
  }
}

void attachExports(cl_exportfuncs_t *pExportFuncs) {
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
// TFV_ShowVGUIMenu). The SC client opens every
// built-in viewport window through CTeamFortressViewport::ShowVGUIMenu(id);
// the old mod inline-hooked it so the vanilla mission brief (4) never opens
// (the missionbriefing command chain feeds the new motd panel) and the
// vanilla MOTD window (5) yields while hud_motd is non-zero (>= 1 the new
// panel replaces it, < 0 suppresses both, 0 restores the vanilla window).
// Every other menu id chains untouched.
constexpr int kMenuMissionBrief = 4;
constexpr int kMenuMotd = 5;

using ShowVguiMenuFn = void(__fastcall *)(void *pthis, int dummy, int menu);
ShowVguiMenuFn OriginalShowVguiMenu = nullptr;
hook_t *ShowVguiMenuHook = nullptr;

static void __fastcall H_TFV_ShowVGUIMenu(void *pthis, int dummy, int menu) {
  if (menu == kMenuMissionBrief)
    return;
  if (menu == kMenuMotd && panels::motd::blocksVanillaWindow()) {
    panels::motd::showMotd();
    return;
  }
  if (OriginalShowVguiMenu)
    OriginalShowVguiMenu(pthis, dummy, menu);
}

} // namespace

void hookVanillaMenuRoute() {
  const auto target = abc::ResolveGameSymbol(
      abc::g_dwClientBase, "client", "TeamFortressViewport_ShowVGUIMenu",
      MH_GAMESYMBOL_KIND_FUNCTION);
  ShowVguiMenuHook = g_pMetaHookAPI->InlineHook(
      target, H_TFV_ShowVGUIMenu,
      reinterpret_cast<void **>(&OriginalShowVguiMenu));
}

void unhookVanillaMenuRoute() {
  if (ShowVguiMenuHook) {
    g_pMetaHookAPI->UnHook(ShowVguiMenuHook);
    ShowVguiMenuHook = nullptr;
    OriginalShowVguiMenu = nullptr;
  }
}

} // namespace hooks
