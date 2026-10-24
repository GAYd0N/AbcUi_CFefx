// The spray card: the maker preview and the page-owned file browser layer
// (the directory model itself lives in SettingsFileBrowser).

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <cstdlib>
#include <string>

namespace settingspage {

// ----------------------------------------------------------- spray picker --

std::string sprayStatusText() {
  char buffer[ABCUI_SETTINGS_TEXT_MAX];
  buffer[0] = '\0';
  if (!Api->settings || !Api->settings->sprayStatus(buffer, sizeof(buffer)))
    return {};
  return buffer;
}

std::string sprayPreviewText() {
  char buffer[512];
  buffer[0] = '\0';
  if (!Api->settings || !Api->settings->sprayPreviewUri(buffer, sizeof(buffer)))
    return {};
  return buffer;
}

int32_t attributeIndex(AbcUiElement *element, const char *name) {
  const std::string text = attributeOf(element, name);
  return text.empty() ? -1 : parseInt(text).value_or(0);
}

void renderSprayBrowser() {
  if (!Page)
    return;
  if (AbcUiElement *path = node("spray-file-path"))
    setInnerRml(path, escapeText(Browser.directory()));
  if (AbcUiElement *roots = node("spray-file-roots")) {
    std::string markup;
    int index = 0;
    for (const auto &root : Browser.roots()) {
      markup += "<button class=\"spray-file-root\" "
                "data-action=\"" +
                std::string(kActionSprayRoot) + "\" data-root=\"" +
                std::to_string(index++) + "\">" + escapeText(root.Label) +
                "</button>";
    }
    Api->ui->elementSetInnerRML(roots, markup.c_str());
  }
  if (AbcUiElement *list = node("spray-file-list")) {
    std::string markup;
    int index = 0;
    for (const auto &item : Browser.entries()) {
      markup += "<div class=\"spray-file-row";
      if (!item.Directory && item.Name == SpraySelected)
        markup += " selected";
      markup +=
          "\" data-action=\"" + std::string(kActionSprayOpen) +
          "\" data-entry=\"" + std::to_string(index++) + "\">" +
          "<span class=\"spray-file-name\">" + escapeText(item.Name) +
          "</span><span class=\"spray-file-tag\">" +
          escapeText(resolve(item.Directory
                                 ? "#abcui.gameui/Settings_SprayMakerFolder"
                                 : "#abcui.gameui/Settings_SprayMakerImage")) +
          "</span></div>";
    }
    if (markup.empty())
      markup = "<div class=\"spray-file-empty\">" +
               escapeText(resolve("#abcui.gameui/Settings_SprayMakerEmpty")) +
               "</div>";
    Api->ui->elementSetInnerRML(list, markup.c_str());
  }
  if (AbcUiElement *status = node("spray-file-status")) {
    setInnerRml(
        status,
        escapeText(Browser.error().empty()
                       ? resolve("#abcui.gameui/Settings_SprayMakerHint")
                       : Browser.error()));
  }
}

void openSprayBrowser() {
  if (!Page)
    return;
  hideLayers();
  SprayOpen = true;
  SpraySelected.clear();
  Browser.setRoots(files::FileBrowser::defaultRoots());
  // Start at the game folder (the process working directory) - the first
  // root - and fall back to the first readable one.
  for (const auto &root : Browser.roots()) {
    if (Browser.open(root.Path))
      break;
  }
  renderSprayBrowser();
  setDisplay(kLayerSprayFile, true);
  ActiveLayer = kLayerSprayFile;
  if (AbcUiElement *focus = node("spray-file-cancel"))
    Api->ui->elementFocus(focus);
}

void closeSprayBrowser() {
  hideLayers();
}

void handleSprayAction(const std::string &action, AbcUiElement *entry) {
  if (action == kActionSprayOpen) {
    const int32_t index = attributeIndex(entry, "data-entry");
    const auto &entries = Browser.entries();
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size())
      return;
    const auto &picked = entries[static_cast<std::size_t>(index)];
    if (picked.Directory) {
      Browser.enter(picked.Name);
      SpraySelected.clear();
    } else {
      // A file click only selects; the confirm button starts the maker.
      SpraySelected = picked.Name;
    }
  } else if (action == kActionSprayUp) {
    Browser.openParent();
    SpraySelected.clear();
  } else if (action == kActionSprayRoot) {
    const int32_t index = attributeIndex(entry, "data-root");
    const auto &roots = Browser.roots();
    if (index >= 0 && static_cast<std::size_t>(index) < roots.size()) {
      Browser.open(roots[static_cast<std::size_t>(index)].Path);
      SpraySelected.clear();
    }
  } else if (action == kActionSprayConfirm) {
    if (SpraySelected.empty() || !Api->settings)
      return;
    std::string path = Browser.directory();
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
      path += '\\';
    path += SpraySelected;
    // The maker runs framework-side (decode/encode/write + the preview
    // upload) and stores its answer; the page reads it back on the next
    // render. A synchronous request means the layer can close right here.
    Api->settings->sprayMake(path.c_str());
    hideLayers();
    render();
    refocusDefault();
    return;
  }
  renderSprayBrowser();
}

void renderSprayMaker() {
  if (!Page)
    return;
  const std::string status = sprayStatusText();
  setInnerRml(node("spray-maker-status"), escapeText(status));
  AbcUiElement *image = node("spray-made-preview");
  if (!image)
    return;
  const std::string uri = sprayPreviewText();
  if (uri.empty()) {
    setDisplay(image, false);
    return;
  }
  if (attributeOf(image, "src") != uri)
    Api->ui->elementSetAttribute(image, "src", uri.c_str());
  setDisplay(image, true);
}

} // namespace settingspage
