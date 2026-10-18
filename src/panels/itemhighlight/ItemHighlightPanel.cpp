// panels::ItemHighlight implementation - see ItemHighlightPanel.h.
// from: ABCEnchance/src/hud/vgui/itemhighlight.cpp (GBK->UTF-8).
#include <metahook.h>

#include "ItemHighlightPanel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "../../framework/ColorUtil.h"
#include <vector>

#include <com_model.h>
#include <const.h>
#include <event_api.h>

#include <abc/CvarCmd.h>
#include <abc/Console.h>
#include <abc/Math.h>

#include "../../data/HudState.h"
#include "../../framework/Localization.h"
#include "../../framework/Projection.h"
#include "../../framework/ResourceFile.h"
#include "../../framework/TextUtil.h"
#include "../../framework/Theme.h"

namespace panels {

namespace itemhighlight {

// Old ctor defaults (persisted contract - never rename).
cvar_t *ItemHighlight = nullptr;        // hud_highlight_item
cvar_t *ItemHighlightRange = nullptr;   // hud_highlight_item_range
cvar_t *ItemHighlightName = nullptr;    // hud_highlight_item_name
cvar_t *ItemHighlightNameFov = nullptr; // hud_highlight_item_fov
cvar_t *ItemHighlightAimFov = nullptr;  // hud_highlight_item_aimfov
cvar_t *ItemHighlightPickup = nullptr;  // hud_highlight_item_pickup

void registerCvars() {
  ItemHighlight =
      abc::CREATE_CVAR("hud_highlight_item", "1", abc::FCVAR_VALUE, nullptr);
  ItemHighlightRange = abc::CREATE_CVAR(
      "hud_highlight_item_range", "344", abc::FCVAR_VALUE, [](cvar_t *cvar) {
        cvar->value = std::clamp(cvar->value, 0.0f, 344.0f);
      });
  ItemHighlightName = abc::CREATE_CVAR("hud_highlight_item_name", "1",
                                       abc::FCVAR_VALUE, nullptr);
  ItemHighlightNameFov = abc::CREATE_CVAR(
      "hud_highlight_item_fov", "90", abc::FCVAR_VALUE,
      [](cvar_t *cvar) { cvar->value = fmodf(cvar->value, 360.0f); });
  ItemHighlightAimFov = abc::CREATE_CVAR(
      "hud_highlight_item_aimfov", "20", abc::FCVAR_VALUE,
      [](cvar_t *cvar) { cvar->value = fmodf(cvar->value, 360.0f); });
  ItemHighlightPickup = abc::CREATE_CVAR("hud_highlight_item_pickup", "1",
                                         abc::FCVAR_VALUE, nullptr);
}

namespace {

// Old cl_highlight_t (the list record) with the model index resolved
// lazily per map (old m_bHasFilledIndex).
struct Item {
  std::string path;
  std::string nameToken;
  int modelIndex = -1;
  unsigned char color[4] = {255, 255, 255, 255};
};

// Old m_aryHighLightTable + m_dicStoredEntity (entindex -> item index).
std::vector<Item> Items;
std::unordered_map<int, int> Tracked;
bool IndexesFilled = false;
bool ListWarned = false;

// Old ITEM_LIST_PATH ("abcenchance/ItemHighLightList.txt"): migrated to the
// abcui asset root (plan 01 §itemhighlight migration note).
constexpr const char *kItemListPath = "abc/abcui/hud/ItemHighLightList.txt";

// Old CItemHighLightPanel::LoadItemList: whitespace-token stream of 6-field
// records (model path, localization token, R, G, B, A). D-19: a trailing
// partial record warns and is dropped (the old code SYS_ERROR'd on it).
void loadItemList() {
  if (!Items.empty() || ListWarned)
    return;
  ListWarned = true;
  std::vector<unsigned char> bytes;
  if (!framework::ResourceFile::read(kItemListPath, bytes, 4 * 1024 * 1024)) {
    abc::ConsoleWarn("[AbcUi] itemhighlight: no item list file %s\n",
                     kItemListPath);
    return;
  }
  const std::string content(reinterpret_cast<const char *>(bytes.data()),
                            bytes.size());

  // Tokenize on whitespace (the old COM_ParseFile contract).
  std::vector<std::string> tokens;
  size_t at = 0;
  while (at < content.size()) {
    while (at < content.size() && (content[at] == ' ' || content[at] == '\t' ||
                                   content[at] == '\r' || content[at] == '\n'))
      at++;
    const size_t begin = at;
    while (at < content.size() && content[at] != ' ' && content[at] != '\t' &&
           content[at] != '\r' && content[at] != '\n')
      at++;
    if (at > begin)
      tokens.push_back(content.substr(begin, at - begin));
  }
  const size_t records = tokens.size() / 6;
  for (size_t r = 0; r < records; r++) {
    Item item;
    item.path = tokens[r * 6];
    item.nameToken = tokens[r * 6 + 1];
    for (int c = 0; c < 4; c++)
      item.color[c] = static_cast<unsigned char>(
          std::clamp(atoi(tokens[r * 6 + 2 + c].c_str()), 0, 255));
    Items.push_back(std::move(item));
  }
  if (tokens.size() % 6 != 0)
    abc::ConsoleWarn(
        "[AbcUi] itemhighlight: %s has a trailing partial record (dropped)\n",
        kItemListPath);
  abc::ConsolePrint("[AbcUi] itemhighlight: %d item(s) on the highlight list\n",
                    static_cast<int>(Items.size()));
}

// Old AddEntity lazy fill: one EV_FindModelIndex per list entry, per map.
void fillModelIndexes() {
  for (Item &item : Items) {
    if (item.path[0] == '*')
      continue; // brush models can't be highlight items (old list has none)
    item.modelIndex = gEngfuncs.pEventAPI->EV_FindModelIndex(item.path.c_str());
  }
  IndexesFilled = true;
}

} // namespace

void onAddEntity(cl_entity_t *ent, const char *modelname) {
  (void)modelname;
  if (!ItemHighlight || ItemHighlight->value <= 0)
    return;
  loadItemList();
  // Old gate chain: studio model, not an attachment, visible.
  if (!ent || !ent->model || ent->model->type != mod_studio ||
      ent->baseline.movetype == MOVETYPE_FOLLOW)
    return;
  if ((ent->curstate.effects & EF_NODRAW) != 0)
    return;
  if (!IndexesFilled)
    fillModelIndexes();
  int itemIndex = -1;
  for (size_t i = 0; i < Items.size(); i++) {
    if (Items[i].modelIndex > 0 &&
        Items[i].modelIndex == ent->curstate.modelindex) {
      itemIndex = static_cast<int>(i);
      break;
    }
  }
  if (itemIndex < 0)
    return;
  // D-19: assign (old emplace kept a stale record when an entindex was
  // reused for a different item).
  Tracked[ent->index] = itemIndex;
}

#ifndef NDEBUG
int debugTrackedCount() {
  return static_cast<int>(Tracked.size());
}
#endif

} // namespace itemhighlight

namespace {

using itemhighlight::Item;
using itemhighlight::ItemHighlight;
using itemhighlight::ItemHighlightAimFov;
using itemhighlight::ItemHighlightName;
using itemhighlight::ItemHighlightNameFov;
using itemhighlight::ItemHighlightPickup;
using itemhighlight::ItemHighlightRange;

// Old AddEntity distance floor for the pickup prompt.
constexpr float kPickupReach = 86.0f;
// Old ItemNamePanel.res 45x45 design box.
constexpr float kMarkerBox = 45.0f;

const Item *itemAt(int index) {
  if (index < 0 || index >= static_cast<int>(itemhighlight::Items.size()))
    return nullptr;
  return &itemhighlight::Items[index];
}

// Localized display name; D-19: an unknown token falls back to the raw token
// (old code rendered an empty label - invisible breakage).
std::string displayName(const Item &item) {
  std::string text;
  if (framework::Localization::translate(text, item.nameToken))
    return text;
  return item.nameToken;
}

} // namespace

ItemHighlightPanel::ItemHighlightPanel()
    : HudPanel("itemhighlight", "abc/abcui/hud/rml/itemhighlight.rml") {}

void ItemHighlightPanel::onDocumentReady() {}

void ItemHighlightPanel::reset() {
  AppliedDom.clear();
  // Old Reset: clear the stored entities + re-arm the model index fill.
  itemhighlight::Tracked.clear();
  itemhighlight::IndexesFilled = false;
  clearMarkers();
  PromptShown = false;
  if (Document) {
    if (Rml::Element *prompt = Document->GetElementById("pickup"))
      AppliedDom.property(prompt, "display", "none");
  }
  hide();
}

void ItemHighlightPanel::clearMarkers() {
  AppliedDom.clear();
  Rml::Element *container =
      Document ? Document->GetElementById("markers") : nullptr;
  if (container) {
    for (auto &[entindex, marker] : Markers) {
      (void)entindex;
      if (marker.box)
        container->RemoveChild(marker.box);
    }
  }
  Markers.clear();
}

void ItemHighlightPanel::removeMarker(int entindex) {
  const auto itor = Markers.find(entindex);
  if (itor == Markers.end())
    return;
  Rml::Element *container =
      Document ? Document->GetElementById("markers") : nullptr;
  AppliedDom.forget(itor->second.art);
  AppliedDom.forget(itor->second.artAim);
  AppliedDom.forget(itor->second.label);
  AppliedDom.forget(itor->second.box);
  if (container && itor->second.box)
    container->RemoveChild(itor->second.box);
  Markers.erase(itor);
}

ItemHighlightPanel::Marker *
ItemHighlightPanel::updateMarker(int entindex, int itemIndex, float ratio,
                                 const float screen[2]) {
  const Item *item = itemAt(itemIndex);
  if (!item || !Document)
    return nullptr;

  Marker &marker = Markers[entindex];
  if (!marker.box) {
    Rml::Element *container = Document->GetElementById("markers");
    if (!container)
      return nullptr;
    Rml::ElementPtr box = Document->CreateElement("div");
    if (!box)
      return nullptr;
    marker.box = box.get();
    marker.box->SetClass("marker", true);

    Rml::ElementPtr art = Document->CreateElement("img");
    marker.art = art.get();
    marker.art->SetClass("art", true);
    marker.art->SetAttribute("src",
                             Rml::String("../img/itemhighlight/marker.png"));
    marker.box->AppendChild(std::move(art));

    Rml::ElementPtr artAim = Document->CreateElement("img");
    marker.artAim = artAim.get();
    marker.artAim->SetClass("art", true);
    marker.artAim->SetClass("art-aim", true);
    marker.artAim->SetAttribute(
        "src", Rml::String("../img/itemhighlight/marker_aim.png"));
    AppliedDom.property(marker.artAim, "display", "none");
    marker.box->AppendChild(std::move(artAim));

    Rml::ElementPtr label = Document->CreateElement("div");
    marker.label = label.get();
    marker.label->SetClass("label", true);
    marker.box->AppendChild(std::move(label));

    container->AppendChild(std::move(box));
  }

  // Old per-item tint (list RGBA) on both the icon and the label.
  const std::string tint = framework::ColorUtil::formatRgba(
      item->color[0], item->color[1], item->color[2], item->color[3]);
  AppliedDom.property(marker.art, "image-color", tint);
  if (marker.label) {
    AppliedDom.property(marker.label, "color", tint);
    AppliedDom.text(marker.label,
                    framework::TextUtil::escapeRml(displayName(*item)));
  }

  // Old Update: icon box scales as 45 * ratio (ratio = the old
  // distance/range clamp - the icon grows with distance by design). The
  // label's line-height rides the live box size so the name stays centred
  // (block + centered line, the vgui Label port pattern).
  const float density = framework::Theme::scale();
  const float size = kMarkerBox * density * ratio;
  const float half = size / 2.0f;
  AppliedDom.property(marker.box, "width", std::to_string(size) + "px");
  AppliedDom.property(marker.box, "height", std::to_string(size) + "px");
  AppliedDom.property(marker.box, "left",
                      std::to_string(screen[0] - half) + "px");
  AppliedDom.property(marker.box, "top",
                      std::to_string(screen[1] - half) + "px");
  AppliedDom.property(marker.box, "display", "block");
  if (marker.label)
    AppliedDom.property(marker.label, "line-height",
                        std::to_string(size) + "px");

  // Default (non-aim) state each frame - the aim pass re-flags the target.
  AppliedDom.property(marker.art, "display", "block");
  AppliedDom.property(marker.artAim, "display", "none");
  return &marker;
}

void ItemHighlightPanel::updatePromptText(const char *nameToken) {
  Rml::Element *text =
      Document ? Document->GetElementById("pickup-text") : nullptr;
  if (!text)
    return;
  // Old GetTemplateText + UpdateName: the localized template carries the
  // %%USE_BINDKEY%% / %%PICKUP_ITEM%% markers.
  std::string templ;
  if (!framework::Localization::translate(templ, "GameUI_ABC_ItemPickupNotice"))
    templ = "[%USE%] pick up %ITEM%";
  const char *binding = gEngfuncs.Key_LookupBinding("use");
  std::string key = binding ? binding : "";
  for (char &c : key)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (key.empty())
    key = "USE"; // D-19: old code skipped the whole update on no binding
  std::string name;
  if (nameToken && !framework::Localization::translate(name, nameToken))
    name = nameToken;

  const auto replaceAll = [](std::string &target, const char *from,
                             const std::string &to) {
    const size_t fromLen = strlen(from);
    size_t at = 0;
    while ((at = target.find(from, at)) != std::string::npos) {
      target.replace(at, fromLen, to);
      at += to.size();
    }
  };
  replaceAll(templ, "%%USE_BINDKEY%%", key);
  replaceAll(templ, "%%PICKUP_ITEM%%", name);
  AppliedDom.text(text, framework::TextUtil::escapeRml(templ));
}

void ItemHighlightPanel::onData(double frameTime) {
  (void)frameTime;
  // The per-frame body runs in onRedraw (world projection needs the live
  // view matrices; old OnThink ran in the same render phase).
}

void ItemHighlightPanel::onRedraw(double frameTime) {
  (void)frameTime;
  if (!Document)
    return;

  // Old gate: hud_highlight_item off stops everything (D-19: this also
  // hides already-tracked markers, which the old code kept painting).
  const bool enabled = ItemHighlight && ItemHighlight->value > 0;
  const bool nameEnabled = ItemHighlightName && ItemHighlightName->value > 0;
  const bool pickupEnabled =
      ItemHighlightPickup && ItemHighlightPickup->value > 0;
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!enabled || !local || hudstate::isHudHidden(hudstate::kHideAll)) {
    hide();
    Rml::Element *prompt = Document->GetElementById("pickup");
    if (prompt)
      AppliedDom.property(prompt, "display", "none");
    PromptShown = false;
    return;
  }

