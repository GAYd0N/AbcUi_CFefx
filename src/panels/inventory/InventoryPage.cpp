// panels::inventory implementation - see InventoryPage.h.
#include "InventoryPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

#include <format>
#include <string>

namespace panels::inventory {

namespace {

constexpr const char *kRowClass = "inv-row";
constexpr const char *kSelectedClass = "inv-selected";

std::string localized(const char *token, const char *fallback) {
  std::string out;
  return framework::Localization::translate(out, token) ? out : fallback;
}

// Walk up from the event target to the element carrying the intent; the
// row itself carries data-serial, buttons carry data-action.
Rml::Element *attributeNode(Rml::Element *node, const char *attribute) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute(attribute))
      return it;
    if (it->GetId() == "menuinventory")
      break;
  }
  return nullptr;
}

Rml::Element *findRow(Rml::Element *list, std::uint64_t serial) {
  if (!list)
    return nullptr;
  for (int i = 0; i < list->GetNumChildren(); i++) {
    Rml::Element *row = list->GetChild(i);
    if (row && row->GetAttribute<std::uint64_t>("data-serial", 0) == serial)
      return row;
  }
  return nullptr;
}

} // namespace

InventoryPage::InventoryPage()
    : InteractiveDocument("menuinventory", "abc/abcui/gameui/rml/inventory.rml",
                          false),
      Listener(this) {}

InventoryPage::~InventoryPage() {
  close();
}

void InventoryPage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
  render();
}

void InventoryPage::prepare() {
  // A fresh opening projects even when the model did not move while the
  // window was closed (selection is page-visible state). Focus is taken by
  // present() once the document is actually visible.
  RenderedRevision = static_cast<std::uint64_t>(-1);
  render();
}

void InventoryPage::render() {
  Rml::ElementDocument *doc = document();
  if (!doc || !Model)
    return;
  if (RenderedRevision == Model->revision())
    return;
  RenderedRevision = Model->revision();

  using framework::TextUtil::escapeRml;

  const auto &items = Model->items();
  const std::uint64_t selectedSerial = Model->selectedSerial();
  const services::inventory::Item *selected = Model->selected();

  std::string html;
  html.reserve(4096);
  std::size_t index = 0;
  for (const services::inventory::Item &item : items) {
    const bool selectedRow = item.serial == selectedSerial;
    html += "<div class=\"";
    html += kRowClass;
    if (selectedRow) {
      html += " ";
      html += kSelectedClass;
    }
    html += "\" data-serial=\"";
    html += std::to_string(item.serial);
    html += "\"><span class=\"inv-row-index\">";
    html += std::format("{:02}", ++index);
    html += "</span><span class=\"inv-row-name\">";
    html += escapeRml(item.name.empty()
                          ? localized("Invmenu_NoItemName", "Nameless item")
                          : item.name);
    html += "</span><span class=\"inv-row-mark\">\xe2\x80\xba</span></div>";
  }

  if (Rml::Element *list = element("inv-list")) {
    list->SetInnerRML(html);
    list->SetProperty("display", items.empty() ? "none" : "block");
  }
  if (Rml::Element *empty = element("inv-empty"))
    empty->SetProperty("display", items.empty() ? "block" : "none");
  if (Rml::Element *count = element("inv-count"))
    count->SetInnerRML(std::to_string(items.size()));

  // Detail card: with a selection the name/description and the server's
  // availability flags are projected; without one the card shrinks to the
  // "select an item" prompt. The hidden name/description keep the native
  // no-selection text (screen readers and the fallback projection).
  const bool hasSelection = selected != nullptr;
  if (Rml::Element *body = element("inv-detail-body"))
    body->SetProperty("display", hasSelection ? "block" : "none");
  if (Rml::Element *tags = element("inv-tags"))
    tags->SetProperty("display", hasSelection ? "block" : "none");
  if (Rml::Element *hint = element("inv-select-hint"))
    hint->SetProperty("display", hasSelection ? "none" : "block");

  const std::string noSelection =
      localized("Invmenu_NoItemSelected", "No item selected");
  if (Rml::Element *name = element("inv-name")) {
    if (!selected)
      name->SetInnerRML(escapeRml(noSelection));
    else if (selected->name.empty())
      name->SetInnerRML(
          escapeRml(localized("Invmenu_NoItemName", "Nameless item")));
    else
      name->SetInnerRML(escapeRml(selected->name));
  }
  if (Rml::Element *description = element("inv-desc")) {
    if (!selected)
      description->SetInnerRML(escapeRml(noSelection));
    else if (selected->description.empty())
      description->SetInnerRML(escapeRml(
          localized("Invmenu_NoItemDescription", "No item description")));
    else
      description->SetInnerRML(escapeRml(selected->description));
  }

  if (selected) {
    const auto setTag = [&](const char *id, bool ok, const char *okToken,
                            const char *blockedToken, const char *okFallback,
                            const char *blockedFallback) {
      Rml::Element *tag = element(id);
      if (!tag)
        return;
      tag->SetInnerRML(escapeRml(localized(ok ? okToken : blockedToken,
                                           ok ? okFallback : blockedFallback)));
      tag->SetClass("inv-tag-ok", ok);
      tag->SetClass("inv-tag-blocked", !ok);
    };
    setTag("inv-tag-usable", selected->canActivate, "Invmenu_Usable",
           "Invmenu_NotUsable", "Can use", "Cannot use");
    setTag("inv-tag-droppable", selected->canDrop, "Invmenu_Droppable",
           "Invmenu_NotDroppable", "Can drop", "Cannot drop");
  }

  // Buttons: native UpdateSelection enables Activate/Drop only with a
  // selection and Drop ALL only with items; the canActivate/canDrop flags
  // are a warning tint, not a disable (the native handler still sends the
  // command and lets the server decide).
  setButtonState("inv-activate", selected != nullptr,
                 selected != nullptr && !selected->canActivate);
  setButtonState("inv-drop", selected != nullptr,
                 selected != nullptr && !selected->canDrop);
  setButtonState("inv-dropall", !items.empty(), false);
}

