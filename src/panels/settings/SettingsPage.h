#pragma once

// panels::settings - the T10 basic settings page (one interactive document),
// extended in T11 with the key-binding section.
//
// The page is a PROJECTION of services::settings::SettingsModel (cvar rows)
// and services::settings::KeyBindModel (bind rows): rows are built from the
// models' tables, edits land in their drafts, Apply/Cancel/Reset go through
// the model transactions. Nothing here writes an engine value or command
// directly and no game state is inferred from the DOM - the gameui module
// decides visibility and calls render()/requestLeave() on its sync, exactly
// like the main-menu shell. Bind capture itself is module-owned (router
// kCapture token): the page only reports the fixed "bind-capture-begin"
// intent through the sink and projects the model's capture state.
//
// Page-internal layers (reset confirmation, unsaved-changes prompt, bind
// conflict, bind defaults confirmation) are DOM overlays inside this
// document, not controller overlays: the T04 controller table is frozen and
// these prompts never outlive the page. The module keeps owning the
// ESC/return decision: it calls requestLeave() before navigating back, and
// the page either shows the unsaved prompt (intent consumed) or reports the
// fixed "return" action through the sink.
//
// Rendering rules: every dynamic string reaches the DOM through
// Localization::translate + TextUtil::escapeRml (draft text is user input);
// all layout is dp (the context density ratio scales). Visibility of the
// layers is tracked in page members, never inferred from IsVisible() (a
// display property lands at the next layout frame - T09 focus-guard fact).

#include "../../framework/InteractiveDocument.h"
#include "sections/CrosshairPreview.h"
#include "sections/SpraySection.h"
#include "sections/VideoSection.h"
#include "services/uiconfig/UiDefinitions.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Rml {
class Element;
class Event;
} // namespace Rml

namespace services {
namespace settings {
class SettingsModel;
struct RowView;
class KeyBindModel;
struct BindRowView;
class VideoModeModel;
class VoiceModel;
} // namespace settings
} // namespace services

namespace panels {
namespace settings {

// Same seam contract as the other menu windows: fixed page-internal action
// names only ("return", "bind-capture-begin"), never user strings.
using ActionSink = std::function<void(const char *action)>;

class SettingsPage final : public framework::InteractiveDocument {
public:
  SettingsPage();
  ~SettingsPage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }
  void setModel(services::settings::SettingsModel *model) {
    Model = model;
  }
  void setPages(
      const std::vector<services::uiconfig::SettingsPageDefinition> *Value) {
    Pages = Value;
  }
  bool selectPage(const std::string &Id);
  const std::string &activePage() const {
    return ActivePage;
  }
  void setBindModel(services::settings::KeyBindModel *model) {
    BindModel = model;
  }
  // T12 video rows: staging goes through the model directly (like the bind
  // rows); the apply intent ("video-apply") is reported to the module, which
  // owns the restart/confirmation presentation.
  void setVideoModel(services::settings::VideoModeModel *model) {
    VideoModel = model;
  }
  // T13 voice section: cvar rows ride the settings model, the hardware
  // rows (mic volume/boost/test) ride this model.
  void setVoiceModel(services::settings::VoiceModel *model) {
    VoiceModel_ = model;
  }
  // Spray maker: the module owns decode/encode/write, so a confirmed pick
  // travels out through this sink and the answer comes back through
  // setSprayResult (message + the pixels to preview).
  void setSpraySink(sections::SpraySection::RequestSink sink) {
    Spray.setRequestSink(std::move(sink));
  }
  // The module's answer: the outcome line plus the Rml source of the preview
  // texture it uploaded (empty clears the preview).
  void setSprayResult(bool ok, const std::string &message,
                      const std::string &previewUri = {});
  bool sprayBrowsing() const {
    return Spray.browsing();
  }

  // Re-projects the whole model into the DOM. Safe while hidden. When
  // `skipInputRow` is non-null, that row's text field keeps its exact
  // editor state (the change-commit path passes the row it came from).
  void render(const char *skipInputRow = nullptr);
  // Cheap voice-only re-projection for the mic-test meter beat (the full
  // render() rewrites every row - too heavy per frame).
  void renderVoiceSection();
  void renderPreviews();
  // Live crosshair preview (the ten part elements of its widget block),
  // drawn from the draft cvar values with the shared geometry.
  void renderCrosshairPreview();
  // Spray maker status + the preview of the spray just made.
  void renderSprayMaker();

