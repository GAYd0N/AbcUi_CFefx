// abcui.playerboard - the overhead name/health/armor tile for teammates.
// Moved from src/panels/playerboard (old CPlayerInfoPanel fleet, ABCEnchance
// sources src/hud/vgui/playerboard.*): the per-player vgui panels become 32
// static tiles in one document, re-targeted in the redraw phase from the
// world projection (cos-60 cone, 1024u range, same team, alive).
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

constexpr const char *kPanelName = "playerboard";
constexpr const char *kDocumentPath =
    "mod://abcui.playerboard/hud/rml/playerboard.rml";

// Old Viewport ctor cvars (persisted contract - never rename).
constexpr const char *kPlayerInfo = "hud_playerinfo";
constexpr const char *kPlayerInfoDanger = "hud_playerinfo_danger";

// Old Think gates (cos 60 cone, 1024u range).
constexpr float kMaxDistance = 1024.0f;
constexpr float kMinViewDot = 0.5f;
// Old SetPos centered the tile on the projection (x - GetWide()/2, wide 80).
constexpr float kTileHalfWidth = 40.0f;
// Old fill = BackGroundImage wide (67 in PlayerBoardPanel.res).
constexpr float kBarWidth = 67.0f;

AbcUiCvarHandle PlayerInfo = nullptr;
AbcUiCvarHandle PlayerInfoDanger = nullptr;

// Document element ids "tile-1".."tile-32".
std::string TileIds[ABCUI_MAX_PLAYERS + 1];
bool TileIdsReady = false;

const char *tileId(int index) {
  if (!TileIdsReady) {
    for (int i = 1; i <= ABCUI_MAX_PLAYERS; i++)
      TileIds[i] = "tile-" + std::to_string(i);
    TileIdsReady = true;
  }
  return TileIds[index].c_str();
}

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

AbcUiElement *tileElement(int index) {
  AbcUiDocument *doc = panelDocument();
  return doc ? Api->ui->querySelector(doc, tileId(index)) : nullptr;
}

// Per-tile inner-element lookup. The 32 tiles intentionally repeat the same
// inner ids in one document, so the lookup MUST stay subtree-scoped.
AbcUiElement *child(AbcUiElement *tile, const char *id) {
  return tile ? Api->ui->elementFind(tile, id) : nullptr;
}

void setProperty(AbcUiElement *element, const char *name, const char *value) {
  if (element)
    Api->ui->elementSetProperty(element, name, value);
}

int cvarInt(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

void hideAllTiles() {
  for (int index = 1; index <= ABCUI_MAX_PLAYERS; index++) {
    if (AbcUiElement *tile = tileElement(index))
      setProperty(tile, "display", "none");
  }
}

float cvarFloat(AbcUiCvarHandle cvar, float fallback) {
  return cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
}

// Old UpdateClientInfo: name/bars/icons refresh; false hides the tile.
bool updateClientInfo(AbcUiElement *tile, int entindex) {
  AbcUiPlayerInfo info{};
  info.structSize = sizeof(info);
  AbcUiPlayerInfo local{};
  local.structSize = sizeof(local);
  Api->players->get(entindex, &info);
  Api->players->local(&local);
  if (!info.valid || !local.valid)
    return false;
  if (info.spectate || info.team != local.team)
    return false;

  const float health = info.health;
  const bool dead = health <= 0.0f;
  // Health/armor clamp for display: negative overkill and >100 values are
  // wire-legal. A dead player's residual armor is not meaningful on the
  // overhead tile, so both bars read 0 while the dead icon shows.
  const float armorRatio =
      dead ? 0.0f : std::clamp(info.armor / 100.0f, 0.0f, 1.0f);
  const float healthRatio =
      dead ? 0.0f : std::clamp(health / 100.0f, 0.0f, 1.0f);

  if (AbcUiElement *name = child(tile, "name")) {
    // Server-derived name: escaped (the old vgui Label drew it plain).
    char escaped[256];
    Api->ui->escapeRml(info.name, escaped, sizeof(escaped));
    Api->ui->elementSetInnerRML(name, escaped);
    // Old SetFgColor(GetPlayerColor(index)): the client dll's per-player
    // tint (0..1 floats -> rgba byte triple, alpha 255).
    float tint[3];
    if (Api->players->clientColor(entindex, tint)) {
      char color[64];
      Api->ui->formatRgba(static_cast<int>(tint[0] * 255.0f),
                          static_cast<int>(tint[1] * 255.0f),
                          static_cast<int>(tint[2] * 255.0f), 255, color,
                          sizeof(color));
      setProperty(name, "color", color);
    }
  }
  const std::string healthWidth =
      std::format("{:g}dp", kBarWidth * healthRatio);
  setProperty(child(tile, "bar-health"), "width", healthWidth.c_str());
  const std::string armorWidth = std::format("{:g}dp", kBarWidth * armorRatio);
  setProperty(child(tile, "bar-armor"), "width", armorWidth.c_str());

  // Danger band icons: below hud_playerinfo_danger - dead -> skull, hurt ->
  // medkit; otherwise the crouch icon rides the hull height (old maxs.z -
  // mins.z < 64).
  AbcUiEntityState entity{};
  entity.structSize = sizeof(entity);
  Api->players->entity(entindex, &entity);
  const float danger = cvarFloat(PlayerInfoDanger, 30.0f);
  // Priority is dead > medkit > crouch; an unusual health value never hides
  // the tile.
  const bool showMedkit = !dead && health < danger;
  const bool showDead = dead;
  const bool showCrouch = !showMedkit && !showDead && entity.valid &&
                          std::fabs(entity.maxs[2] - entity.mins[2]) < 64.0f;

  setProperty(child(tile, "icon-crouch"), "display",
              showCrouch ? "block" : "none");
  setProperty(child(tile, "icon-medkit"), "display",
              showMedkit ? "block" : "none");
  setProperty(child(tile, "icon-dead"), "display", showDead ? "block" : "none");
  return true;
}

// ---- panel callbacks ----

void panelReset(void *) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  for (int index = 1; index <= ABCUI_MAX_PLAYERS; index++) {
    if (AbcUiElement *tile = tileElement(index))
      setProperty(tile, "display", "none");
  }
}

