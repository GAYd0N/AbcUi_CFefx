#include "panels/settings/sections/SpraySection.h"

#include "panels/settings/SettingsUi.h"
#include "framework/TextUtil.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdlib>
#include <string>

namespace panels::settings::sections {
using detail::translated;
namespace {

int attributeIndex(Rml::Element *element, const char *name) {
  if (!element)
    return -1;
  const Rml::String text = element->GetAttribute<Rml::String>(name, "");
  if (text.empty())
    return -1;
  return std::atoi(text.c_str());
}

} // namespace

SpraySection::SpraySection() = default;

void SpraySection::open(Rml::ElementDocument *document) {
  BrowseOpen = true;
  Selected.clear();
  Browser.setRoots(services::filedialog::FileBrowser::defaultRoots());
  // Start at the game folder (the process working directory) - the first
  // root - and fall back to the first readable one.
  for (const auto &root : Browser.roots()) {
    if (Browser.open(root.Path))
      break;
  }
  renderBrowser(document);
}

void SpraySection::close(Rml::ElementDocument *document) {
  BrowseOpen = false;
  Selected.clear();
  renderBrowser(document);
}

void SpraySection::handleAction(const std::string &action, Rml::Element *entry,
                                Rml::ElementDocument *document) {
  if (action == "spray-file-open") {
    const int index = attributeIndex(entry, "data-entry");
    const auto &entries = Browser.entries();
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size())
      return;
    const auto &picked = entries[static_cast<std::size_t>(index)];
    if (picked.Directory) {
      Browser.enter(picked.Name);
      Selected.clear();
    } else {
      // A file click only selects; the confirm button starts the maker.
      Selected = picked.Name;
    }
  } else if (action == "spray-file-up") {
    Browser.openParent();
    Selected.clear();
  } else if (action == "spray-file-root") {
    const int index = attributeIndex(entry, "data-root");
    const auto &roots = Browser.roots();
    if (index >= 0 && static_cast<std::size_t>(index) < roots.size()) {
      Browser.open(roots[static_cast<std::size_t>(index)].Path);
      Selected.clear();
    }
  } else if (action == "spray-file-confirm") {
    if (Selected.empty() || !Request)
      return;
    std::string path = Browser.directory();
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
      path += '\\';
    path += Selected;
    Request(path);
  }
  renderBrowser(document);
}

void SpraySection::setResult(bool ok, const std::string &message) {
  (void)ok;
  Message = message;
}

void SpraySection::renderBrowser(Rml::ElementDocument *document) {
  if (!document)
    return;
  if (Rml::Element *path = document->GetElementById("spray-file-path"))
    path->SetInnerRML(framework::TextUtil::escapeRml(Browser.directory()));
  if (Rml::Element *roots = document->GetElementById("spray-file-roots")) {
    std::string markup;
    int index = 0;
    for (const auto &root : Browser.roots()) {
      markup += "<button class=\"spray-file-root\" "
                "data-action=\"spray-file-root\" data-root=\"" +
                std::to_string(index++) + "\">" +
                framework::TextUtil::escapeRml(root.Label) + "</button>";
    }
    roots->SetInnerRML(markup);
  }
  if (Rml::Element *list = document->GetElementById("spray-file-list")) {
    std::string markup;
    int index = 0;
    for (const auto &item : Browser.entries()) {
      markup += "<div class=\"spray-file-row";
      if (!item.Directory && item.Name == Selected)
        markup += " selected";
      markup += "\" data-action=\"spray-file-open\" data-entry=\"" +
                std::to_string(index++) + "\">";
      markup += "<span class=\"spray-file-name\">" +
                framework::TextUtil::escapeRml(item.Name) + "</span>";
      markup += "<span class=\"spray-file-tag\">" +
                framework::TextUtil::escapeRml(
                    translated(item.Directory ? "#Settings_SprayMakerFolder"
                                              : "#Settings_SprayMakerImage")) +
                "</span></div>";
    }
    if (markup.empty())
      markup = "<div class=\"spray-file-empty\">" +
               framework::TextUtil::escapeRml(
                   translated("#Settings_SprayMakerEmpty")) +
               "</div>";
    list->SetInnerRML(markup);
  }
  if (Rml::Element *status = document->GetElementById("spray-file-status")) {
    status->SetInnerRML(framework::TextUtil::escapeRml(
        Browser.error().empty() ? translated("#Settings_SprayMakerHint")
                                : Browser.error()));
  }
}

} // namespace panels::settings::sections
