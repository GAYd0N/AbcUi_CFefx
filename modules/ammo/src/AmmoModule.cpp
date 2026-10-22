// abcui.ammo - the ammo readout and the three pickup stacks. Moved from
// src/panels/{ammobar,ammostack,weaponstack,itemstack} (old CAmmoPanel,
// CAmmoStackPanel, CWeaponStackPanel, CItemStackPanel; ABCEnchance sources
// src/hud/vgui/ammo*).
//
// The ammo bar is the HL2-inspired pool readout (large clip + small reserve
// per ammo type); the stacks append pickup rows that fade in, expire and fade
// out. Stack documents stay permanently visible - an empty transparent
// container draws nothing.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;

constexpr const char *kBarPanel = "ammobar";
constexpr const char *kAmmoStackPanel = "ammostack";
constexpr const char *kWeaponStackPanel = "weaponstack";
constexpr const char *kItemStackPanel = "itemstack";

// Old CAmmoPanel::WEAPONSTATE / ITEM_FLAG bits used by RefreshAmmo.
constexpr int32_t kStateValid = 1 << 0;
constexpr int32_t kStateUzi = 1 << 2;
constexpr int32_t kUziAkimboFlag = 32;
// Primary / secondary ammo type pools (ammobar.rml #ammo1 / #ammo2).
constexpr const char *kPoolIds[2] = {"ammo1", "ammo2"};

AbcUiPanelDesc BarPanel = {};
AbcUiPanelDesc AmmoStackDesc = {};
AbcUiPanelDesc WeaponStackDesc = {};
AbcUiPanelDesc ItemStackDesc = {};

// ------------------------------------------------------------- ammo bar ----

bool BarWeaponKnown = false;
AbcUiWeaponInfo BarWeapon = {};
std::string PoolRml[2];

AbcUiDocument *barDocument() {
  return Api->ui->panelDocument(kBarPanel);
}

void setElementProperty(const char *id, const char *name, const char *value) {
  AbcUiDocument *doc = barDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetProperty(element, name, value);
}

