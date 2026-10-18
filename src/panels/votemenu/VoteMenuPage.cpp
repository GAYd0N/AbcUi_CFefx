#include "VoteMenuPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

#include <cstdio>

namespace panels::votemenu {

namespace {

constexpr const char *kActionKind = "vm-kind";
constexpr const char *kActionFilter = "vm-filter";
constexpr const char *kActionSelect = "vm-select";
constexpr const char *kActionSubmit = "vm-submit";
constexpr const char *kActionCancel = "vm-cancel";

std::string resolve(const char *token) {
  if (!token || !*token)
    return {};
  if (*token != '#')
    return token;
  std::string out;
  if (framework::Localization::translate(out, token + 1))
    return out;
  return token;
}

std::string resolve(const std::string &token) {
  return resolve(token.c_str());
}

Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

const char *startToken(services::votemenu::Kind kind) {
  switch (kind) {
  case services::votemenu::Kind::kBan:
    return "#VoteMenu_StartBan";
  case services::votemenu::Kind::kKick:
    return "#VoteMenu_StartKick";
  case services::votemenu::Kind::kKill:
    return "#VoteMenu_StartKill";
  case services::votemenu::Kind::kMap:
    return "#VoteMenu_StartMap";
  }
  return "#VoteMenu_Start";
}

const char *filterToken(services::votemenu::Kind kind) {
  return kind == services::votemenu::Kind::kMap ? "#VoteMenu_FilterMapHint"
                                                : "#VoteMenu_FilterPlayerHint";
}

const char *emptyToken(services::votemenu::Kind kind) {
  return kind == services::votemenu::Kind::kMap ? "#VoteMenu_NoMaps"
                                                : "#VoteMenu_NoPlayers";
}

} // namespace

VoteMenuPage::VoteMenuPage()
    : InteractiveDocument("menuvotemenu", "abc/abcui/gameui/rml/votemenu.rml",
                          true),
      Listener(this) {}

VoteMenuPage::~VoteMenuPage() {
  close();
}

void VoteMenuPage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
}

Rml::Element *VoteMenuPage::node(const char *id) const {
  Rml::ElementDocument *doc = document();
  return doc ? doc->GetElementById(id) : nullptr;
}

void VoteMenuPage::prepare() {
  RenderedRevision = static_cast<std::size_t>(-1);
  if (Model)
    Model->prepare();
  setFilterText("");
  showHint(nullptr);
  render();
}

void VoteMenuPage::showHint(const char *text) {
  Rml::Element *hint = node("vm-hint");
  if (!hint)
    return;
  const std::string line = text ? resolve(text) : std::string();
  hint->SetInnerRML(framework::TextUtil::escapeRml(line));
  hint->SetProperty("display", line.empty() ? "none" : "block");
}

void VoteMenuPage::setFilterText(const char *text) {
  if (auto *input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
          const_cast<Rml::Element *>(node("vm-filter"))))
    input->SetValue(text ? text : "");
}

