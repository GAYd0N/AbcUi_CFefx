// panels::DeadMsgPanel implementation - see DeadMsgPanel.h.
// from: ABCEnchance/src/hud/vgui/deadmsg.* (GBK->UTF-8).
// Port note: the old AddItem console print built its printf with transposed
// Q_snprintf arguments (the victim name landed in the format slot); the port
// prints the localized "DeathMsg_ConsolePrint" template correctly.

#include "DeadMsgPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <vector>

#include <abc/CvarCmd.h>
#include <abc/Console.h>
#include <abc/Format.h>

// Windows.h min/max macros fight std::max (old repo precedent).
#undef max
#undef min

#include "../../data/HudState.h"
#include "../../data/TextFeed.h"
#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

namespace panels {

namespace deadmsg {

// Old CDeathMsgPanel ctor defaults (persisted contract - never rename).
cvar_t *DeathNotice = nullptr;
cvar_t *DeathNoticeTime = nullptr;
cvar_t *DeathNoticeCount = nullptr;

void registerCvars() {
  DeathNotice =
      abc::CREATE_CVAR("hud_deathnotice", "1", abc::FCVAR_VALUE, nullptr);
  DeathNoticeTime =
      abc::CREATE_CVAR("hud_deathnotice_time", "6", abc::FCVAR_VALUE, nullptr);
  DeathNoticeCount =
      abc::CREATE_CVAR("hud_deathnotice_count", "6", abc::FCVAR_VALUE, nullptr);
}

} // namespace deadmsg

namespace {
constexpr float kFadeSeconds = 0.5f; // old RunAnimationCommand duration
} // namespace

DeadMsgPanel::DeadMsgPanel()
    : HudPanel("deadmsg", "abc/abcui/hud/rml/deadmsg.rml") {}

void DeadMsgPanel::onDocumentReady() {
  subscribe(textfeed::KillFeed, [this](const char *victim, const char *attacker,
                                       const char *inflictor) {
    addItem(victim, attacker, inflictor);
  });
  // The old viewport-wide HIDEALL token hid the feed with the rest of the
  // Viewport; rows kept fading underneath. On clear the still-alive rows
  // reappear (old Viewport SetVisible(true) revealed its child panels).
  subscribe(hudstate::HideTokenChanged, [this](int) {
    if (hudstate::isHudHidden(hudstate::kHideAll))
      hide();
    else if (!Rows.empty())
      show();
  });
}

void DeadMsgPanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  if (deadmsg::DeathNotice && deadmsg::DeathNotice->value <= 0) {
    reset();
    return;
  }
  Rml::Element *feed = Document ? Document->GetElementById("feed") : nullptr;
  if (!feed)
    return;
  const double keep =
      deadmsg::DeathNoticeTime ? deadmsg::DeathNoticeTime->value : 6.0;
  for (auto itor = Rows.begin(); itor != Rows.end();) {
    Row &row = *itor;
    if (row.fadeStartTime < 0.0) {
      // Old per-row RunAnimationCommand: fade starts keep seconds
      // after the row was added.
      if (ClientTimeNow >= row.createTime + keep) {
        row.fadeStartTime = ClientTimeNow;
        row.element->SetProperty("transition", "opacity 0.5s linear-in");
        row.element->SetProperty("opacity", "0");
      }
      ++itor;
      continue;
    }
    if (ClientTimeNow >= row.fadeStartTime + kFadeSeconds) {
      feed->RemoveChild(row.element);
      itor = Rows.erase(itor);
      continue;
    }
    ++itor;
  }
  if (Rows.empty())
    hide();
}

void DeadMsgPanel::reset() {
  // Old Reset: clear rows + hide.
  Rml::Element *feed = Document ? Document->GetElementById("feed") : nullptr;
  if (feed) {
    for (Row &row : Rows)
      feed->RemoveChild(row.element);
  }
  Rows.clear();
  hide();
}

void DeadMsgPanel::addItem(const char *victim, const char *attacker,
                           const char *inflictor) {
  // Old hud_deathnotice callback (ShowDeathMsg) - evaluated live so the
  // cvar takes effect without a panel rebuild.
  if (deadmsg::DeathNotice && deadmsg::DeathNotice->value <= 0)
    return;
  Rml::Element *feed = Document ? Document->GetElementById("feed") : nullptr;
  if (!feed)
    return;

  // A killfeed during a hide-all token still appends the row (it renders
  // with the panel's next show), but must not un-hide the panel.
  if (!hudstate::isHudHidden(hudstate::kHideAll))
    show();

  // Server-derived names are escaped through the shared helper (D-17 #3;
  // was a local lambda, same rules).
  const auto escapeXml = framework::TextUtil::escapeRml;
  std::string rml = "<div class=\"attacker font-default\">" +
                    escapeXml(attacker) +
                    "</div>"
                    "<div class=\"inflictor font-default\">[" +
                    escapeXml(inflictor) +
                    "]</div>"
                    "<div class=\"victim font-default\">" +
                    escapeXml(victim) + "</div>";
  Rml::ElementDocument *document = feed->GetOwnerDocument();
  Rml::ElementPtr holder = document->CreateElement("div");
  if (!holder)
    return;
  Rml::Element *row = holder.get();
  row->SetClass("row", true);
  row->SetInnerRML(rml);
  feed->AppendChild(std::move(holder));

  // Old capacity: hud_deathnotice_count rows, oldest dropped first.
  const int maxRows = deadmsg::DeathNoticeCount
                          ? static_cast<int>(deadmsg::DeathNoticeCount->value)
                          : 6;
  while (static_cast<int>(Rows.size()) >= std::max(1, maxRows)) {
    feed->RemoveChild(Rows.front().element);
    Rows.erase(Rows.begin());
  }
  Row entry;
  entry.element = row;
  entry.createTime = ClientTimeNow;
  Rows.push_back(entry);

  // Old AddItem console print (localized template). D-17: the token is a
  // {0}..{2} template substituted through abc::Format - the names never
  // touch a printf format slot.
  std::string templateText;
  if (framework::Localization::translate(templateText,
                                         "DeathMsg_ConsolePrint")) {
    abc::ConsolePrint("%s\n",
                      abc::Format(templateText, victim,
                                  attacker[0] == '\0' ? "Something" : attacker,
                                  inflictor)
                          .c_str());
  }
}

} // namespace panels
