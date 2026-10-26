// abcui.weaponchoose - the Ecco-style weapon selection bar. Moved from
// src/panels/weaponchoose (old CWeaponChoosePanel, ABCEnchance sources:
// src/hud/vgui/weaponchoose.*).
//
// Slot icons across the top; owned weapons under each slot; the selected slot
// expands and the bucket frame marks the highlight. Selection rides the
// weapons api's selectSlot (slot keys / invnext / invprev through the
// command bus, TextMenu-open gated here); +attack confirms the highlighted
// pick (the old vgui mouse-press confirm - normal play has no vgui cursor, so
// the attack key is the honest equivalent; D-17 note).
//
// Layout numbers from WeaponChoosePanel.res (slot icons 15x15, active items
// 60x20, inactive 20x20, 1dp gaps; close_time 5); scheme WeaponChoose.* colors
// (ActiveColor = HUD accent amber, Inactive = HUD accent @125, empty Red
// variants, ammo bar 80,80,80,125 / 255,255,0,125, bucket 255 255 0 255).
//
// hud_fastswitch never shows the bar (the command already went out); the
// hud_rainbow_menu cycle rides the rebuild (old PerformLayout counter).
#include <abcui/events.h>
#include "../../common/Text.h"
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

constexpr const char *kPanelName = "weaponchoose";
constexpr const char *kDocumentPath =
    "mod://abcui.weaponchoose/hud/rml/weaponchoose.rml";

// Old ctor default (persisted contract - never rename).
constexpr const char *kRainbowMenu = "hud_rainbow_menu";

constexpr float kSlotIcon = 15.0f;
constexpr float kItemActiveW = 60.0f;
constexpr float kItemActiveH = 20.0f;
constexpr float kItemInactiveW = 20.0f;
constexpr float kItemInactiveH = 20.0f;
constexpr float kGap = 1.0f;
constexpr double kCloseTime = 5.0;

AbcUiCvarHandle RainbowMenu = nullptr;

double CloseAtTime = 0.0;
double ClientTimeNow = 0.0;
bool Visible = false;
int RainbowCounter = 0;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

void setVisible(bool visible) {
  if (visible == Visible)
    return;
  Visible = visible;
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

// Old RGBtoHSV/HSVtoRGB pair, hue-shifted - the rainbow tint cycle.
void rainbowShift(int counter, unsigned char &r, unsigned char &g,
                  unsigned char &b) {
  float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
  const float mx = std::max({rf, gf, bf});
  const float mn = std::min({rf, gf, bf});
  float h = 0.0f;
  const float v = mx;
  const float s = mx > 0.0f ? (mx - mn) / mx : 0.0f;
  if (mx > mn) {
    if (mx == rf)
      h = (gf - bf) / (mx - mn) + (gf < bf ? 6.0f : 0.0f);
    else if (mx == gf)
      h = (bf - rf) / (mx - mn) + 2.0f;
    else
      h = (rf - gf) / (mx - mn) + 4.0f;
    h *= 60.0f;
  }
  h = static_cast<float>(std::fmod(h + counter * 20.0f, 360.0f));
  const float c = v * s;
  const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  const float m = v - c;
  float r2 = 0, g2 = 0, b2 = 0;
  if (h < 60) {
    r2 = c;
    g2 = x;
  } else if (h < 120) {
    r2 = x;
    g2 = c;
  } else if (h < 180) {
    g2 = c;
    b2 = x;
  } else if (h < 240) {
    g2 = x;
    b2 = c;
  } else if (h < 300) {
    r2 = x;
    b2 = c;
  } else {
    r2 = c;
    b2 = x;
  }
  r = static_cast<unsigned char>((r2 + m) * 255.0f);
  g = static_cast<unsigned char>((g2 + m) * 255.0f);
  b = static_cast<unsigned char>((b2 + m) * 255.0f);
}

bool getWeapon(int slot, int slotPos, AbcUiWeaponInfo &out) {
  out = AbcUiWeaponInfo{};
  return Api->weapons->get(slot, slotPos, &out) != 0;
}

bool selectedWeapon(AbcUiWeaponInfo &out) {
  out = AbcUiWeaponInfo{};
  return Api->weapons->selected(&out) != 0;
}

bool shouldDraw() {
  // Old ShouldDraw gate chain, fed from the hud state snapshot: spectate,
  // hide-all/hide-weapons token, HEV suit, alive, and hud_fastswitch (the
  // weapon command already went out, the bar never shows).
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  if (state.spectate)
    return false;
  if (state.hideBits & (ABCUI_HIDEBIT_ALL | ABCUI_HIDEBIT_WEAPONS))
    return false;
  if (!state.suit)
    return false;
  if (state.health <= 0)
    return false;
  if (Api->weapons->fastSwitch())
    return false;
  return true;
}

void setContentSize(float width, float height) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  AbcUiElement *content = Api->ui->querySelector(doc, "content");
  if (!content)
    return;
  const std::string widthValue = std::format("{:g}dp", width);
  Api->ui->elementSetProperty(content, "width", widthValue.c_str());
  const std::string heightValue = std::format("{:g}dp", height);
  Api->ui->elementSetProperty(content, "height", heightValue.c_str());
}

