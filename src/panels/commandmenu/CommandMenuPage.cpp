#include "CommandMenuPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

#include <utility>

namespace panels::commandmenu {
namespace {

constexpr const char *kActionActivate = "cm-activate";

std::string resolve(const std::string &token) {
  if (token.empty())
    return token;
  std::string out;
  if (token[0] == '#') {
    if (framework::Localization::translate(out, token.substr(1)))
      return out;
    return token;
  }
  // Literal labels are also translated by exact source text. This keeps the
  // JSON readable while allowing the complete shipped menu to be localized;
  // explicit "#token" labels remain available for custom entries.
  if (framework::Localization::translate(out, token))
    return out;
  return token;
}

std::string resolve(const char *token) {
  return token ? resolve(std::string(token)) : std::string();
}

Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

} // namespace

CommandMenuPage::CommandMenuPage()
    : InteractiveDocument("menucommandmenu",
                          "abc/abcui/gameui/rml/commandmenu.rml", false),
      Listener(this) {}

CommandMenuPage::~CommandMenuPage() {
  close();
}

void CommandMenuPage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
  resolveLabels();
}

Rml::Element *CommandMenuPage::node(const char *id) const {
  Rml::ElementDocument *doc = document();
  return doc ? doc->GetElementById(id) : nullptr;
}

void CommandMenuPage::resolveLabels() {
  using framework::TextUtil::escapeRml;
  const auto setText = [this](const char *id, const char *token) {
    if (Rml::Element *element = node(id))
      element->SetInnerRML(escapeRml(resolve(token)));
  };
  setText("cm-title", "#CommandMenu_Title");
}

void CommandMenuPage::prepare(const std::string &mapName) {
  RenderedRevision = static_cast<std::size_t>(-1);
  if (Model) {
    Model->open(mapName);
    render();
  }
}

void CommandMenuPage::render() {
  Rml::ElementDocument *doc = document();
  if (!doc || !Model)
    return;
  if (RenderedRevision == Model->revision())
    return;
  RenderedRevision = Model->revision();

  using framework::TextUtil::escapeRml;

  if (Rml::Element *title = doc->GetElementById("cm-title")) {
    const std::string text = resolve(Model->definition().Title);
    title->SetInnerRML(text.empty() ? resolve("#CommandMenu_Title")
                                    : escapeRml(text));
  }

  Rml::Element *columns = doc->GetElementById("cm-columns");
  if (!columns)
    return;

  std::string html;
  html.reserve(8192);
  const std::size_t count = Model->visibleColumnCount(kVisibleColumns);
  const std::size_t first = Model->firstVisibleColumn(kVisibleColumns);
  if (Rml::Element *shell = doc->GetElementById("cm-shell")) {
    const std::size_t slots = count == 0 ? 1 : count;
    shell->SetClass("cm-cols-2", slots == 2);
    shell->SetClass("cm-cols-3", slots == 3);
  }

  if (count == 0) {
    html += "<div class=\"cm-column\" id=\"cm-column-empty\">";
    html += "<div class=\"cm-empty\">";
    html += escapeRml(resolve("#CommandMenu_Empty"));
    html += "</div></div>";
  } else {
    for (std::size_t column = first; column < first + count; ++column) {
      const auto &items = Model->itemsAt(column);
      const std::size_t selected = Model->selectedAt(column);
      html += "<div class=\"cm-column\" id=\"cm-column-";
      html += std::to_string(column - first);
      html += "\">";
      for (std::size_t index = 0; index < items.size(); ++index) {
        const services::commandmenu::Item &item = *items[index];
        html += "<div id=\"cm-row-";
        html += std::to_string(column);
        html += "-";
        html += std::to_string(index);
        html += "\" class=\"cm-row";
        if (index == selected)
          html += " cm-selected";
        if (item.hasItems())
          html += " cm-branch";
        else
          html += " cm-leaf";
        html += "\" data-action=\"";
        html += kActionActivate;
        html += "\" data-column=\"";
        html += std::to_string(column);
        html += "\" data-index=\"";
        html += std::to_string(index);
        html += "\">";
        html += "<span class=\"cm-key\">";
        html += std::to_string(index + 1);
        html += "</span><span class=\"cm-label\">";
        html += escapeRml(resolve(item.Label));
        html += "</span>";
        if (item.hasItems())
          html += "<span class=\"cm-arrow\">&gt;</span>";
        html += "</div>";
      }
      html += "</div>";
    }
  }
  columns->SetInnerRML(html);
  resolveLabels();
  if (isVisible())
    focusSelected();
}