void InventoryPage::setButtonState(const char *id, bool enabled, bool warning) {
  Rml::Element *button = element(id);
  if (!button)
    return;
  // RmlUi's <button> is a plain element: it has no form-control disabled
  // state, and a class rule loses the cascade to the variant classes. Apply
  // the state as local properties and clear them again on the next state.
  if (enabled) {
    button->RemoveAttribute("disabled");
    button->RemoveProperty("background-color");
    button->RemoveProperty("border");
    button->RemoveProperty("color");
    if (warning) {
      button->SetProperty("background-color", "transparent");
      button->SetProperty("border", "1dp var(--abc-danger)");
      button->SetProperty("color", "var(--abc-danger)");
    }
  } else {
    button->SetAttribute("disabled", "");
    button->SetProperty("background-color", "var(--abc-inset)");
    button->SetProperty("border", "1dp var(--abc-line-soft)");
    button->SetProperty("color", "#4f5a62");
  }
  button->SetClass("inv-disabled", !enabled);
  button->SetClass("inv-blocked", warning && enabled);
}

void InventoryPage::refocusDefault() {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  Rml::Element *list = element("inv-list");
  if (Model && Model->selectedSerial() != 0) {
    if (Rml::Element *row = findRow(list, Model->selectedSerial())) {
      row->Focus();
      return;
    }
  }
  if (list)
    list->Focus();
}

bool InventoryPage::handleKey(Rml::Input::KeyIdentifier key) {
  if (!Model)
    return false;
  switch (key) {
  case Rml::Input::KI_UP:
  case Rml::Input::KI_DOWN: {
    if (!Model->moveSelection(key == Rml::Input::KI_UP ? -1 : 1))
      return true; // consumed even at the list ends
    render();
    refocusDefault();
    if (Rml::Element *row =
            findRow(element("inv-list"), Model->selectedSerial()))
      row->ScrollIntoView(false);
    return true;
  }
  case Rml::Input::KI_RETURN:
  case Rml::Input::KI_NUMPADENTER:
    if (Model->selected()) {
      runAction(services::inventory::Action::kActivate);
      return true;
    }
    return false;
  case Rml::Input::KI_DELETE:
    if (Model->selected()) {
      runAction(services::inventory::Action::kDrop);
      return true;
    }
    return false;
  case Rml::Input::KI_ESCAPE: {
    const CloseSink sink = CloseSink_;
    if (sink)
      sink();
    return true;
  }
  default:
    return false;
  }
}

void InventoryPage::runAction(services::inventory::Action action) {
  if (ActionSink_)
    ActionSink_(action);
}

void InventoryPage::selectRow(std::uint64_t serial) {
  if (!Model)
    return;
  Model->select(serial);
  render();
}

void InventoryPage::handleClick(Rml::Event &event) {
  if (!Model)
    return;
  if (Rml::Element *actionNode =
          attributeNode(event.GetTargetElement(), "data-action")) {
    // RmlUi's <button> is a plain element: the disabled attribute has no
    // built-in click veto, so the page's own marker is the veto.
    if (actionNode->IsClassSet("inv-disabled"))
      return;
    const Rml::String action =
        actionNode->GetAttribute<Rml::String>("data-action", "");
    if (action == "inv-close") {
      const CloseSink sink = CloseSink_;
      if (sink)
        sink();
    } else if (action == "inv-activate") {
      runAction(services::inventory::Action::kActivate);
    } else if (action == "inv-drop") {
      runAction(services::inventory::Action::kDrop);
    } else if (action == "inv-dropall") {
      runAction(services::inventory::Action::kDropAll);
    }
    return;
  }
  if (Rml::Element *row =
          attributeNode(event.GetTargetElement(), "data-serial")) {
    selectRow(row->GetAttribute<std::uint64_t>("data-serial", 0));
  }
}

void InventoryPage::PageListener::ProcessEvent(Rml::Event &event) {
  if (!Owner)
    return;
  if (event.GetType() == "click")
    Owner->handleClick(event);
}

} // namespace panels::inventory
