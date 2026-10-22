// The create-local-game page (moved from panels::creategame). A FORM
// projection of the framework's enumerated map catalog plus the server
// options from settings.scr, projected through the sdk creategame section;
// the module validates nothing itself - Apply assembles the fields once
// and the framework-side submit validates, keeps the pending request and
// dispatches the attempt (a refusal comes back as a hint token and the
// latch re-arms, so a corrected form can re-Apply).
//
// The map picker offers exactly the catalog entries (the search narrows,
// the row list mirrors the select one-to-one, so a pick can never name a
// map outside the filtered set). The Apply latch (one report per opening;
// the backend's per-generation latch is the second guard) and the LAN
// toggle stay page-local. Every dynamic string reaches the DOM escaped.
#include "PageShared.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace creategamepage {
namespace {

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
uint64_t ChangeBinding = 0;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;

constexpr const char *kDocumentName = "menucreategame";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/creategame.rml";

constexpr const char *kActionApply = "cg-apply";
constexpr const char *kActionCancel = "cg-cancel";
constexpr const char *kActionMapPick = "cg-map-pick";
constexpr const char *kActionLan = "cg-lan";

// The page's local gestures: the LAN toggle and the Apply latch (one
// report per opening; the backend latch is behind it).
bool Lan = false;
bool Applied = false;
// Fresh-open latch: the form rebuilds once per opening (the catalog and
// the option defaults are stable per world, the fields reset).
bool Prepared = false;

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

void setInnerRml(const char *id, const std::string &rml) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

std::string inputValue(const char *id) {
  AbcUiElement *control = node(id);
  if (!control)
    return {};
  const int needed = Api->ui->elementValue(control, nullptr, 0);
  if (needed <= 0)
    return {};
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->elementValue(control, out.data(), static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

std::string selectedMap() {
  AbcUiElement *select = node("cg-map");
  if (!select || Api->ui->elementSelectSelection(select) < 0)
    return {};
  return inputValue("cg-map");
}

// The players field normalizes to an integer spelling (the shipped
// settings.scr NUMBER semantics; the model still range-checks).
std::string playersText() {
  const std::string raw = inputValue("cg-players");
  return std::to_string(static_cast<int>(std::strtof(raw.c_str(), nullptr)));
}

// Option names become element ids ("cg-option-<name>") in the generated
// markup and in the later id lookups. settings.scr is local data, but a
// quote or space would still break the markup; accept only the identifier
// shape the shipped script uses.
bool validOptionName(const char *name) {
  const size_t size = name ? std::strlen(name) : 0;
  if (size == 0 || size > 64)
    return false;
  for (size_t i = 0; i < size; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-'))
      return false;
  }
  return true;
}

std::string number(float value) {
  char text[48];
  std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
  return text;
}

void showHint(const char *text);

// Fresh projection for a new opening: rebuilds the map options and the
// option rows from the section, resets the fields, clears the hint and
// re-arms the Apply latch.
void prepare() {
  Applied = false;
  Lan = false;

  // Module 1: the map catalog. The select is the page's value store
  // (selectedMap() reads it, row picks write it); the visible picker is
  // the row list.
  const uint32_t count = Api->creategame->mapCount();
  const std::vector<std::string> catalog = [&] {
    std::vector<std::string> names;
    names.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
      names.push_back(Api->creategame->mapAt(static_cast<int32_t>(index)));
    return names;
  }();

  if (AbcUiElement *filter = node("cg-map-filter"))
    Api->ui->elementSetValue(filter, "");

  // Filter to everything (empty query), then reset the typed fields.
  {
    AbcUiElement *select = node("cg-map");
    if (select) {
      Api->ui->elementSelectRemoveAll(select);
      for (const std::string &name : catalog)
        Api->ui->elementSelectAdd(select, escapeText(name.c_str()).c_str(),
                                  name.c_str());
      Api->ui->elementSelectSetSelection(select, catalog.empty() ? -1 : 0);
    }
    std::string rows;
    for (size_t index = 0; index < catalog.size(); ++index) {
      rows += "<div class=\"cg-map-row\" data-action=\"cg-map-pick\" "
              "data-index=\"";
      rows += std::to_string(index);
      rows += "\"><span class=\"cg-map-row-name\">";
      rows += escapeText(catalog[index]);
      rows += "</span></div>";
    }
    setInnerRml("cg-map-list", rows);
    char text[48];
    std::snprintf(text, sizeof(text), "%d / %d",
                  static_cast<int>(catalog.size()),
                  static_cast<int>(catalog.size()));
    setInnerRml("cg-map-count", text);
  }

  Api->ui->elementSetValue(node("cg-players"), "8");
  {
    std::string name = Api->creategame->defaultName();
    if (name.empty())
      name = resolve("#CG_DefaultName");
    Api->ui->elementSetValue(node("cg-name"), name.c_str());
  }
  Api->ui->elementSetValue(node("cg-password"), "");

  // Module 2: the server settings rows (one per option on the 96dp label
  // grid the typed rows use, so every control lines up).
  std::string markup;
  const int32_t optionCount = Api->creategame->optionCount();
  std::vector<std::string> optionNames;
  for (int32_t index = 0; index < optionCount; ++index) {
    AbcUiCgOption option;
    option.structSize = sizeof(option);
    if (!Api->creategame->optionAt(index, &option) ||
        !validOptionName(option.name))
      continue;
    optionNames.push_back(option.name);
    // One row per option on the page's 96dp label grid (the typed rows
    // above use the same column), so every control lines up.
    markup += "<div class=\"cg-row\"><div class=\"cg-row-label\">" +
              escapeText(resolve(option.label)) + "</div>";
    const std::string id = std::string("cg-option-") + option.name;
    if (option.toggle || option.hasChoices)
      markup += "<select class=\"cg-opt-select\" id=\"" + id + "\"></select>";
    else {
      markup += "<input id=\"" + id + "\" class=\"" +
                (option.slider ? "cg-slider cg-opt-slider" : "cg-opt-field") +
                "\" type=\"" + (option.slider ? "range" : "text") +
                "\" value=\"" + number(option.defaultValue) + "\"";
      if (option.slider)
        markup += " min=\"" + number(option.min) + "\" max=\"" +
                  number(option.max) + "\" step=\"1\"";
      markup += "/><span class=\"row-value cg-opt-value\" id=\"" + id +
                "-value\">" + number(option.defaultValue) + "</span>";
    }
    markup += "</div>";
  }
  if (AbcUiElement *host = node("cg-options")) {
    Api->ui->elementSetInnerRML(host, markup.c_str());
    // An empty option set must not leave the block's divider hanging over
    // nothing (the shipped script always fills it, a stripped install may
    // not).
    Api->ui->elementSetProperty(host, "display",
                                markup.empty() ? "none" : "block");
  }
  for (int32_t index = 0; index < optionCount; ++index) {
    AbcUiCgOption option;
    option.structSize = sizeof(option);
    if (!Api->creategame->optionAt(index, &option) ||
        !validOptionName(option.name))
      continue;
    AbcUiElement *select =
        node((std::string("cg-option-") + option.name).c_str());
    if (!select)
      continue;
    int32_t selected = 0;
    if (option.toggle) {
      Api->ui->elementSelectAdd(
          select, escapeText(resolve("#Settings_Off")).c_str(), "0");
      Api->ui->elementSelectAdd(
          select, escapeText(resolve("#Settings_On")).c_str(), "1");
      selected = option.defaultValue > 0 ? 1 : 0;
    } else {
      for (int32_t choice = 0; choice < option.choiceCount; ++choice) {
        AbcUiCgChoice entry;
        entry.structSize = sizeof(entry);
        if (!Api->creategame->choiceAt(index, choice, &entry))
          continue;
        Api->ui->elementSelectAdd(select,
                                  escapeText(resolve(entry.label)).c_str(),
                                  number(entry.value).c_str());
        if (entry.value == option.defaultValue)
          selected = choice;
      }
    }
    Api->ui->elementSelectSetSelection(select, selected);
  }
  setInnerRml("cg-lan", escapeText(resolve("#Settings_Off")));

  showHint(nullptr);
  setInnerRml("cg-players-value", "8");
}

// Marks the row of the select's current option.
void highlightSelectedMap() {
  AbcUiElement *list = node("cg-map-list");
  AbcUiElement *select = node("cg-map");
  if (!list || !select)
    return;
  const int active = Api->ui->elementSelectSelection(select);
  const int32_t count = Api->ui->elementChildCount(list);
  for (int32_t index = 0; index < count; ++index) {
    AbcUiElement *row = Api->ui->elementChild(list, index);
    if (!row)
      continue;
    const bool isActive =
        Api->ui->elementAttributeInt(row, "data-index", -1) == active;
    Api->ui->elementSetClass(row, "cg-map-row-active", isActive ? 1 : 0);
    if (isActive)
      Api->ui->elementScrollIntoView(row, 0);
  }
}

// Rebuilds the picker from the query: the select and the row list mirror
// one-to-one (data-index is the option index).
void filterMaps(const std::string &query) {
  AbcUiElement *select = node("cg-map");
  if (!select)
    return;
  std::string needle;
  for (char c : query)
    needle += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);

  const std::string previous = inputValue("cg-map");
  Api->ui->elementSelectRemoveAll(select);
  int selected = 0;
  int matches = 0;
  std::string rows;
  const uint32_t count = Api->creategame->mapCount();
  for (uint32_t index = 0; index < count; ++index) {
    const char *raw = Api->creategame->mapAt(static_cast<int32_t>(index));
    std::string lowered;
    for (const char c : std::string(raw))
      lowered += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (!needle.empty() && lowered.find(needle) == std::string::npos)
      continue;
    if (previous == raw)
      selected = Api->ui->elementSelectCount(select);
    Api->ui->elementSelectAdd(select, escapeText(raw).c_str(), raw);
    rows += "<div class=\"cg-map-row\" data-action=\"cg-map-pick\" "
            "data-index=\"";
    rows += std::to_string(matches);
    rows += "\"><span class=\"cg-map-row-name\">";
    rows += escapeText(raw);
    rows += "</span></div>";
    ++matches;
  }
  Api->ui->elementSelectSetSelection(
      select, Api->ui->elementSelectCount(select) ? selected : -1);
  setInnerRml("cg-map-list", rows);
  char text[48];
  std::snprintf(text, sizeof(text), "%d / %d", matches,
                static_cast<int>(count));
  setInnerRml("cg-map-count", text);
  highlightSelectedMap();
  if (AbcUiElement *apply = node("cg-apply"))
    Api->ui->elementSetAttribute(
        apply, "disabled",
        Api->ui->elementSelectCount(select) ? nullptr : "disabled");
}

// Inline feedback for a refused request. A shown hint re-arms the Apply
// latch (a rejected form can be corrected; a successful launch stays
// latched).
void showHint(const char *text) {
  if (text && *text)
    Applied = false;
  AbcUiElement *hint = node("cg-hint");
  if (!hint)
    return;
  const std::string resolved = text ? resolve(text) : std::string();
  Api->ui->elementSetInnerRML(hint, escapeText(resolved).c_str());
  Api->ui->elementSetProperty(hint, "display",
                              resolved.empty() ? "none" : "block");
}

// The safe default focus: the search field (typing narrows the list
// immediately, and the row list is one Tab away).
void refocusDefault() {
  if (AbcUiElement *filter = node("cg-map-filter"))
    Api->ui->elementFocus(filter);
}

void apply() {
  if (Applied)
    return;
  AbcUiElement *select = node("cg-map");
  if (!select)
    return;
  std::vector<AbcUiCgField> fields;
  const auto push = [&fields](const char *name, const std::string &value) {
    AbcUiCgField field{};
    field.structSize = sizeof(field);
    std::snprintf(field.name, sizeof(field.name), "%s", name);
    std::snprintf(field.value, sizeof(field.value), "%s", value.c_str());
    fields.push_back(field);
  };
  push("map", selectedMap());
  push("players", playersText());
  push("name", inputValue("cg-name"));
  push("password", inputValue("cg-password"));
  push("lan", Lan ? "1" : "0");
  const int32_t optionCount = Api->creategame->optionCount();
  for (int32_t index = 0; index < optionCount; ++index) {
    AbcUiCgOption option;
    option.structSize = sizeof(option);
    if (!Api->creategame->optionAt(index, &option) ||
        !validOptionName(option.name))
      continue;
    push(option.name,
         inputValue((std::string("cg-option-") + option.name).c_str()));
  }
  Applied = true;
  char hint[128];
  hint[0] = '\0';
  if (!Api->creategame->submit(fields.data(),
                               static_cast<int32_t>(fields.size()), hint,
                               sizeof(hint))) {
    showHint(hint[0] ? hint : "#CG_ErrorMap");
  }
}

int32_t onClick(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click"))
    return 0;
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (!entry || action.empty())
    return 1;
  if (action == kActionMapPick) {
    // A row pick moves the select's selection (the list and the select are
    // index-parallel); the highlight follows. The map choice stays
    // page-local until Apply assembles the request.
    AbcUiElement *select = node("cg-map");
    const int index = Api->ui->elementAttributeInt(entry, "data-index", -1);
    if (select && index >= 0 && index < Api->ui->elementSelectCount(select)) {
      Api->ui->elementSelectSetSelection(select, index);
      highlightSelectedMap();
    }
    return 1;
  }
  if (action == kActionLan) {
    Lan = !Lan;
    Api->ui->elementSetInnerRML(
        entry,
        escapeText(resolve(Lan ? "#Settings_On" : "#Settings_Off")).c_str());
    return 1;
  }
  if (action == kActionApply) {
    apply(); // one report per opening (the backend latch is behind it)
    return 1;
  }
  if (action == kActionCancel) {
    Api->menu->back(); // a plain back with zero engine writes
    return 1;
  }
  return 1;
}

int32_t onChange(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "change"))
    return 0;
  const char *id = Api->ui->elementId(target);
  if (!id)
    return 0;
  if (!std::strcmp(id, "cg-map-filter")) {
    filterMaps(inputValue("cg-map-filter"));
    return 0;
  }
  const std::string valueId = std::string(id) + "-value";
  if (AbcUiElement *value = node(valueId.c_str()))
    Api->ui->elementSetInnerRML(value, escapeText(inputValue(id)).c_str());
  return 0;
}