// Rebuilds the bar DOM from the weapon registry (old PerformLayout); keeps the
// panel open on selection changes.
void rebuild() {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  AbcUiElement *content = Api->ui->querySelector(doc, "content");
  if (!content)
    return;

  // Owned weapons grouped by slot (old InsertWeapon slot lists; the registry
  // also carries the full WeaponList - ownership filters it).
  std::vector<AbcUiWeaponInfo> registry(static_cast<size_t>(
      std::max(0, Api->weapons->list(nullptr, 0, sizeof(AbcUiWeaponInfo)))));
  if (!registry.empty()) {
    for (auto &weapon : registry)
      weapon.structSize = sizeof(weapon);
    const int32_t total = Api->weapons->list(
        registry.data(), static_cast<uint32_t>(registry.size()),
        sizeof(AbcUiWeaponInfo));
    if (total >= 0 && static_cast<size_t>(total) < registry.size())
      registry.resize(static_cast<size_t>(total));
  }

  std::vector<AbcUiWeaponInfo> bySlot[ABCUI_WEAPON_SLOT_COUNT];
  for (const AbcUiWeaponInfo &weapon : registry) {
    if (weapon.slot < 0 || weapon.slot >= ABCUI_WEAPON_SLOT_COUNT)
      continue;
    if (!weapon.owned)
      continue;
    bySlot[weapon.slot].push_back(weapon);
  }
  for (auto &list : bySlot) {
    std::sort(list.begin(), list.end(),
              [](const AbcUiWeaponInfo &a, const AbcUiWeaponInfo &b) {
                return a.slotPos < b.slotPos;
              });
  }

  AbcUiWeaponInfo selected{};
  const bool hasSelected = selectedWeapon(selected);
  const int activeSlot = hasSelected ? selected.slot : -1;

  std::string rml;
  rml.reserve(4096);
  float x = 0.0f;
  float maxHeight = kSlotIcon;
  RainbowCounter = 0;
  // Highlighted item bounds, recorded in the loop below (old
  // m_pHandledWeapon GetBounds - the bucket rides them, y included).
  float selX = 0.0f, selY = 0.0f, selW = 0.0f, selH = 0.0f;
  bool selFound = false;
  std::string buffer;

  // Old item bars: abw=w/10, abx=w/20, abg=w/30, abh=h/10; fill rides the
  // reserve count over max (scheme AmmoBar colors, empty = Red).
  const auto appendAmmoBars = [&](const AbcUiWeaponInfo &weapon, float w,
                                  float h) {
    const float abw = w / 10.0f;
    const float abx = w / 20.0f;
    const float abg = w / 30.0f;
    const float abh = h / 10.0f;
    const int ammoTypes[2] = {weapon.ammoType, weapon.ammo2Type};
    const int maxes[2] = {weapon.max1, weapon.max2};
    for (int bar = 0; bar < 2; bar++) {
      if (ammoTypes[bar] < 0)
        continue;
      const int count = Api->weapons->countAmmo(ammoTypes[bar]);
      const float ratio =
          maxes[bar] > 0
              ? std::clamp(static_cast<float>(count) / maxes[bar], 0.0f, 1.0f)
              : 1.0f;
      const float bx = abx + bar * (abw + abg);
      buffer = std::format(
          "<div class='bar-bg' style='left:{:.1f}dp; top:0dp; "
          "width:{:.1f}dp; height:{:.1f}dp;'></div>"
          "<div class='bar-fill' style='left:{:.1f}dp; top:0dp; "
          "width:{:.1f}dp; height:{:.1f}dp; "
          "background-color: {};'></div>",
          bx, abw, abh, bx, abw * ratio, abh,
          count > 0 ? "rgba(255, 255, 0, 125)" : "rgba(255, 0, 0, 255)");
      rml += buffer;
    }
  };

  for (int slot = 0; slot < ABCUI_WEAPON_SLOT_COUNT; slot++) {
    float y = 0.0f;
    // Slot icon row (old Slot1..Slot9, Slot0 order with the matching
    // slotN_icon assets; the icons tint by scheme SlotColor). Additive
    // (modadd://) like the rest of the HUD ink, so the digits glow over the
    // scene instead of sitting on it.
    buffer = std::format(
        "<div class='slot' style='left:{:.0f}dp; top:0dp;'>"
        "<img src='modadd://abcui.weaponchoose/hud/img/slot{}_icon.png' "
        "style='image-color: rgba(255, 176, 0, 160);'/></div>",
        x, (slot + 1) % 10);
    rml += buffer;
    y = kSlotIcon + kGap;

    const bool expanded = slot == activeSlot;
    for (const AbcUiWeaponInfo &weapon : bySlot[slot]) {
      const bool active = hasSelected && weapon.slot == selected.slot &&
                          weapon.slotPos == selected.slotPos;
      const float w = expanded ? kItemActiveW : kItemInactiveW;
      const float h = expanded ? kItemActiveH : kItemInactiveH;

      // Old UpdateColor: ammo state picks the scheme tint, rainbow rotates
      // the hue per item (ActiveColor Default, InactiveColor
      // DefaultTransparent, EmptyColor Red, EmptyInactive 125 0 0 80).
      unsigned char r = 255, g = 176, b = 0, a = 255;
      unsigned char ri = 255, gi = 176, bi = 0, ai = 125;
      if (!weapon.hasAmmo) {
        r = 255, g = 0;
        ri = 125, gi = 0, ai = 80;
      }
      if (RainbowMenu && Api->console->cvarValue(RainbowMenu, 0.0f) > 0.0f) {
        rainbowShift(RainbowCounter, r, g, b);
        rainbowShift(RainbowCounter, ri, gi, bi);
      }

      // Active sprite while highlighted, inactive otherwise; the ammo bar
      // pair rides the item top (old item PerformLayout).
      AbcUiWeaponSprite source = active ? weapon.active : weapon.inactive;
      if (source.status != ABCUI_OK) {
        source.status = ABCUI_OK;
        moduleui::copyCString(
            source.atlas, "mod://abcui.weaponchoose/hud/spr/weapon_error.spr");
        source.rect[0] = 0;
        source.rect[1] = active ? 46 : 0;
        source.rect[2] = 170;
        source.rect[3] = source.rect[1] + 45;
      }
      std::string img;
      if ((source.status == ABCUI_OK)) {
        buffer = std::format(
            "<img src='spricon://{}/{},{},{},{}' style='width:100%; "
            "height:100%; image-color: rgba({}, {}, {}, {});'/>",
            source.atlas, source.rect[0], source.rect[1], source.rect[2],
            source.rect[3], active ? r : ri, active ? g : gi, active ? b : bi,
            active ? a : ai);
        img = buffer;
      }

      buffer =
          std::format("<div class='item{}' style='left:{:.0f}dp; top:{:.0f}dp; "
                      "width:{:.0f}dp; height:{:.0f}dp;'>",
                      active ? " active" : "", x, y, w, h);
      rml += buffer;
      if (active) {
        selX = x;
        selY = y;
        selW = w;
        selH = h;
        selFound = true;
      }
      appendAmmoBars(weapon, w, h);
      rml += img;
      rml += "</div>";
      y += h + kGap;
      RainbowCounter++;
    }
    maxHeight = std::max(maxHeight, y);
    x += (expanded ? kItemActiveW : kItemInactiveW) + kGap;
  }

  // Bucket frame over the highlighted item (old select_icon ImagePanel - the
  // bounds came straight off the item, y included). Missing from the rebuilt
  // list (dropped while shown) hides the frame, like the old ChooseWeapon
  // empty-slot path.
  if (selFound) {
    buffer = std::format(
        "<img id='bucket' "
        "src='mod://abcui.weaponchoose/hud/img/select_icon.png' "
        "style='left:{:.0f}dp; top:{:.0f}dp; width:{:.0f}dp; height:{:.0f}dp; "
        "image-color: rgba(255, 255, 0, 255);'/>",
        selX, selY, selW, selH);
    rml += buffer;
  }

  Api->ui->elementSetInnerRML(content, rml.c_str());
  setContentSize(x, maxHeight);
}

