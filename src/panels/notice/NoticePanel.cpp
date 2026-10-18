// panels::NoticePanel implementation - see NoticePanel.h.
// from: ABCEnchance/src/hud/vgui/notice.* (GBK->UTF-8).

#include "NoticePanel.h"

#include <RmlUi/Core/Element.h>

#include "../../data/HudState.h"
#include "../../data/TextFeed.h"
#include "../../framework/TextUtil.h"

namespace panels {

namespace {
// Old NoticePanel.res keep_time.
constexpr double kKeepTime = 3.0;
} // namespace

NoticePanel::NoticePanel(const char *name, const char *documentPath, int target)
    : HudPanel(name, documentPath), Target(target) {}

void NoticePanel::onDocumentReady() {
  subscribe(textfeed::Notice, [this](int target, const char *text) {
    if (target != Target)
      return;
    if (Rml::Element *message = element("message"))
      // D-17 #3: server-derived text goes in escaped (the old vgui
      // Label drew it plain; SetInnerRML would hand the server an
      // element-injection surface the original never had).
      message->SetInnerRML(framework::TextUtil::escapeRml(text));
    // Old ShowMessage: close at client time + keep_time.
    CloseTime = ClientTimeNow + kKeepTime;
    // The old viewport-wide HIDEALL bit hid the notice with everything
    // else (messages still armed the close timer).
    if (!hudstate::isHudHidden(hudstate::kHideAll))
      show();
  });
  subscribe(hudstate::HideTokenChanged, [this](int) {
    if (hudstate::isHudHidden(hudstate::kHideAll))
      hide();
    else if (CloseTime > ClientTimeNow)
      show();
  });
}

void NoticePanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  if (CloseTime > 0.0 && ClientTimeNow > CloseTime) {
    CloseTime = 0.0;
    hide();
  }
}

void NoticePanel::reset() {
  // Old Reset: drop the countdown and hide.
  CloseTime = 0.0;
  hide();
}

} // namespace panels