void present() {
  if (!Page || !Api->creategame)
    return;
  if (Api->documents->visible(Page))
    return;
  // Fresh open: rebuild the form from the section (the fields reset, the
  // latch re-arms); a re-show while visible only shows.
  if (!Prepared) {
    Prepared = true;
    prepare();
  }
  Api->documents->show(Page);
  refocusDefault();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
}

bool wantsPage(const AbcUiMenuState *state) {
  return state && state->up && state->page == ABCUI_MENU_PAGE_CREATE_GAME;
}

void onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!state)
    return;
  if (wantsPage(state))
    present();
  else
    dismiss();
}

void onMenuPaint(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!state || !wantsPage(state) || !Page)
    return;
  if (!Api->documents->visible(Page)) {
    // A fresh opening was requested while the document loaded: prepare now
    // so the first visible frame carries the form.
    Prepared = false;
    return;
  }
}

void onDocumentReady(void *) {
  Prepared = false;
  BoundRoot = Api->documents->root(Page);
  if (!BoundRoot)
    return;
  ClickBinding =
      Api->ui->elementAddListener(BoundRoot, "click", &onClick, nullptr);
  ChangeBinding =
      Api->ui->elementAddListener(BoundRoot, "change", &onChange, nullptr);
}

void onDocumentHide(void *) {
  // A reopened page rebuilds the form from scratch.
  Prepared = false;
  showHint(nullptr);
}

