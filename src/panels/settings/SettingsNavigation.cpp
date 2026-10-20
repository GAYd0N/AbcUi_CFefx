#include "SettingsPage.h"
#include "SettingsUi.h"
#include "framework/TextUtil.h"
#include "services/settings/KeyBindModel.h"
#include "services/settings/SettingsModel.h"
#include "services/settings/VoiceModel.h"

#include <RmlUi/Core/ElementDocument.h>
#include <algorithm>

namespace panels::settings {

const services::uiconfig::SettingsPageDefinition *
SettingsPage::currentPage() const {
  if (Pages) {
    for (const auto &Page : *Pages)
      if (Page.Id == ActivePage)
        return &Page;
  }
  return nullptr;
}

bool SettingsPage::hasComponent(const char *Name) const {
  // Unconfigured test fixtures retain the legacy all-in-one projection;
  // production always supplies Pages, including an empty failed catalog.
  // Page-level behaviours (user-script) and the section widgets share this
  // one query: a widget exists whenever the active page declares it.
  const auto *Page = currentPage();
  return !Pages ||
         (Page && (Page->hasComponent(Name) || Page->sectionHaving(Name)));
}

void SettingsPage::buildNavigation() {
  if (!document() || !Pages)
    return;
  auto *Navigation = document()->GetElementById("settings-navigation");
  if (!Navigation)
    return;
  std::string Markup;
  for (const auto &Page : *Pages) {
    Markup +=
        "<button id=\"settings-tab-" + Page.Id +
        "\" data-action=\"settings-page\" data-page=\"" + Page.Id + "\">" +
        framework::TextUtil::escapeRml(detail::translated(Page.Label.c_str())) +
        "</button>";
  }
  Navigation->SetInnerRML(Markup);
  if (!currentPage())
    ActivePage = Pages->empty() ? "" : Pages->front().Id;
  projectPage();
}

bool SettingsPage::selectPage(const std::string &Id) {
  if (!Pages || layerVisible() || (BindModel && BindModel->capturing()))
    return false;
  auto It = std::find_if(Pages->begin(), Pages->end(),
                         [&](const auto &Page) { return Page.Id == Id; });
  if (It == Pages->end())
    return false;
  if (Id != ActivePage && VoiceModel_) {
    VoiceModel_->stopTest();
    renderVoiceSection();
  }
  ActivePage = Id;
  projectPage();
  if (document()) {
    if (auto *Panel = document()->GetElementById("panel"))
      Panel->SetScrollTop(0);
    if (auto *Tab = document()->GetElementById("settings-tab-" + Id))
      Tab->Focus();
  }
  return true;
}

void SettingsPage::projectPage() {
  if (!document() || !Pages)
    return;
  // A page switch strands the picker's right-click menu (it lives outside
  // the scroll container): close it with the page it belongs to.
  ModelPicker.closeContextMenu(document());
  for (const auto &Page : *Pages) {
    if (auto *Rows = document()->GetElementById("settings-rows-" + Page.Id))
      Rows->SetProperty("display", Page.Id == ActivePage ? "block" : "none");
    if (auto *Tab = document()->GetElementById("settings-tab-" + Page.Id))
      Tab->SetClass("selected", Page.Id == ActivePage);
  }
  projectWidgets();
  // The key-binding defaults entry lives in the global action bar and is
  // only offered where the page declares the bind list.
  if (auto *BindReset = document()->GetElementById("bind-reset"))
    BindReset->SetProperty("display",
                           hasComponent("bindings") ? "block" : "none");
  if (auto *Title = document()->GetElementById("title")) {
    const auto *Page = currentPage();
    Title->SetInnerRML(framework::TextUtil::escapeRml(
        detail::translated(Page ? Page->Label.c_str() : "#Settings_Title")));
  }
  if (auto *Description = document()->GetElementById("settings-description")) {
    const auto *Page = currentPage();
    Description->SetInnerRML(
        Page ? framework::TextUtil::escapeRml(
                   detail::translated(Page->Description.c_str()))
             : "");
  }
  if (auto *Reset = document()->GetElementById("settings-reset"))
    Reset->SetProperty("display", currentPage() && !currentPage()->Cvars.empty()
                                      ? "block"
                                      : "none");
}

void SettingsPage::projectWidgets() {
  Rml::ElementDocument *Doc = document();
  Rml::Element *Host =
      Doc ? Doc->GetElementById("settings-widget-host") : nullptr;
  if (!Doc || !Host)
    return;
  const auto *Page = currentPage();
  // Each widget block is a singleton: home is the card of the section that
  // declares its component, otherwise the (hidden) parking host. Row-flow
  // blocks join the rows column (after the last row); everything else goes
  // to the card's widget column.
  for (int I = 0; I < detail::kWidgetBlockCount; ++I) {
    Rml::Element *Block = Doc->GetElementById(detail::kWidgetBlocks[I].Element);
    if (!Block)
      continue;
    Rml::Element *Target = Host;
    if (Page) {
      if (const auto *Section =
              Page->sectionHaving(detail::kWidgetBlocks[I].Component)) {
        const std::string Suffix =
            detail::kWidgetBlocks[I].RowFlow ? "-items" : "-widgets";
        if (Rml::Element *Container = Doc->GetElementById(
                ("settings-card-" + Page->Id + "-" + Section->Id + Suffix)
                    .c_str()))
          Target = Container;
      }
    }
    Rml::Element *Parent = Block->GetParentNode();
    if (!Parent || Parent == Target)
      continue;
    Rml::ElementPtr Detached = Parent->RemoveChild(Block);
    if (Detached)
      Target->AppendChild(std::move(Detached));
  }
}

void SettingsPage::resetCurrentPage() {
  if (!Model)
    return;
  if (!Pages)
    Model->resetDefaults();
  else if (const auto *Page = currentPage())
    Model->resetDefaults(Page->Cvars);
  // Hardware/display controls have no verified factory-default provider.
  // Their own explicit transactions remain separate; never invent values.
}
} // namespace panels::settings