  // View forward + eye offset (old OnThink prelude).
  const float range = ItemHighlightRange ? ItemHighlightRange->value : 344.0f;
  const float nameFov =
      ItemHighlightNameFov ? ItemHighlightNameFov->value : 90.0f;
  const float aimFov = ItemHighlightAimFov ? ItemHighlightAimFov->value : 20.0f;
  const float needDot = cosf(nameFov * 3.14159265f / 180.0f);
  const float needAimDot = cosf(aimFov * 3.14159265f / 180.0f);

  vec3_t view{};
  gEngfuncs.GetViewAngles(view);
  abc::CMathlib::AngleVectors(view, view, nullptr, nullptr);
  vec3_t viewOfs{};
  gEngfuncs.pEventAPI->EV_LocalPlayerViewheight(viewOfs);

  float maxDot = 0.0f;
  int maxEntIndex = 0;
  int maxItemIndex = -1;

  // Old OnThink sweep: validate, range-check, project, update markers.
  for (auto itor = itemhighlight::Tracked.begin();
       itor != itemhighlight::Tracked.end();) {
    const int entindex = itor->first;
    const int itemIndex = itor->second;
    cl_entity_t *ent = gEngfuncs.GetEntityByIndex(entindex);
    const Item *trackedItem = itemAt(itemIndex);
    if (!ent || !ent->model || !trackedItem ||
        ent->curstate.modelindex != trackedItem->modelIndex ||
        (ent->curstate.effects & EF_NODRAW) ||
        ent->curstate.messagenum != local->curstate.messagenum) {
      removeMarker(entindex);
      itor = itemhighlight::Tracked.erase(itor);
      continue;
    }
    vec3_t rel{};
    for (int i = 0; i < 3; i++)
      rel[i] = ent->curstate.origin[i] - local->curstate.origin[i] - viewOfs[i];
    const float length =
        sqrtf(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
    if (length >= range) {
      removeMarker(entindex);
      itor = itemhighlight::Tracked.erase(itor);
      continue;
    }
    const float dot =
        length > 0.0f
            ? (rel[0] * view[0] + rel[1] * view[1] + rel[2] * view[2]) / length
            : 0.0f;
    if (dot > maxDot) {
      maxDot = dot;
      maxEntIndex = entindex;
      maxItemIndex = itemIndex;
    }
    if (nameEnabled && dot >= needDot) {
      // Old ratio = clamp(length / range, 0.5, 1) - verbatim.
      const float ratio =
          std::clamp(range > 0.0f ? length / range : 1.0f, 0.5f, 1.0f);
      float screen[2] = {};
      framework::projection::worldToScreen(ent->curstate.origin, screen);
      updateMarker(entindex, itemIndex, ratio, screen);
    } else {
      removeMarker(entindex);
    }
    itor++;
  }

  // Aim pass (old maxdot block): the pickup prompt + the aim marker state.
  Rml::Element *prompt = Document->GetElementById("pickup");
  const Item *aimItem = itemAt(maxItemIndex);
  bool promptVisible = false;
  if (aimItem && maxEntIndex && maxDot >= needAimDot && pickupEnabled) {
    // D-19: the prompt also needs the 86u reach every frame (old code
    // left a stale prompt up while aiming beyond reach).
    cl_entity_t *ent = gEngfuncs.GetEntityByIndex(maxEntIndex);
    if (ent) {
      vec3_t rel{};
      for (int i = 0; i < 3; i++)
        rel[i] = ent->curstate.origin[i] - local->curstate.origin[i];
      const float length =
          sqrtf(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
      if (length <= kPickupReach) {
        updatePromptText(aimItem->nameToken.c_str());
        promptVisible = true;
        // Aim state on the target's marker (its own look-at pass
        // already ran this frame; maxdot >= aimdot >= namedot).
        const auto itor = Markers.find(maxEntIndex);
        if (itor != Markers.end() && itor->second.art && itor->second.artAim) {
          itor->second.art->SetProperty("display", "none");
          itor->second.artAim->SetProperty("display", "block");
        }
      }
    }
  }
  if (prompt) {
    if (promptVisible) {
      AppliedDom.property(prompt, "display", "block");
      PromptShown = true;
    } else if (PromptShown) {
      AppliedDom.property(prompt, "display", "none");
      PromptShown = false;
    }
  }

  show();
}

} // namespace panels
