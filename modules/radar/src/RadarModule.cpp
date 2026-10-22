// abcui.radar - the FBO minimap plate. Moved from src/panels/radar (old
// CRadarPanel, ABCEnchance sources src/hud/vgui/radar.*).
//
// Route 2: the document owns the plate art, the north/aim marks and the
// player avatars; the world render goes to the framework's 1024^2 FBO
// producer (src/render/RadarRender) during the engine pre-view phase, and the
// FBO colour texture is bound to the "radarfbo://" external texture family.
// The module only reports whether the plate is on screen (setActive), picks
// the square/round mask and rebuilds the map URI when the FBO generation
// changes - no per-frame texture uploads exist on this path.
//
// The plate geometry stays in design units for the box (+scale_radar swaps
// 100 -> 300) while the avatar size and the per-frame avatar/north placement
// stay raw pixels, exactly the old code's mixed-unit behaviour.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

constexpr const char *kPanelName = "radar";
constexpr const char *kDocumentPath = "mod://abcui.radar/hud/rml/radar.rml";

// Old ctor defaults (persisted contract - never rename).
constexpr const char *kRadar = "hud_radar";          // 1 base, 2 round, 0 off
constexpr const char *kRadarZoom = "hud_radar_zoom"; // FBO ortho zoom
constexpr const char *kRadarAvatar = "hud_radar_avatar"; // teammate avatars
constexpr const char *kRadarAvatarSize = "hud_radar_avatarsize";

AbcUiCvarHandle Radar = nullptr;
AbcUiCvarHandle RadarZoom = nullptr;
AbcUiCvarHandle RadarAvatar = nullptr;
AbcUiCvarHandle RadarAvatarSize = nullptr;

bool Scaled = false;

// Avatar <img> elements keyed by player index (rebuilt incrementally; a row
// that leaves the plate is removed).
std::unordered_map<int, AbcUiElement *> AvatarElements;
std::unordered_map<int, std::string> AvatarUris;
std::string MapUri;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

AbcUiElement *query(const char *id) {
  AbcUiDocument *doc = panelDocument();
  return doc ? Api->ui->querySelector(doc, id) : nullptr;
}

float cvarFloat(AbcUiCvarHandle cvar, float fallback) {
  return cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
}

void setProperty(AbcUiElement *element, const char *name, const char *value) {
  if (element)
    Api->ui->elementSetProperty(element, name, value);
}

std::string pixelValue(float value) {
  char text[32];
  std::snprintf(text, sizeof(text), "%gpx", static_cast<double>(value));
  return text;
}

void setPlateSize(bool scaled) {
  Scaled = scaled;
  const char *size = scaled ? "300dp" : "100dp";
  setProperty(query("plate"), "width", size);
  setProperty(query("plate"), "height", size);
}

void onScaleRadarDown(void *) {
  setPlateSize(true);
}

void onScaleRadarUp(void *) {
  setPlateSize(false);
}

// Removes every avatar element (map change / shutdown: the document dies).
void clearAvatars() {
  for (auto &[_, element] : AvatarElements) {
    if (element)
      Api->ui->elementRemove(element);
  }
  AvatarElements.clear();
  AvatarUris.clear();
}

// ---- panel callbacks ----

void panelReset(void *) {
  // Old CRadarPanel::Reset: SetScale(false).
  setPlateSize(false);
}