// Old SelectWeapon: the highlighted pick must be usable (ammo or
// SELECTONEMPTY); confirm sends the weapon command + select sound and closes
// the bar. Rejected picks keep the bar open and let the attack through (the
// old m_bSelectBlock armed only on the success path).
bool confirmSelection() {
  AbcUiWeaponInfo weapon{};
  if (!selectedWeapon(weapon) || !Visible || !shouldDraw() || !weapon.owned)
    return false;
  if (Api->weapons->fastSwitch())
    return false;
  if (!(weapon.flags & ABCUI_ITEM_FLAG_SELECT_ON_EMPTY) && !weapon.hasAmmo)
    return false;
  // The name originates in the server's WeaponList message; the registry
  // already rejects non-identifiers, and this second gate keeps the console
  // token safe even if a future producer bypasses that path.
  if (!Api->weapons->validCommandName(weapon.name))
    return false;
  Api->console->serverCommand(weapon.name);
  Api->console->playSound("common/wpn_select.wav", 1.0f);
  setVisible(false);
  return true;
}

// +attack while the bar shows: attempt the confirm, swallow the press only
// when it confirmed (old confirm + BlockAttackOnce pair).
int attackConfirm() {
  if (!Visible)
    return 0;
  return confirmSelection() ? 1 : 0;
}