void VoteMenuPage::render() {
  Rml::ElementDocument *doc = document();
  if (!doc || !Model)
    return;
  if (RenderedRevision == Model->revision())
    return;
  RenderedRevision = Model->revision();

  using framework::TextUtil::escapeRml;
  using framework::TextUtil::escapeRmlAttribute;
  using services::votemenu::Kind;

  Rml::Element *panel = doc->GetElementById("vm-panel");
  if (panel)
    panel->SetAttribute("data-kind", services::votemenu::kindId(Model->kind()));
  for (Kind kind : {Kind::kBan, Kind::kKick, Kind::kKill, Kind::kMap}) {
    Rml::Element *tab = doc->GetElementById(
        (std::string("vm-tab-") + services::votemenu::kindId(kind)).c_str());
    if (!tab)
      continue;
    const bool active = Model->kind() == kind;
    tab->SetClass("vm-active", active);
    tab->SetAttribute("aria-pressed", active ? "true" : "false");
  }

  Rml::Element *filter = doc->GetElementById("vm-filter");
  if (filter)
    filter->SetAttribute("placeholder", resolve(filterToken(Model->kind())));

  Rml::Element *head = doc->GetElementById("vm-list-head");
  if (head)
    head->SetProperty("display",
                      Model->kind() == Kind::kMap ? "none" : "block");

  Rml::Element *list = doc->GetElementById("vm-list");
  if (!list)
    return;
  std::string rows;
  const auto *selectedCandidate = Model->selected();
  if (Model->visible().empty()) {
    rows = "<div class=\"vm-empty\">";
    rows += escapeRml(resolve(emptyToken(Model->kind())));
    rows += "</div>";
  } else {
    std::size_t index = 0;
    for (const auto &candidate : Model->visible()) {
      const bool selected = selectedCandidate == &candidate;
      rows += "<div class=\"vm-row";
      if (selected)
        rows += " vm-selected";
      rows += "\" data-action=\"vm-select\" data-index=\"";
      rows += std::to_string(index++);
      rows += "\">";
      if (Model->kind() == Kind::kMap) {
        rows += "<span class=\"vm-map-name\">";
        rows += escapeRml(candidate.name);
        rows += "</span>";
      } else {
        char slot[8];
        _snprintf_s(slot, sizeof(slot), _TRUNCATE, "%02d", candidate.slot);
        rows += "<span class=\"vm-slot\">";
        rows += slot;
        rows += "</span><span class=\"vm-name\">";
        rows += escapeRml(candidate.name);
        rows += "</span><span class=\"vm-real\">";
        rows +=
            escapeRml(candidate.realName.empty() ? "-" : candidate.realName);
        rows += "</span><span class=\"vm-id\">";
        rows += escapeRml(candidate.steamId.empty() ? "-" : candidate.steamId);
        rows += "</span>";
      }
      rows += "</div>";
    }
  }
  list->SetInnerRML(rows);

  Rml::Element *count = doc->GetElementById("vm-count");
  if (count) {
    char text[32];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%d",
                static_cast<int>(Model->visible().size()));
    count->SetInnerRML(text);
  }

  Rml::Element *selected = doc->GetElementById("vm-selected");
  const auto *candidate = selectedCandidate;
  if (selected)
    selected->SetInnerRML(escapeRml(
        candidate ? candidate->name : resolve("#VoteMenu_NoneSelected")));

  if (Rml::Element *submit = doc->GetElementById("vm-submit")) {
    submit->SetInnerRML(escapeRml(resolve(startToken(Model->kind()))));
    if (candidate)
      submit->RemoveAttribute("disabled");
    else
      submit->SetAttribute("disabled", "disabled");
  }
}

void VoteMenuPage::refocusDefault() {
  if (Rml::Element *filter = node("vm-filter")) {
    filter->Focus();
    return;
  }
  if (Rml::Element *cancel = node("vm-cancel"))
    cancel->Focus();
}

void VoteMenuPage::handleClick(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action.empty())
    return;

  if (action == kActionKind) {
    services::votemenu::Kind kind{};
    if (!Model ||
        !services::votemenu::parseKind(
            entry->GetAttribute<Rml::String>("data-kind", "").c_str(), kind))
      return;
    Model->setKind(kind);
    setFilterText("");
    showHint(nullptr);
    render();
    return;
  }
  if (action == kActionFilter) {
    if (!Model)
      return;
    std::string query;
    if (auto *input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
            node("vm-filter")))
      query = input->GetValue();
    Model->setFilter(std::move(query));
    showHint(nullptr);
    render();
    return;
  }
  if (action == kActionSelect) {
    if (!Model)
      return;
    const int index = entry->GetAttribute<int>("data-index", -1);
    if (index < 0 || static_cast<std::size_t>(index) >= Model->visible().size())
      return;
    Model->select(static_cast<std::size_t>(index));
    showHint(nullptr);
    render();
    return;
  }
  if (action == kActionSubmit && Model && Model->selected() == nullptr) {
    showHint("#VoteMenu_NoSelection");
    return;
  }
  if (action != kActionSubmit && action != kActionCancel)
    return;
  const ActionSink callback = Sink;
  if (callback)
    callback(action.c_str());
}

void VoteMenuPage::PageListener::ProcessEvent(Rml::Event &event) {
  Owner->handleClick(event);
}

} // namespace panels::votemenu
