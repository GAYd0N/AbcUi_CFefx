// abcui.itemhighlight - world-item callouts (name markers plus the pickup
// prompt). Moved from src/panels/itemhighlight (old CItemHighLightPanel /
// CItemNamePanel / CItemPickupPanel; ABCEnchance sources
// src/hud/vgui/itemhighlight.*).
//
// D-19 (P7 user directive): the highlight is delivered as pure HUD elements.
// The old model-shell era (TEMPENTITY copies around the item) is not ported;
// one marker <div> per tracked entity under #markers carries the corner
// bracket icon, its aim variant and the tinted name, and #pickup is the
// centred pickup notice - all positioned from the per-frame world projection.
//
// Tracking comes from the hud.addentity channel (the same pass the engine
// sees): the module keeps the entity's last-known origin/model index/effects
// and drops it when the pass stops refreshing it. Entities are tracked by
// index, never by the engine-owned pointer.
//
// Old-quirk ledger (D-17 family, all recorded in D-19):
// - hud_highlight_item_name really gates the name markers (the old code read
//   it only in the both-off early return, so it was dead).
// - hud_highlight_item 0 hides existing markers immediately.
// - the pickup prompt hides when the aim item walks out of the 86u reach.
// - a malformed item list warns and drops the trailing record.
// - entity-index reuse re-assigns the tracked item.
#include "items/ItemList.h"

#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <cstring>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

constexpr const char *kPanelName = "itemhighlight";
constexpr const char *kDocumentPath =
    "mod://abcui.itemhighlight/hud/rml/itemhighlight.rml";

// Old ctor defaults (persisted contract - never rename).
constexpr const char *kItemHighlight = "hud_highlight_item";
constexpr const char *kItemRange = "hud_highlight_item_range";
constexpr const char *kItemName = "hud_highlight_item_name";
constexpr const char *kItemNameFov = "hud_highlight_item_fov";
constexpr const char *kItemAimFov = "hud_highlight_item_aimfov";
constexpr const char *kItemPickup = "hud_highlight_item_pickup";

// Old AddEntity distance floor for the pickup prompt.
constexpr float kPickupReach = 86.0f;
// Old ItemNamePanel.res 45x45 design box.
constexpr float kMarkerBox = 45.0f;
// A tracked entity is dropped when the AddEntity pass has not refreshed it
// for this long (the engine only calls the pass for entities in the PVS).
constexpr double kSeenTimeout = 0.2;

AbcUiCvarHandle ItemHighlight = nullptr;
AbcUiCvarHandle ItemRange = nullptr;
AbcUiCvarHandle ItemName = nullptr;
AbcUiCvarHandle ItemNameFov = nullptr;
AbcUiCvarHandle ItemAimFov = nullptr;
AbcUiCvarHandle ItemPickup = nullptr;

// The item list ships inside the package (hud/ItemHighLightList.txt): a
// whitespace-token stream of 6-field records (model path, localization token,
// R, G, B, A).
constexpr const char *kItemListModule = "abcui.itemhighlight";
constexpr const char *kItemListPath = "hud/ItemHighLightList.txt";
constexpr uint32_t kItemListMaxBytes = 4u * 1024u * 1024u;

struct Item {
  std::string path;
  std::string nameToken;
  int modelIndex = -1;
  unsigned char color[4] = {255, 255, 255, 255};
};
std::vector<Item> Items;
bool ListLoaded = false;
bool ListWarned = false;
bool IndexesFilled = false;

struct TrackedEntity {
  int itemIndex = -1;
  float origin[3] = {};
  int32_t modelIndex = 0;
  int32_t messageNum = 0;
  int32_t effects = 0;
  bool noDraw = false;
  double lastSeen = 0.0;
};
std::unordered_map<int, TrackedEntity> Tracked;

struct Marker {
  AbcUiElement *box = nullptr;
  AbcUiElement *art = nullptr;
  AbcUiElement *artAim = nullptr;
  AbcUiElement *label = nullptr;
  std::string tint;
  std::string text;
  float bounds[4] = {};
  bool boundsValid = false;
};

std::unordered_map<int, Marker> Markers;
bool PromptShown = false;
double ClientTimeNow = 0.0;

// Old cvar callbacks: clamp the range to 0..344 and fold the fovs into
// 0..360. The handles are filled right after registration (the callback only
// runs when the engine sets the cvar later).
struct CvarWatch {
  AbcUiCvarHandle handle = nullptr;
};
CvarWatch RangeWatch;
CvarWatch NameFovWatch;
CvarWatch AimFovWatch;