void panelOnData(void *, double) {
  // The document must be shown for any tile to render; per-tile display
  // flips carry the real visibility.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (state.hideBits & ABCUI_HIDEBIT_ALL)
    Api->ui->documentHide(doc);
  else
    Api->ui->documentShow(doc);
}

void panelOnRedraw(void *, double) {
  // Old Think gate chain: cvar -> local player -> self -> entity state ->
  // range -> view cone; the survivor gets UpdateClientInfo.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);
  if (cvarInt(PlayerInfo, 1) <= 0 || player.index == 0 ||
      (state.hideBits & ABCUI_HIDEBIT_ALL) || state.spectate) {
    hideAllTiles();
    return;
  }

  // View forward (AngleVectors).
  const float pitch = player.angles[0] * 3.14159265358979323846f / 180.0f;
  const float yaw = player.angles[1] * 3.14159265358979323846f / 180.0f;
  const float view[3] = {std::cos(pitch) * std::cos(yaw),
                         std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};

  // All 32 tiles re-evaluated per frame (the old fleet's Think); hidden tiles
  // cost a display flip only.
  for (int index = 1; index <= ABCUI_MAX_PLAYERS; index++) {
    AbcUiElement *tile = tileElement(index);
    if (!tile)
      continue;
    if (index == player.index) {
      setProperty(tile, "display", "none");
      continue;
    }

    AbcUiEntityState entity{};
    entity.structSize = sizeof(entity);
    Api->players->entity(index, &entity);
    if (!entity.valid || entity.messageNum != player.messageNum ||
        !entity.isPlayer) {
      setProperty(tile, "display", "none");
      continue;
    }

    float toTarget[3];
    float distanceSquared = 0.0f;
    for (int i = 0; i < 3; i++) {
      toTarget[i] = entity.origin[i] - player.origin[i];
      distanceSquared += toTarget[i] * toTarget[i];
    }
    if (distanceSquared >= kMaxDistance * kMaxDistance) {
      setProperty(tile, "display", "none");
      continue;
    }

    const float distance = std::sqrt(distanceSquared);
    float dot = 0.0f;
    for (int i = 0; i < 3; i++)
      dot += toTarget[i] * view[i];
    if (distance <= 0.0f) {
      setProperty(tile, "display", "none");
      continue;
    }
    dot /= distance;
    if (dot <= kMinViewDot) {
      setProperty(tile, "display", "none");
      continue;
    }

    const float anchor[3] = {entity.origin[0], entity.origin[1],
                             entity.origin[2] + 45.0f};
    float screen[2] = {};
    if (Api->ui->project(anchor, screen) != 0) {
      setProperty(tile, "display", "none");
      continue;
    }

    const std::string left = std::format("{:g}px", screen[0] - kTileHalfWidth);
    const std::string top = std::format("{:g}px", screen[1]);
    setProperty(tile, "left", left.c_str());
    setProperty(tile, "top", top.c_str());
    setProperty(tile, "display",
                updateClientInfo(tile, index) ? "block" : "none");
  }
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = nullptr;
  Panel.onData = &panelOnData;
  Panel.onRedraw = &panelOnRedraw;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);
}

void onModuleInit() {
  PlayerInfo = Api->console->registerCvar(kPlayerInfo, "1", ABCUI_CVAR_VALUE);
  PlayerInfoDanger =
      Api->console->registerCvar(kPlayerInfoDanger, "30", ABCUI_CVAR_VALUE);
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data || !api->players)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi) ||
      api->players->structSize < sizeof(AbcUiPlayersApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.playerboard";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = nullptr;
  out->onUnload = &onModuleUnload;
  return 1;
}