void setBarVisible(bool visible) {
  AbcUiDocument *doc = barDocument();
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

void applyHideToken() {
  // Old hudhideCallback: ShowPanel(!HIDEWEAPONS) while suited; the
  // Viewport-wide HIDEALL bit hides the ammo bar with everything else.
  // refreshAmmo re-hides when the weapon carries no ammo types.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  const bool visible = state.suit &&
                       !(state.hideBits & ABCUI_HIDEBIT_WEAPONS) &&
                       !(state.hideBits & ABCUI_HIDEBIT_ALL) &&
                       !state.spectate && BarWeaponKnown;
  setBarVisible(visible);
}

void setPoolVisible(int pool, bool visible) {
  setElementProperty(kPoolIds[pool], "display", visible ? "block" : "none");
  if (visible)
    return;
  AbcUiDocument *doc = barDocument();
  if (!doc)
    return;
  if (AbcUiElement *container = Api->ui->querySelector(doc, kPoolIds[pool]))
    Api->ui->elementSetInnerRML(container, "");
  PoolRml[pool].clear();
}

std::string spriteUri(const AbcUiWeaponSprite &sprite) {
  char uri[256];
  std::snprintf(uri, sizeof(uri), "spricon://%s/%d,%d,%d,%d", sprite.atlas,
                sprite.rect[0], sprite.rect[1], sprite.rect[2], sprite.rect[3]);
  return uri;
}

void updatePool(int pool, const AbcUiWeaponSprite &sprite,
                const std::string &clip, const std::string &reserve) {
  AbcUiDocument *doc = barDocument();
  if (!doc)
    return;
  AbcUiElement *container = Api->ui->querySelector(doc, kPoolIds[pool]);
  if (!container)
    return;
  // One inline line: sprite + large clip + optional small reserve. Being
  // inline, every part shares the font baseline (ammobar.rcss note).
  std::string rml;
  if (sprite.valid) {
    rml = "<img class=\"icon\" src=\"" + spriteUri(sprite) + "\"/>";
  } else {
    // Keep the sprite slot (the CSS box is 20dp regardless of the source) so
    // a weapon without a usable ammo sprite cannot shift the line box.
    rml = "<img class=\"icon\" style=\"visibility:hidden\"/>";
  }
  // Plain type: the pool spans are the readout (ammobar.rcss draws the 1px
  // outline). The clip span carries no class - the pool styles it.
  rml += "<span>" + clip + "</span>";
  if (!reserve.empty())
    rml += "<span class=\"reserve\">" + reserve + "</span>";
  // AmmoX fires on every ammo change; skip the re-parse when the line is
  // already correct (same sprite, same numbers).
  if (rml == PoolRml[pool])
    return;
  PoolRml[pool] = std::move(rml);
  Api->ui->elementSetInnerRML(container, PoolRml[pool].c_str());
}

void refreshAmmo() {
  if (!BarWeaponKnown) {
    applyHideToken();
    return;
  }
  applyHideToken();
  if (!(BarWeapon.state & kStateValid))
    return;

  const bool pool1 = BarWeapon.ammoType > 0;
  const bool pool2 = BarWeapon.ammo2Type > 0;
  setPoolVisible(0, pool1);
  setPoolVisible(1, pool2);

  // Pool 1: the old akimbo case folds both magazines into the clip value
  // ("clip2xclip"); the reserve stays the shared ammo pool.
  if (pool1) {
    const int ammoLeft = Api->weapons->countAmmo(BarWeapon.ammoType);
    std::string clip;
    std::string reserve;
    if (BarWeapon.clip >= 0) {
      if ((BarWeapon.state & kStateUzi) && (BarWeapon.flags & kUziAkimboFlag) &&
          BarWeapon.clip2 >= 0) {
        clip = std::to_string(BarWeapon.clip2) + "x" +
               std::to_string(BarWeapon.clip);
      } else {
        clip = std::to_string(BarWeapon.clip);
      }
      reserve = "/" + std::to_string(ammoLeft);
    } else {
      clip = std::to_string(ammoLeft);
    }
    updatePool(0, BarWeapon.ammo, clip, reserve);
  }

  if (pool2) {
    const int ammoLeft2 = Api->weapons->countAmmo(BarWeapon.ammo2Type);
    std::string clip;
    std::string reserve;
    if (BarWeapon.clip2 >= 0) {
      clip = std::to_string(BarWeapon.clip2);
      reserve = "/" + std::to_string(ammoLeft2);
    } else {
      clip = std::to_string(ammoLeft2);
    }
    updatePool(1, BarWeapon.ammo2, clip, reserve);
  }

  // The divider only means something between two live pools.
  setElementProperty("pool-sep", "display",
                     (pool1 && pool2) ? "block" : "none");

  if (!pool1 && !pool2)
    setBarVisible(false);
}

// -------------------------------------------------------------- stacks ----

struct StackItem {
  AbcUiElement *element = nullptr;
  double expireTime = 0.0;
  double fadeStartTime = -1.0;
  bool fadeInPending = true;
  int weaponId = 0;
  std::string iconUri; // last applied weapon sprite, so a reload is cheap
};

struct StackState {
  const char *panelName = nullptr;
  const char *documentPath = nullptr;
  double clientTimeNow = 0.0;
  std::vector<StackItem> items;
  // Old CBaseStackPanel ApplySettings values, pinned per .res.
  double keepTime = 5.0;
  double fadeIn = 0.1;
  double fadeOut = 0.3;
  int maxItems = 10;
};

StackState AmmoStack;
StackState WeaponStack;
StackState ItemStack;

AbcUiDocument *stackDocument(const StackState &stack) {
  return Api->ui->panelDocument(stack.panelName);
}

AbcUiElement *stackContainer(const StackState &stack) {
  AbcUiDocument *doc = stackDocument(stack);
  return doc ? Api->ui->querySelector(doc, "stack") : nullptr;
}

void setStackDocumentVisible(const StackState &stack, bool visible) {
  AbcUiDocument *doc = stackDocument(stack);
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

AbcUiElement *appendStackItem(StackState &stack, const std::string &rml) {
  AbcUiElement *container = stackContainer(stack);
  if (!container)
    return nullptr;
  AbcUiElement *item =
      Api->ui->elementAppendRml(container, rml.c_str(), "item");
  if (!item)
    return nullptr;
  // Start the old Show() state: alpha 0, the fade-in runs on the next style
  // computation (transition to opacity 1 in tickItems).
  Api->ui->elementSetProperty(item, "opacity", "0");

  StackItem entry;
  entry.element = item;
  entry.expireTime = stack.clientTimeNow + stack.keepTime;
  stack.items.push_back(entry);

  while (static_cast<int>(stack.items.size()) > std::max(1, stack.maxItems)) {
    if (stack.items.front().element)
      Api->ui->elementRemove(stack.items.front().element);
    stack.items.erase(stack.items.begin());
  }
  return item;
}

void clearStackItems(StackState &stack) {
  for (StackItem &item : stack.items) {
    if (item.element)
      Api->ui->elementRemove(item.element);
  }
  stack.items.clear();
}

void tickStack(StackState &stack, double frameTime) {
  stack.clientTimeNow += frameTime;
  AbcUiDocument *doc = stackDocument(stack);
  if (!doc)
    return;
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  if (state.hideBits & ABCUI_HIDEBIT_ALL)
    Api->ui->documentHide(doc);
  else if (!stack.items.empty() && !Api->ui->documentVisible(doc))
    Api->ui->documentShow(doc);

  if (stack.items.empty())
    return;
  for (auto itor = stack.items.begin(); itor != stack.items.end();) {
    StackItem &item = *itor;
    if (!item.element) {
      itor = stack.items.erase(itor);
      continue;
    }
    if (item.fadeInPending) {
      item.fadeInPending = false;
      char transition[64];
      std::snprintf(transition, sizeof(transition), "opacity %.2fs linear-in",
                    stack.fadeIn);
      Api->ui->elementSetProperty(item.element, "transition", transition);
      Api->ui->elementSetProperty(item.element, "opacity", "1");
    }
    if (item.fadeStartTime < 0.0 && stack.clientTimeNow >= item.expireTime) {
      item.fadeStartTime = stack.clientTimeNow;
      char transition[64];
      std::snprintf(transition, sizeof(transition), "opacity %.2fs linear-in",
                    stack.fadeOut);
      Api->ui->elementSetProperty(item.element, "transition", transition);
      Api->ui->elementSetProperty(item.element, "opacity", "0");
    }
    if (item.fadeStartTime >= 0.0 &&
        stack.clientTimeNow >= item.fadeStartTime + stack.fadeOut) {
      Api->ui->elementRemove(item.element);
      itor = stack.items.erase(itor);
      continue;
    }
    ++itor;
  }
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

// ---------------------------------------------------------- stack feeds ----

void addAmmoPickup(int index, int count) {
  AbcUiWeaponSprite sprite{};
  // Old AddAmmoPickup: icon from any weapon carrying the ammo type.
  if (!Api->weapons->ammoIcon(index, &sprite))
    return;
  // Count: ink-only additive glyphs (no black backing), the same readout ink
  // as the score and the flashlight percentage; a value outside the glyph set
  // falls back to plain type.
  const std::string countText = std::to_string(count);
  char ink[256];
  const std::string countInk =
      Api->ui->inkMarkup(countText.c_str(), nullptr, ink, sizeof(ink))
          ? std::string(ink)
          : countText;
  const std::string item = "<img class=\"icon\" src=\"" + spriteUri(sprite) +
                           "\"/><div class=\"count font-default\">" + countInk +
                           "</div>";
  appendStackItem(AmmoStack, item);
}

void addWeaponPickup(int id) {
  AbcUiWeaponInfo weapon{};
  if (!Api->weapons->getById(id, &weapon) || !weapon.inactive.valid)
    return;
  const std::string uri = spriteUri(weapon.inactive);
  AbcUiElement *item =
      appendStackItem(WeaponStack, "<img class=\"icon\" src=\"" + uri + "\"/>");
  if (!item)
    return;
  // The tint/icon refresh keys off the id kept next to the element (old
  // CWeaponStackItem::m_iWeapon).
  WeaponStack.items.back().weaponId = id;
  WeaponStack.items.back().iconUri = uri;
}

void addItemPickup(const char *name) {
  // Old g_EventItemPickup handler: unknown hud.txt names are skipped.
  char atlas[128];
  int32_t rect[4];
  if (!Api->ui->hudSprite(name, atlas, sizeof(atlas), rect))
    return;
  // The name is server data; the hud.txt lookup is the real gate, but the
  // markup boundary still escapes it (a future lookup change must not turn
  // this into a DOM injection point).
  const std::string item =
      "<img class=\"icon\" src=\"spr://" + escapeText(name) + "\"/>";
  appendStackItem(ItemStack, item);
}

// Old CWeaponStackItem::CheckExpire/ForceReload per tick: icon follows a
// sprite reload (src rebuilt from live data) and the empty tint follows the
// weapon's ammo state.
void updateWeaponStack() {
  for (StackItem &item : WeaponStack.items) {
    if (!item.element || item.weaponId == 0)
      continue;
    AbcUiWeaponInfo weapon{};
    if (!Api->weapons->getById(item.weaponId, &weapon))
      continue;
    if (weapon.inactive.valid) {
      const std::string uri = spriteUri(weapon.inactive);
      if (uri != item.iconUri) {
        item.iconUri = uri;
        Api->ui->elementSetInnerRML(
            item.element,
            ("<img class=\"icon\" src=\"" + uri + "\"/>").c_str());
      }
    }
    Api->ui->elementSetClass(item.element, "empty", weapon.hasAmmo ? 0 : 1);
  }
}

// ---- event sinks ----

void onCurWeapon(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiCurWeaponEvent *>(payload);
  if (event->state <= 0)
    return;
  // Old g_EventCurWeapon handler: weapon lookup may miss (server sent no
  // WeaponList yet) - RefreshAmmo then hides the bar.
  BarWeaponKnown = Api->weapons->getById(event->id, &BarWeapon) != 0;
  if (BarWeaponKnown) {
    BarWeapon.clip = event->clip;
    BarWeapon.clip2 = event->clip2;
    BarWeapon.state = event->state;
  }
  refreshAmmo();
}

void onAmmoX(void *, const void *) {
  refreshAmmo();
}

void onSuitChanged(void *, const void *) {
  applyHideToken();
  refreshAmmo();
}

void onHideToken(void *, const void *) {
  applyHideToken();
}

void onSpectate(void *, const void *) {
  applyHideToken();
}

void onWeapPickup(void *, const void *payload) {
  addWeaponPickup(static_cast<const AbcUiWeaponIdEvent *>(payload)->id);
}

void onAmmoPickup(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiAmmoEvent *>(payload);
  addAmmoPickup(event->index, event->count);
}

void onItemPickup(void *, const void *payload) {
  addItemPickup(static_cast<const AbcUiItemPickupEvent *>(payload)->name);
}

// ---- panel callbacks ----

void barDocumentReady(void *) {
  BarWeaponKnown = false;
  refreshAmmo();
}

void barReset(void *) {
  // Old Reset: hide + drop the weapon reference.
  AbcUiDocument *doc = barDocument();
  if (doc)
    Api->ui->documentHide(doc);
  BarWeaponKnown = false;
  for (std::string &rml : PoolRml)
    rml.clear();
}

void ammoStackDocumentReady(void *) {
  // The container draws nothing while empty; keep the document shown.
  setStackDocumentVisible(AmmoStack, true);
}

void ammoStackOnData(void *, double frameTime) {
  tickStack(AmmoStack, frameTime);
}

void ammoStackReset(void *) {
  clearStackItems(AmmoStack);
}

void weaponStackDocumentReady(void *) {
  setStackDocumentVisible(WeaponStack, true);
}

void weaponStackOnData(void *, double frameTime) {
  tickStack(WeaponStack, frameTime);
  updateWeaponStack();
}

void weaponStackReset(void *) {
  clearStackItems(WeaponStack);
}

void itemStackDocumentReady(void *) {
  setStackDocumentVisible(ItemStack, true);
}

void itemStackOnData(void *, double frameTime) {
  tickStack(ItemStack, frameTime);
}

void itemStackReset(void *) {
  clearStackItems(ItemStack);
}

// ---- lifecycle ----

void registerPanel(AbcUiPanelDesc &desc, const char *name, const char *doc,
                   void (*ready)(void *), void (*onData)(void *, double),
                   void (*reset)(void *)) {
  desc.structSize = sizeof(desc);
  desc.name = name;
  desc.documentPath = doc;
  desc.zIndex = 0;
  desc.userdata = nullptr;
  desc.onDocumentReady = ready;
  desc.onData = onData;
  desc.onRedraw = nullptr;
  desc.reset = reset;
  Api->panels->registerPanel(&desc);
}

void onModuleLoad() {
  AmmoStack.panelName = kAmmoStackPanel;
  AmmoStack.maxItems = 200 / 20; // tall 200 / item_tall 20
  WeaponStack.panelName = kWeaponStackPanel;
  WeaponStack.maxItems = 280 / 30; // tall 280 / item_tall 30
  ItemStack.panelName = kItemStackPanel;
  ItemStack.maxItems = 195 / 30; // tall 195 / item_tall 30

  registerPanel(BarPanel, kBarPanel, "mod://abcui.ammo/hud/rml/ammobar.rml",
                &barDocumentReady, nullptr, &barReset);
  registerPanel(AmmoStackDesc, kAmmoStackPanel,
                "mod://abcui.ammo/hud/rml/ammostack.rml",
                &ammoStackDocumentReady, &ammoStackOnData, &ammoStackReset);
  registerPanel(WeaponStackDesc, kWeaponStackPanel,
                "mod://abcui.ammo/hud/rml/weaponstack.rml",
                &weaponStackDocumentReady, &weaponStackOnData,
                &weaponStackReset);
  registerPanel(ItemStackDesc, kItemStackPanel,
                "mod://abcui.ammo/hud/rml/itemstack.rml",
                &itemStackDocumentReady, &itemStackOnData, &itemStackReset);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_CUR_WEAPON, 0, &onCurWeapon,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_AMMO_X, 0, &onAmmoX, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SUIT, 0, &onSuitChanged, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_HIDE_TOKEN, 0, &onHideToken,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SPECTATE, 0, &onSpectate, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_WEAP_PICKUP, 0, &onWeapPickup,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_AMMO_PICKUP, 0, &onAmmoPickup,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_ITEM_PICKUP, 0, &onItemPickup,
                         nullptr);
}

void onModuleShutdown() {
  // The documents die with the context; the element handles must not survive
  // into the next cycle.
  AmmoStack.items.clear();
  WeaponStack.items.clear();
  ItemStack.items.clear();
  BarWeaponKnown = false;
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
      !api->events || !api->ui || !api->console || !api->data || !api->weapons)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->weapons->structSize < sizeof(AbcUiWeaponsApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.ammo";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = nullptr;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