bool CommandMenuPage::finish(
    const services::commandmenu::ActionResult &result) {
  using Kind = services::commandmenu::ActionResult::Kind;
  switch (result.kind) {
  case Kind::kSubmenu:
    render();
    if (isVisible())
      focusSelected();
    return true;
  case Kind::kCommand: {
    const CommandSink sink = CommandSink_;
    if (sink)
      sink(result.command);
    return true;
  }
  case Kind::kClose: {
    const CloseSink sink = CloseSink_;
    if (sink)
      sink();
    return true;
  }
  case Kind::kNone:
    return false;
  }
  return false;
}

bool CommandMenuPage::handleKey(Rml::Input::KeyIdentifier key) {
  if (!Model)
    return false;

  switch (key) {
  case Rml::Input::KI_UP:
    if (Model->moveSelection(-1))
      render();
    return true;
  case Rml::Input::KI_DOWN:
    if (Model->moveSelection(1))
      render();
    return true;
  case Rml::Input::KI_LEFT:
  case Rml::Input::KI_BACK: {
    const services::commandmenu::ActionResult result = Model->back();
    if (result.kind == services::commandmenu::ActionResult::Kind::kNone) {
      render();
      if (isVisible())
        focusSelected();
      return true;
    }
    return finish(result);
  }
  case Rml::Input::KI_RIGHT:
    return finish(Model->openSelected());
  case Rml::Input::KI_RETURN:
  case Rml::Input::KI_NUMPADENTER:
    return finish(Model->activateSelected());
  case Rml::Input::KI_ESCAPE: {
    // ESC is the single full dismiss gesture; LEFT/BACKSPACE walks one
    // column back so deep menus can be explored without closing.
    const CloseSink sink = CloseSink_;
    if (sink)
      sink();
    return true;
  }
  default:
    break;
  }

  if (key >= Rml::Input::KI_0 && key <= Rml::Input::KI_9)
    return finish(Model->activateShortcut(
        static_cast<char>('0' + (key - Rml::Input::KI_0))));
  if (key >= Rml::Input::KI_NUMPAD0 && key <= Rml::Input::KI_NUMPAD9)
    return finish(Model->activateShortcut(
        static_cast<char>('0' + (key - Rml::Input::KI_NUMPAD0))));
  return false;
}

void CommandMenuPage::handleClick(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry || !Model)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action != kActionActivate)
    return;
  const int column = entry->GetAttribute<int>("data-column", -1);
  const int index = entry->GetAttribute<int>("data-index", -1);
  if (column < 0 || index < 0)
    return;
  finish(Model->activateAt(static_cast<std::size_t>(column),
                           static_cast<std::size_t>(index)));
}

void CommandMenuPage::focusSelected() {
  if (!Model || Model->columnCount() == 0)
    return;
  const std::size_t column = Model->columnCount() - 1;
  const std::size_t index = Model->selectedAt(column);
  std::string id = "cm-row-";
  id += std::to_string(column);
  id += "-";
  id += std::to_string(index);
  if (Rml::Element *row = node(id.c_str()))
    row->Focus();
}

void CommandMenuPage::refocusDefault() {
  if (Model && Model->columnCount() > 0) {
    focusSelected();
    return;
  }
}

void CommandMenuPage::PageListener::ProcessEvent(Rml::Event &event) {
  Owner->handleClick(event);
}

} // namespace panels::commandmenu