  // True while Apply would still change (or has failed to change)
  // something: the module's ESC/return guard asks before discarding.
  bool isDirty() const;

  // Leave guard for the module: clean pages report "return" through the
  // sink; dirty pages show the unsaved-changes layer and consume the
  // intent instead. A second call while the layer is up dismisses it.
  void requestLeave();

  // Re-takes the safe default focus (module's per-frame focus guard): the
  // top-most layer's safe entry, else the Apply button.
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  // One delegated listener pair: click on the document (bubbling),
  // change on the rows container (bubbling from the text fields).
  class PageListener final : public Rml::EventListener {
  public:
    explicit PageListener(SettingsPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    SettingsPage *Owner;
  };

  void handleClick(Rml::Event &event);
  void handleChange(Rml::Event &event);
  void buildRows();
  void buildNavigation();
  void projectPage();
  // Moves each singleton widget block (settings.rml) into the card of the
  // section that declares its component; unclaimed blocks wait hidden in
  // the widget host.
  void projectWidgets();
  // Spray picker (page-internal layer): the section owns the browser, the
  // page owns the layer frame and its focus.
  void openSprayBrowser();
  void closeSprayBrowser();
  void resetCurrentPage();
  void releasePreviews();
  bool hasComponent(const char *Name) const;
  const services::uiconfig::SettingsPageDefinition *currentPage() const;
  void buildBindRows();
  void renderRow(const services::settings::RowView &view, bool skipInput);
  void renderBindRow(const services::settings::BindRowView &view);
  void renderVideoSection();
  void renderBindConflict();
  void setBindStatusSummary();
  void setStatusSummary();
  void applyAndReport();
  void showLayer(const char *id);
  void hideLayers();
  bool layerVisible() const {
    return !ActiveLayer.empty();
  }
  std::string
  bindActionLabel(const services::settings::BindRowView &view) const;
  // Shared resolution for one raw label string ("#Valve_X" or literal).
  std::string bindLabelText(const std::string &label) const;
  // The options last projected into one row <select> (cvar -> values);
  // selects rebuild only when the set changed (the T12 Add()-vs-inline
  // RmlUi pin applies to every select on this page).
  const std::vector<std::string> *
  shownSelectValues(const std::string &cvar) const;
  void setShownSelectValues(const std::string &cvar,
                            std::vector<std::string> values);

  PageListener Listener;
  ActionSink Sink;
  const std::vector<services::uiconfig::SettingsPageDefinition> *Pages =
      nullptr;
  std::string ActivePage;
  services::settings::SettingsModel *Model = nullptr;
  services::settings::KeyBindModel *BindModel = nullptr;
  services::settings::VideoModeModel *VideoModel = nullptr;
  services::settings::VoiceModel *VoiceModel_ = nullptr;
  // The resolution options last projected into the <select> ("1920x1080"
  // value strings). The mode list is engine-stable per session; the select
  // is rebuilt only when the set actually changed (no per-render churn).
  sections::VideoSection VideoView;
  sections::CrosshairPreviewState CrosshairPreview;
  sections::SpraySection Spray;
  std::map<std::string, std::vector<std::string>> SelectValuesShown;
  // The hue colour last painted on one row's drag thumb (cvar -> "rgb(...)"):
  // the swatch is rewritten only when it changed, so the render that follows
  // every slider change never re-dirties the style tree on its own.
  std::map<std::string, std::string> HueSwatchShown;
  // The mic-volume field of the change commit currently in flight (its
  // caret/IME state must survive the status refresh, like the row inputs).
  Rml::Element *VoiceVolumeSkipField = nullptr;
  // Re-entrancy guard: the row selects dispatch change while render()
  // builds their options (the T12 Add() pin) - the change handler must
  // not start a nested render pass from inside one.
  bool InRender_ = false;
  // Page-internal modal state: "" when no layer is up, else the layer's
  // DOM id ("reset-confirm" / "leave-confirm" / "bind-conflict" /
  // "bind-reset-confirm"). The truth for event routing, kept in lockstep
  // with the display property.
  std::string ActiveLayer;
};

} // namespace settings
} // namespace panels