// ---- event sinks ----

void onChooseWeapon(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiWeaponEvent *>(payload);
  // Old ChooseWeapon: hudon sound on the first open of a selection round,
  // moveselect on each highlight move; the bar closes after close_time
  // without input.
  if (!shouldDraw())
    return;
  AbcUiWeaponInfo weapon{};
  if (event->slot < 0 || !getWeapon(event->slot, event->slotPos, weapon) ||
      !weapon.owned) {
    // Highlight walked off an owned weapon: empty slot - close.
    setVisible(false);
    return;
  }
  const bool firstOpen = !Visible;
  if (firstOpen) {
    Api->console->playSound("common/wpn_hudon.wav", 1.0f);
    setVisible(true);
    rebuild();
  } else {
    Api->console->playSound("common/wpn_moveselect.wav", 1.0f);
    rebuild();
  }
  CloseAtTime = ClientTimeNow + kCloseTime;
}

void onDropAllWeapons(void *, const void *) {
  setVisible(false);
}

void onOwnershipChanged(void *, const void *) {
  if (Visible)
    rebuild();
}

// ---- panel callbacks ----

void panelOnData(void *, double frameTime) {
  ClientTimeNow += frameTime;
  // Old OnThink: the bar closes itself after close_time (re-armed on every
  // ChooseWeapon through the rebuild path).
  if (Visible && CloseAtTime > 0.0 && ClientTimeNow >= CloseAtTime) {
    CloseAtTime = 0.0;
    setVisible(false);
  }
}