void panelOnData(void *, double) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;

  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);

  // Old Paint gate: cvar > 0 and a suit. D-19: hide (the old early return
  // froze the last plate) and also gate on the hide-all token.
  const bool enabled = cvarFloat(Radar, 1.0f) > 0.0f;
  const bool visible = enabled && state.suit &&
                       !(state.hideBits & ABCUI_HIDEBIT_ALL) &&
                       player.index != 0;
  if (!visible) {
    Api->ui->documentHide(doc);
    Api->radar->setActive(0);
    clearAvatars();
    return;
  }
  Api->ui->documentShow(doc);
  Api->radar->setActive(1);

  AbcUiElement *square = Api->ui->querySelector(doc, "plate-square");
  AbcUiElement *round = Api->ui->querySelector(doc, "plate-round");
  AbcUiElement *map = Api->ui->querySelector(doc, "map");
  AbcUiElement *north = Api->ui->querySelector(doc, "north");
  AbcUiElement *avatars = Api->ui->querySelector(doc, "avatars");
  if (!square || !round || !map || !north || !avatars)
    return;

  // Old Paint: hud_radar 1 = square plate art, > 1 = round plate art; the map
  // mask follows (old m_pRadarImage->SetMaskTexture).
  const bool roundPlate = cvarFloat(Radar, 1.0f) > 1.0f;
  setProperty(square, "display", roundPlate ? "none" : "block");
  setProperty(round, "display", roundPlate ? "block" : "none");
  Api->radar->setMaskRound(roundPlate ? 1 : 0);

  const float zoom = cvarFloat(RadarZoom, 2.5f);
  Api->radar->setZoom(zoom > 0.0f ? zoom : 2.5f);

  // Map <img>: the external FBO texture through the registered URI; the
  // generation suffix re-resolves after frame-buffer regeneration.
  if (Api->radar->ready()) {
    char uri[64];
    std::snprintf(uri, sizeof(uri), "radarfbo://map?gen=%d",
                  Api->radar->generation());
    if (MapUri != uri) {
      MapUri = uri;
      Api->ui->elementSetAttribute(map, "src", uri);
    }
    setProperty(map, "display", "block");
  } else {
    setProperty(map, "display", "none");
  }

  // Old north indicator: rides the plate rim, angle = local entity yaw
  // (curstate.angles, not view angles).
  const float density = Api->ui->scale();
  const float plateSize = (Scaled ? 300.0f : 100.0f) * density;
  const float center = plateSize / 2.0f;
  AbcUiEntityState localEntity{};
  localEntity.structSize = sizeof(localEntity);
  Api->players->entity(player.index, &localEntity);
  {
    const float northSize = 5.0f * density; // old Northground 5 design units
    const float len = plateSize - northSize;
    const float rotate =
        localEntity.angles[1] * 3.14159265358979323846f / 180.0f;
    const float hh =
        roundPlate ? (len / 2.0f)
                   : static_cast<float>(
                         std::sqrt(2.0 * static_cast<double>(len) * len) / 2.0);
    const float stx = std::clamp(center + hh * std::cos(rotate), 0.0f, len);
    const float sty = std::clamp(center + hh * std::sin(rotate), 0.0f, len);
    const std::string left = pixelValue(stx);
    const std::string top = pixelValue(sty);
    setProperty(north, "left", left.c_str());
    setProperty(north, "top", top.c_str());
  }

  std::unordered_set<int> used;
  AbcUiPlayerInfo localInfo{};
  localInfo.structSize = sizeof(localInfo);
  Api->players->local(&localInfo);
  if (cvarFloat(RadarAvatar, 1.0f) <= 0.0f || !localInfo.valid) {
    for (auto &[_, image] : AvatarElements)
      setProperty(image, "display", "none");
    return;
  }

  const float avatarSize = cvarFloat(RadarAvatarSize, 20.0f);
  const float avatarHalf = avatarSize / 2.0f;
  // Old comment: "this should match the radar rendering scale" - the FBO
  // ortho spans +/-worldHalf, so the plate scale is plateSize / full span.
  const float worldHalf = ABCUI_RADAR_ORTHO_BASE / zoom;
  const float radarScale = plateSize / (2.0f * worldHalf);
  const float localYaw =
      -localEntity.angles[1] * 3.14159265358979323846f / 180.0f;
  const float cosYaw = std::cos(localYaw);
  const float sinYaw = std::sin(localYaw);

  for (int index = 1; index <= ABCUI_MAX_PLAYERS; index++) {
    AbcUiPlayerInfo info{};
    info.structSize = sizeof(info);
    if (!Api->players->get(index, &info) || !info.valid || info.spectate ||
        info.team != localInfo.team)
      continue;
    AbcUiEntityState entity{};
    entity.structSize = sizeof(entity);
    if (!Api->players->entity(index, &entity) || !entity.valid ||
        entity.messageNum != player.messageNum || !entity.isPlayer ||
        index == player.index)
      continue;

    const float relX = entity.origin[0] - player.origin[0];
    const float relY = entity.origin[1] - player.origin[1];
    const float radarX = -relY;
    const float radarY = relX;
    const float rotatedX = radarX * cosYaw - radarY * sinYaw;
    const float rotatedY = radarX * sinYaw + radarY * cosYaw;
    float screenX = center + rotatedX * radarScale - avatarHalf;
    float screenY = center - rotatedY * radarScale - avatarHalf;
    const float maxPos = std::max(0.0f, plateSize - avatarSize);
    screenX = std::clamp(screenX, 0.0f, maxPos);
    screenY = std::clamp(screenY, 0.0f, maxPos);
    if (roundPlate) {
      const float dx = screenX + avatarHalf - center;
      const float dy = screenY + avatarHalf - center;
      const float distance = std::sqrt(dx * dx + dy * dy);
      const float maxRadius = center - avatarHalf;
      if (distance > maxRadius && distance > 0.0f) {
        const float ratio = maxRadius / distance;
        screenX = center + dx * ratio - avatarHalf;
        screenY = center + dy * ratio - avatarHalf;
      }
    }

    // Old SetPlayer(i+1, 32x32): the avatar texture resolves through the
    // framework avatar service with the cache-bust generation.
    char uri[96];
    std::snprintf(uri, sizeof(uri), "avatar://%llu?gen=%d",
                  static_cast<unsigned long long>(info.steamId64),
                  Api->players->avatarGeneration(info.steamId64));
    used.insert(index);
    AbcUiElement *&image = AvatarElements[index];
    if (!image) {
      image = Api->ui->elementAppendRml(avatars, "<img/>", "avatar");
      if (!image) {
        AvatarElements.erase(index);
        continue;
      }
    }
    std::string &appliedUri = AvatarUris[index];
    if (appliedUri != uri) {
      appliedUri = uri;
      Api->ui->elementSetAttribute(image, "src", uri);
    }
    setProperty(image, "display", "block");
    const std::string left = pixelValue(screenX);
    const std::string top = pixelValue(screenY);
    const std::string width = pixelValue(avatarSize);
    const std::string height = pixelValue(avatarSize);
    setProperty(image, "left", left.c_str());
    setProperty(image, "top", top.c_str());
    setProperty(image, "width", width.c_str());
    setProperty(image, "height", height.c_str());
  }
  for (auto itor = AvatarElements.begin(); itor != AvatarElements.end();) {
    if (!used.count(itor->first)) {
      if (itor->second)
        Api->ui->elementRemove(itor->second);
      AvatarUris.erase(itor->first);
      itor = AvatarElements.erase(itor);
    } else {
      ++itor;
    }
  }
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  // The radar plate occupies the top-left corner the weapon chooser also
  // uses; parking it under every default HUD document keeps the chooser (and
  // the scoreboard) readable.
  Panel.zIndex = -1;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = nullptr;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);
}