void onDocumentClose(void *) {
  // The DOM is still alive: drop the listeners before it goes (the
  // framework owns the listener objects until they are removed).
  if (BoundRoot && ClickBinding) {
    Api->ui->elementRemoveListener(BoundRoot, "click", ClickBinding);
    ClickBinding = 0;
  }
  if (BoundRoot && ChangeBinding) {
    Api->ui->elementRemoveListener(BoundRoot, "change", ChangeBinding);
    ChangeBinding = 0;
  }
  BoundRoot = nullptr;
  Prepared = false;
}

} // namespace

void load() {
  StateSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_STATE, 0,
                                             &onMenuState, nullptr);
  PaintSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_PAINT, 0,
                                             &onMenuPaint, nullptr);

  AbcUiDocumentDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = kDocumentName;
  desc.path = kDocumentPath;
  desc.flags = ABCUI_DOC_FLAG_MENU | ABCUI_DOC_FLAG_BACKDROP;
  desc.zIndex = 0;
  desc.userdata = nullptr;
  desc.onReady = &onDocumentReady;
  desc.onShow = nullptr;
  desc.onHide = &onDocumentHide;
  desc.onClose = &onDocumentClose;
  Page = Api->documents->create(&desc);
}

void shutdown() {
  // The document dies with the context; forget every borrowed handle.
  Page = nullptr;
  BoundRoot = nullptr;
  ClickBinding = 0;
  ChangeBinding = 0;
  Prepared = false;
  Applied = false;
  Lan = false;
}

void unload() {
  if (StateSubscription) {
    Api->events->unsubscribe(StateSubscription);
    StateSubscription = 0;
  }
  if (PaintSubscription) {
    Api->events->unsubscribe(PaintSubscription);
    PaintSubscription = 0;
  }
  Page = nullptr;
}

} // namespace creategamepage