void clampRange(void *userdata) {
  const auto *watch = static_cast<const CvarWatch *>(userdata);
  if (!watch->handle)
    return;
  Api->console->setCvarValue(
      watch->handle,
      std::clamp(Api->console->cvarValue(watch->handle, 344.0f), 0.0f, 344.0f));
}

void foldFov(AbcUiCvarHandle handle, float fallback) {
  if (!handle)
    return;
  Api->console->setCvarValue(
      handle, std::fmod(Api->console->cvarValue(handle, fallback), 360.0f));
}

void foldNameFov(void *userdata) {
  const auto *watch = static_cast<const CvarWatch *>(userdata);
  foldFov(watch->handle, 90.0f);
}

void foldAimFov(void *userdata) {
  const auto *watch = static_cast<const CvarWatch *>(userdata);
  foldFov(watch->handle, 20.0f);
}

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

int cvarInt(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

void setVisible(bool visible) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

void localPlayer(AbcUiPlayerState &state) {
  state = AbcUiPlayerState{};
  state.structSize = sizeof(state);
  Api->data->playerState(&state);
}

void hudState(AbcUiHudState &state) {
  state = AbcUiHudState{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
}

std::string translate(const char *token, const char *fallback) {
  char out[512];
  return Api->ui->translate(token, out, sizeof(out)) ? std::string(out)
                                                     : std::string(fallback);
}

// Measure-then-copy over the bounded C API (server text is untrusted).
std::string escapeText(const char *text) {
  if (!text || !*text)
    return std::string();
  const int needed = Api->ui->escapeRml(text, nullptr, 0);
  if (needed <= 0)
    return std::string();
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->escapeRml(text, out.data(), static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

std::string spriteUri(const char *file) {
  return std::string("mod://abcui.itemhighlight/hud/img/itemhighlight/") + file;
}

// Old CItemHighLightPanel::LoadItemList. D-19: a trailing partial record
// warns and is dropped (the old code SYS_ERROR'd on it).
void loadItemList() {
  if (ListLoaded || ListWarned)
    return;
  ListWarned = true;
  AbcUiFileView *view = nullptr;
  const std::string uri =
      std::string("mod://") + kItemListModule + "/" + kItemListPath;
  Api->vfs->open(uri.c_str(), &view);
  if (!view) {
    Api->core->warn("itemhighlight: no item list file %s", kItemListPath);
    return;
  }
  ListLoaded = true;
  const char *data = static_cast<const char *>(Api->vfs->viewData(view));
  const uint32_t size = Api->vfs->viewSize(view);
  std::string content;
  if (data && size > 0 && size <= kItemListMaxBytes)
    content.assign(data, size);
  std::vector<itemlist::Entry> parsed;
  Api->vfs->viewFree(view);
  if (content.empty())
    return;

  const bool complete = itemlist::parse(content, parsed);
  for (itemlist::Entry &entry : parsed) {
    Item item;
    item.path = std::move(entry.path);
    item.nameToken = std::move(entry.nameToken);
    std::memcpy(item.color, entry.color, sizeof(item.color));
    Items.push_back(std::move(item));
  }
  if (!complete)
    Api->core->warn("itemhighlight: %s has a trailing partial record (dropped)",
                    kItemListPath);
  Api->core->print("itemhighlight: %d item(s) on the highlight list",
                   static_cast<int>(Items.size()));
}

// Old AddEntity lazy fill: one EV_FindModelIndex per list entry, per map.
void fillModelIndexes() {
  for (Item &item : Items) {
    if (item.path.empty() || item.path[0] == '*')
      continue; // brush models can't be highlight items
    item.modelIndex = Api->data->modelIndex(item.path.c_str());
  }
  IndexesFilled = true;
}

const Item *itemAt(int index) {
  if (index < 0 || index >= static_cast<int>(Items.size()))
    return nullptr;
  return &Items[index];
}

// Localized display name; D-19: an unknown token falls back to the raw token.
std::string displayName(const Item &item) {
  char out[512];
  if (Api->ui->translate(item.nameToken.c_str(), out, sizeof(out)))
    return out;
  return item.nameToken;
}

// ------------------------------------------------------------- markers ----

void removeMarker(int entindex) {
  const auto itor = Markers.find(entindex);
  if (itor == Markers.end())
    return;
  if (itor->second.box)
    Api->ui->elementRemove(itor->second.box);
  Markers.erase(itor);
}

void clearMarkers() {
  for (auto &[_, marker] : Markers) {
    if (marker.box)
      Api->ui->elementRemove(marker.box);
  }
  Markers.clear();
}

void setProperty(AbcUiElement *element, const char *name, const char *value) {
  if (element)
    Api->ui->elementSetProperty(element, name, value);
}

void setElementText(AbcUiElement *element, const std::string &text) {
  if (element)
    Api->ui->elementSetInnerRML(element, text.c_str());
}

Marker *updateMarker(int entindex, int itemIndex, float ratio,
                     const float screen[2]) {
  const Item *item = itemAt(itemIndex);
  AbcUiDocument *doc = panelDocument();
  if (!item || !doc)
    return nullptr;

  Marker &marker = Markers[entindex];
  if (!marker.box) {
    AbcUiElement *container = Api->ui->querySelector(doc, "markers");
    if (!container)
      return nullptr;
    // One box per tracked entity: bracket icon, its aim variant, the label.
    AbcUiElement *box = Api->ui->elementAppendRml(
        container,
        "<img class=\"art\"/><img class=\"art\"/><div class=\"label\"></div>",
        "marker");
    if (!box)
      return nullptr;
    marker.box = box;
    marker.art = Api->ui->elementChild(box, 0);
    marker.artAim = Api->ui->elementChild(box, 1);
    marker.label = Api->ui->elementChild(box, 2);
    const std::string artUri = spriteUri("marker.png");
    const std::string aimUri = spriteUri("marker_aim.png");
    if (marker.art)
      Api->ui->elementSetAttribute(marker.art, "src", artUri.c_str());
    if (marker.artAim)
      Api->ui->elementSetAttribute(marker.artAim, "src", aimUri.c_str());
    setProperty(marker.artAim, "display", "none");
  }

  // Old per-item tint (list RGBA) on both the icon and the label.
  char tint[64];
  Api->ui->formatRgba(item->color[0], item->color[1], item->color[2],
                      item->color[3], tint, sizeof(tint));
  if (marker.tint != tint) {
    marker.tint = tint;
    setProperty(marker.art, "image-color", tint);
    setProperty(marker.label, "color", tint);
  }
  const std::string text = escapeText(displayName(*item).c_str());
  if (marker.text != text) {
    marker.text = text;
    setElementText(marker.label, text);
  }

  // Old Update: icon box scales as 45 * ratio (ratio = the old
  // distance/range clamp - the icon grows with distance by design). The
  // label's line-height rides the live box size so the name stays centred.
  const float density = Api->ui->scale();
  const float size = kMarkerBox * density * ratio;
  const float half = size / 2.0f;
  const float bounds[4] = {screen[0] - half, screen[1] - half, size, size};
  if (!marker.boundsValid ||
      std::memcmp(marker.bounds, bounds, sizeof(bounds)) != 0) {
    marker.boundsValid = true;
    std::memcpy(marker.bounds, bounds, sizeof(bounds));
    const std::string left = std::format("{:g}px", bounds[0]);
    const std::string top = std::format("{:g}px", bounds[1]);
    const std::string width = std::format("{:g}px", bounds[2]);
    setProperty(marker.box, "width", width.c_str());
    setProperty(marker.box, "height", width.c_str());
    setProperty(marker.box, "left", left.c_str());
    setProperty(marker.box, "top", top.c_str());
    setProperty(marker.box, "display", "block");
    setProperty(marker.label, "line-height", width.c_str());
  }

  // Default (non-aim) state each frame - the aim pass re-flags the target.
  setProperty(marker.art, "display", "block");
  setProperty(marker.artAim, "display", "none");
  return &marker;
}

// Old CItemPickupPanel::UpdateName: template + USE binding + item name.
void updatePromptText(const char *nameToken) {
  AbcUiElement *text = query("pickup-text");
  if (!text)
    return;
  // Old GetTemplateText + UpdateName: the localized template carries the
  // %%USE_BINDKEY%% / %%PICKUP_ITEM%% markers.
  const std::string templ =
      translate("GameUI_ABC_ItemPickupNotice", "[%%USE_BINDKEY%%] pick up "
                                               "%%PICKUP_ITEM%%");
  char binding[64];
  const int length = Api->console->keyBinding("use", binding, sizeof(binding));
  std::string key = length > 0 ? std::string(binding) : std::string();
  for (char &c : key)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (key.empty())
    key = "USE"; // D-19: old code skipped the whole update on no binding
  // Unknown tokens fall back to the raw token (D-19 fallback).
  std::string name;
  char buffer[512];
  if (nameToken && *nameToken &&
      Api->ui->translate(nameToken, buffer, sizeof(buffer)))
    name = buffer;
  else
    name = nameToken ? nameToken : "";

  const auto replaceAll = [](std::string &target, const char *from,
                             const std::string &to) {
    const size_t fromLen = strlen(from);
    size_t at = 0;
    while ((at = target.find(from, at)) != std::string::npos) {
      target.replace(at, fromLen, to);
      at += to.size();
    }
  };
  std::string result = templ;
  replaceAll(result, "%%USE_BINDKEY%%", key);
  replaceAll(result, "%%PICKUP_ITEM%%", name);
  setElementText(text, escapeText(result.c_str()));
}

// ---- event sinks ----

void onAddEntity(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiAddEntityEvent *>(payload);
  if (cvarInt(ItemHighlight, 1) <= 0)
    return;
  loadItemList();
  // Old gate chain: studio model, not an attachment, visible.
  if (event->modelType != ABCUI_MODEL_STUDIO ||
      event->moveType == ABCUI_MOVETYPE_FOLLOW ||
      (event->effects & ABCUI_EF_NODRAW) != 0)
    return;
  if (!IndexesFilled)
    fillModelIndexes();
  int itemIndex = -1;
  for (size_t i = 0; i < Items.size(); i++) {
    if (Items[i].modelIndex > 0 && Items[i].modelIndex == event->modelIndex) {
      itemIndex = static_cast<int>(i);
      break;
    }
  }
  if (itemIndex < 0)
    return;
  // D-19: assign (the old emplace kept a stale record when an entindex was
  // reused for a different item).
  TrackedEntity &tracked = Tracked[event->index];
  tracked.itemIndex = itemIndex;
  for (int i = 0; i < 3; i++)
    tracked.origin[i] = event->origin[i];
  tracked.modelIndex = event->modelIndex;
  tracked.messageNum = event->messageNum;
  tracked.effects = event->effects;
  tracked.noDraw = (event->effects & ABCUI_EF_NODRAW) != 0;
  tracked.lastSeen = ClientTimeNow;
}

// ---- panel callbacks ----

void panelReset(void *) {
  clearMarkers();
  // Old Reset: clear the stored entities + re-arm the model index fill.
  Tracked.clear();
  IndexesFilled = false;
  PromptShown = false;
  setProperty(query("pickup"), "display", "none");
  setVisible(false);
}

void panelOnRedraw(void *, double frameTime) {
  ClientTimeNow += frameTime;
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;

  // Old gate: hud_highlight_item off stops everything (D-19: this also hides
  // already-tracked markers, which the old code kept painting).
  const bool enabled = cvarInt(ItemHighlight, 1) > 0;
  const bool nameEnabled = cvarInt(ItemName, 1) > 0;
  const bool pickupEnabled = cvarInt(ItemPickup, 1) > 0;
  AbcUiPlayerState player{};
  localPlayer(player);
  AbcUiHudState hud{};
  hudState(hud);
  if (!enabled || player.index == 0 || (hud.hideBits & ABCUI_HIDEBIT_ALL)) {
    setVisible(false);
    setProperty(query("pickup"), "display", "none");
    PromptShown = false;
    return;
  }

  // View forward + eye offset (old OnThink prelude).
  const float range = cvarFloat(ItemRange, 344.0f);
  const float nameFov = cvarFloat(ItemNameFov, 90.0f);
  const float aimFov = cvarFloat(ItemAimFov, 20.0f);
  const float needDot = std::cos(nameFov * 3.14159265f / 180.0f);
  const float needAimDot = std::cos(aimFov * 3.14159265f / 180.0f);

  const float pitch = player.angles[0] * 3.14159265358979323846f / 180.0f;
  const float yaw = player.angles[1] * 3.14159265358979323846f / 180.0f;
  const float view[3] = {std::cos(pitch) * std::cos(yaw),
                         std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
  float viewOfs[3] = {};
  Api->data->viewHeight(viewOfs);

  float maxDot = 0.0f;
  int maxEntIndex = 0;
  int maxItemIndex = -1;

  // Old OnThink sweep: validate, range-check, project, update markers. The
  // entity state is the last hud.addentity snapshot for the index.
  for (auto itor = Tracked.begin(); itor != Tracked.end();) {
    const int entindex = itor->first;
    TrackedEntity &tracked = itor->second;
    const Item *trackedItem = itemAt(tracked.itemIndex);
    const bool stale = ClientTimeNow - tracked.lastSeen > kSeenTimeout;
    if (!trackedItem || stale || tracked.noDraw ||
        tracked.messageNum != player.messageNum ||
        tracked.modelIndex != trackedItem->modelIndex) {
      removeMarker(entindex);
      itor = Tracked.erase(itor);
      continue;
    }
    const float rel[3] = {tracked.origin[0] - player.origin[0] - viewOfs[0],
                          tracked.origin[1] - player.origin[1] - viewOfs[1],
                          tracked.origin[2] - player.origin[2] - viewOfs[2]};
    const float length =
        std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
    if (length >= range) {
      removeMarker(entindex);
      itor = Tracked.erase(itor);
      continue;
    }
    const float dot =
        length > 0.0f
            ? (rel[0] * view[0] + rel[1] * view[1] + rel[2] * view[2]) / length
            : 0.0f;
    if (dot > maxDot) {
      maxDot = dot;
      maxEntIndex = entindex;
      maxItemIndex = tracked.itemIndex;
    }
    if (nameEnabled && dot >= needDot) {
      // Old ratio = clamp(length / range, 0.5, 1) - verbatim.
      const float ratio =
          std::clamp(range > 0.0f ? length / range : 1.0f, 0.5f, 1.0f);
      float screen[2] = {};
      if (Api->ui->project(tracked.origin, screen) == 0)
        updateMarker(entindex, tracked.itemIndex, ratio, screen);
      else
        removeMarker(entindex); // behind the view: no mirrored ghost label
    } else {
      removeMarker(entindex);
    }
    ++itor;
  }

  // Aim pass (old maxdot block): the pickup prompt + the aim marker state.
  const Item *aimItem = itemAt(maxItemIndex);
  bool promptVisible = false;
  if (aimItem && maxEntIndex != 0 && maxDot >= needAimDot && pickupEnabled) {
    // D-19: the prompt also needs the 86u reach every frame.
    const auto itor = Tracked.find(maxEntIndex);
    if (itor != Tracked.end()) {
      const float rel[3] = {itor->second.origin[0] - player.origin[0],
                            itor->second.origin[1] - player.origin[1],
                            itor->second.origin[2] - player.origin[2]};
      const float length =
          std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
      if (length <= kPickupReach) {
        updatePromptText(aimItem->nameToken.c_str());
        promptVisible = true;
        // Aim state on the target's marker (its own look-at pass already ran
        // this frame; maxdot >= aimdot >= namedot).
        const auto marker = Markers.find(maxEntIndex);
        if (marker != Markers.end()) {
          setProperty(marker->second.art, "display", "none");
          setProperty(marker->second.artAim, "display", "block");
        }
      }
    }
  }
  AbcUiElement *prompt = query("pickup");
  if (promptVisible) {
    setProperty(prompt, "display", "block");
    PromptShown = true;
  } else if (PromptShown) {
    setProperty(prompt, "display", "none");
    PromptShown = false;
  }

  setVisible(true);
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = nullptr;
  Panel.onData = nullptr;
  Panel.onRedraw = &panelOnRedraw;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_HUD_ADD_ENTITY, 0, &onAddEntity,
                         nullptr);
}

void onModuleInit() {
  ItemHighlight =
      Api->console->registerCvar(kItemHighlight, "1", ABCUI_CVAR_VALUE);
  // The clamped/folded values keep their old cvar-callback semantics; the
  // watch callback receives this module's handle holder.
  ItemRange = Api->console->registerCvarWatch(
      kItemRange, "344", ABCUI_CVAR_VALUE, &clampRange, &RangeWatch);
  ItemName = Api->console->registerCvar(kItemName, "1", ABCUI_CVAR_VALUE);
  ItemNameFov = Api->console->registerCvarWatch(
      kItemNameFov, "90", ABCUI_CVAR_VALUE, &foldNameFov, &NameFovWatch);
  ItemAimFov = Api->console->registerCvarWatch(
      kItemAimFov, "20", ABCUI_CVAR_VALUE, &foldAimFov, &AimFovWatch);
  ItemPickup = Api->console->registerCvar(kItemPickup, "1", ABCUI_CVAR_VALUE);
  RangeWatch.handle = ItemRange;
  NameFovWatch.handle = ItemNameFov;
  AimFovWatch.handle = ItemAimFov;
}

void onModuleShutdown() {
  // The document (and every marker) dies with the context.
  clearMarkers();
  Tracked.clear();
  PromptShown = false;
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
      !api->events || !api->ui || !api->console || !api->data || !api->vfs)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi) ||
      api->vfs->structSize < sizeof(AbcUiVfsApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.itemhighlight";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