void onModuleInit() {
  Radar = Api->console->registerCvar(kRadar, "1", ABCUI_CVAR_VALUE);
  RadarZoom = Api->console->registerCvar(kRadarZoom, "2.5", ABCUI_CVAR_VALUE);
  RadarAvatar = Api->console->registerCvar(kRadarAvatar, "1", ABCUI_CVAR_VALUE);
  RadarAvatarSize =
      Api->console->registerCvar(kRadarAvatarSize, "20", ABCUI_CVAR_VALUE);
  // Old ADD_COMMAND("+scale_radar"/"-scale_radar"): hold to enlarge.
  Api->console->registerCommand("+scale_radar", &onScaleRadarDown, nullptr);
  Api->console->registerCommand("-scale_radar", &onScaleRadarUp, nullptr);
}

void onModuleShutdown() {
  // The document dies with the context; stop the world pass and drop the
  // avatar handles (they belonged to the dead document).
  Api->radar->setActive(0);
  AvatarElements.clear();
  AvatarUris.clear();
  MapUri.clear();
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
                                         const AbcUiModuleApi *api,
                                         AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api || !out)
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data ||
      !api->players || !api->radar)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi) ||
      api->players->structSize < sizeof(AbcUiPlayersApi) ||
      api->radar->structSize < sizeof(AbcUiRadarApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.radar";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