void panelReset(void *) {
  Visible = false;
  AbcUiDocument *doc = panelDocument();
  if (doc)
    Api->ui->documentHide(doc);
}

// ---- lifecycle ----

int onSlotCommand(void *userdata, const void *) {
  if (!panelDocument())
    return 0;
  // The old weaponres slot handler ran the selection walk and left the
  // original engine slot command chained; the TextMenu owns the keys while
  // its menu is open (left entirely to the textmenu module then).
  if (Api->commands->textMenuOpen())
    return 0;
  const intptr_t slot = reinterpret_cast<intptr_t>(userdata);
  Api->weapons->selectSlot(static_cast<int>(slot), 1, 0);
  return 0;
}

int onInvNext(void *, const void *) {
  if (!panelDocument())
    return 0;
  Api->weapons->selectSlot(Api->weapons->selectedSlot(), 1, 1);
  return 0;
}

int onInvPrev(void *, const void *) {
  if (!panelDocument())
    return 0;
  Api->weapons->selectSlot(Api->weapons->selectedSlot(), -1, 1);
  return 0;
}

int onAttack(void *, const void *) {
  if (!panelDocument())
    return 0;
  return attackConfirm();
}

int onNativeHudDraw(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNativeHudEvent *>(payload);
  return panelDocument() && (event->element == ABCUI_NATIVE_HUD_AMMO);
}

void onModuleLoad() {
  Api->events->subscribeVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW, 0, &onNativeHudDraw,
                             nullptr);
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = nullptr;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_HUD_CHOOSE_WEAPON, 0, &onChooseWeapon,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_DROP_ALL_WEAPONS, 0,
                         &onDropAllWeapons, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_PICKUP_WEAPON, 0,
                         &onOwnershipChanged, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_DROP_WEAPON, 0, &onOwnershipChanged,
                         nullptr);

  for (intptr_t i = 0; i < ABCUI_WEAPON_SLOT_COUNT; i++) {
    const std::string name = std::format("slot{}", i + 1);
    Api->commands->subscribeCommand(name.c_str(), 0, &onSlotCommand,
                                    reinterpret_cast<void *>(i));
  }
  Api->commands->subscribeCommand("invnext", 0, &onInvNext, nullptr);
  Api->commands->subscribeCommand("invprev", 0, &onInvPrev, nullptr);
  Api->commands->subscribeCommand("+attack", 0, &onAttack, nullptr);
}

void onModuleInit() {
  RainbowMenu = Api->console->registerCvar(kRainbowMenu, "0", ABCUI_CVAR_VALUE);
}

void onModuleShutdown() {
  // The document dies with the context; drop the bar state so a stale Visible
  // cannot swallow the next round's attack.
  Visible = false;
  CloseAtTime = 0.0;
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
      !api->events || !api->ui || !api->console || !api->data ||
      !api->commands || !api->weapons)
    return 0;
  // The ui/console/weapons sections grew for this module; refuse a framework
  // whose tables are shorter than the members used below.
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      !ABCUI_API_HAS_MEMBER(AbcUiWeaponsApi, api->weapons, ammoIcon))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.weaponchoose";
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
