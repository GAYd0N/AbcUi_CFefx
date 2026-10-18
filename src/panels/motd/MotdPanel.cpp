// panels::Motd implementation - see MotdPanel.h.
// from: ABCEnchance/src/hud/vgui/motd.cpp (GBK->UTF-8).
#include <metahook.h>

#include "MotdPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

#include <abc/CvarCmd.h>
#include <abc/Console.h>

#include "../../data/NetworkMessages.h"
#include "../../framework/Commands.h"
#include "../../framework/PanelManager.h"
#include "../../framework/ResourceFile.h"
#include "../../framework/TextUtil.h"

namespace panels {

namespace motd {
// Old ctor cvar (persisted contract - never rename).
cvar_t *HudMotd = nullptr;
namespace {
MotdPanel *instance() {
  return static_cast<MotdPanel *>(framework::PanelManager::find("motd"));
}
} // namespace

void registerCvars() {
  // Old callback: re-typing hud_motd 1/-1 mid-game dismissed the open panel
  // (0 left it alone - the vanilla window takes over on the next trigger).
  HudMotd =
      abc::CREATE_CVAR("hud_motd", "1", abc::FCVAR_VALUE, [](cvar_t *cvar) {
        switch (static_cast<int>(cvar->value)) {
        case 1:
        case -1:
          if (MotdPanel *panel = instance())
            panel->hide();
          break;
        default:
          break;
        }
      });
}

// Wheel ownership: the open motd scrolls (old per-page auto-advance died
// with the pagination - D-17 ledger).
bool wantsWheel() {
  MotdPanel *panel = instance();
  return panel && panel->isVisible();
}

// The old TFV_ShowVGUIMenu hook's gate: hud_motd != 0 keeps the vanilla MOTD
// window down (>= 1 shows this panel instead, < 0 suppresses both).
bool blocksVanillaWindow() {
  return HudMotd && HudMotd->value != 0.0f;
}

// Old CViewport::ShowMOTD -> CMotdPanel::ShowMotd: the client's
// ShowVGUIMenu(MOTD) lands here instead of the vanilla window. The hud_motd
// gate rides inside showMotd like the old one (the hook only guarantees the
// vanilla side stays down).
void showMotd() {
  if (MotdPanel *panel = instance())
    panel->showMotd();
}
} // namespace motd

// Old MotdPanel.res timing.
constexpr double kKeepTime = 5.0;
constexpr double kFadeTime = 1.0;

// Minimal HTML-to-text: drop script/style blocks and tags, decode the five
// required entities (the vgui HTML control rendered the full document; the
// Rml port shows the readable content - see the header D-17 note).
std::string htmlToText(std::string html) {
  std::string lowered = html;
  for (char &c : lowered)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const auto dropBlock = [&](const char *tag) {
    size_t at = 0;
    while (true) {
      const std::string open = std::string("<") + tag;
      const size_t begin = lowered.find(open, at);
      if (begin == std::string::npos)
        break;
      const std::string close = std::string("</") + tag + ">";
      size_t end = lowered.find(close, begin);
      end = end == std::string::npos ? html.size() : end + close.size();
      html.erase(begin, end - begin);
      lowered.erase(begin, end - begin);
      at = begin;
    }
  };
  dropBlock("script");
  dropBlock("style");

  std::string out;
  out.reserve(html.size());
  for (size_t i = 0; i < html.size(); i++) {
    if (html[i] == '<') {
      const size_t close = html.find('>', i);
      i = close == std::string::npos ? html.size() : close;
      continue;
    }
    out += html[i];
  }
  const auto replaceAll = [](std::string text, const char *from,
                             const char *to) {
    size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
      text.replace(at, strlen(from), to);
      at += strlen(to);
    }
    return text;
  };
  out = replaceAll(out, "&nbsp;", " ");
  out = replaceAll(out, "&amp;", "&");
  out = replaceAll(out, "&lt;", "<");
  out = replaceAll(out, "&gt;", ">");
  out = replaceAll(out, "&quot;", "\"");
  return out;
}

MotdPanel::MotdPanel() : HudPanel("motd", "abc/abcui/hud/rml/motd.rml") {}

MotdPanel::~MotdPanel() = default;

void MotdPanel::reset() {
  Buffer.clear();
  Page.clear();
  HasContent = false;
  Fading = false;
  // Old Reset cleared the mission-brief flag too (m_bLoadedMissionBrief =
  // false) - the brief reloads per map through the VidInit panel reset.
  LoadedMissionBrief = false;
  if (Rml::Element *box = element("box"))
    box->SetProperty("opacity", "1");
  hide();
}

void MotdPanel::append(const char *message) {
  if (!message)
    return;
  const size_t size = strlen(message);
  if (size > 65536 || Buffer.size() > 65536 - size) {
    Buffer.clear();
    return;
  }
  Buffer += message;
}

void MotdPanel::startCountdown() {
  HasContent = !Page.empty();
  ShowTime = ClientTimeNow + kKeepTime;
  Fading = false;
  if (Rml::Element *box = element("box"))
    box->SetProperty("opacity", "1");
  show();
}

void MotdPanel::finishSend() {
  if (Buffer.empty()) {
    return;
  }
  // Old FinishSendMOTD: HTML documents and http(s) URLs went to the vgui
  // HTML control; everything else became text pages - and the finish never
  // showed the window (display waited for missionbriefing/ShowMotd). The
  // port has no browser surface: HTML renders stripped, URLs render as
  // text with a console note (D-17 - see the header).
  if (Buffer.starts_with("<!DOCTYPE html>") ||
      Buffer.starts_with("<!doctype html>")) {
    Page = htmlToText(Buffer);
  } else if (Buffer.starts_with("http://") || Buffer.starts_with("https://")) {
    Page = Buffer;
    abc::ConsoleWarn(
        "[AbcUi] motd is a URL; open it in a browser manually: %s\n",
        Buffer.c_str());
  } else {
    Page = Buffer;
  }
  Buffer.clear();
  if (Page.empty())
    return;
  if (Rml::Element *message = element("message"))
    message->SetInnerRML(framework::TextUtil::escapeRml(Page));
  HasContent = true;
}

void MotdPanel::loadMissionBrief() {
  // Old LoadMissionBrief read GetLevelName() + ".txt" - the level name is
  // "maps/<map>.bsp", so the file was "maps/<map>.bsp.txt" (the SC mission
  // briefing convention). The port prefers that legacy name and falls back
  // to the plain "maps/<map>.txt" (D-17: both conventions served).
  if (LoadedMissionBrief)
    return;
  LoadedMissionBrief = true;
  const char *levelName = gEngfuncs.pfnGetLevelName();
  if (!levelName || !levelName[0])
    return;

  const auto tryRead = [&](const std::string &path) {
    std::vector<unsigned char> bytes;
    if (!framework::ResourceFile::read(path, bytes, 65536))
      return false;
    Buffer.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    return true;
  };
  if (tryRead(std::string(levelName) + ".txt"))
    return;
  tryRead(std::string(levelName).substr(0, std::string(levelName).rfind('.')) +
          ".txt");
}

void MotdPanel::showMotd() {
  if (motd::HudMotd && motd::HudMotd->value < 1.0f)
    return;
  loadMissionBrief();
  // Old ShowMotd: FinishSendMOTD first when pages were not built, then the
  // window showed and counted down. Mid-game motd_write alone never popped
  // the window - the command path is the only shower.
  if (Page.empty())
    finishSend();
  if (!Page.empty())
    startCountdown();
}

void MotdPanel::onDocumentReady() {
  subscribe(netmsg::Motd, [this](int code, const char *text) {
    if (text && text[0]) {
      append(text);
    } else {
      // Old empty-chunk semantics: the server closes the window.
      HasContent = false;
      hide();
    }
    if (code > 0)
      finishSend();
  });
  if (abc::CChainedCommand *brief = commands::get("missionbriefing")) {
    subscribe(brief->Event(), [this]() {
      showMotd();
      // Old g_EventCmdMissionBrief always swallowed the original
      // (ret &= false) - the vanilla SC briefing window must not open
      // on top of ours.
      return false;
    });
  }
  hide();
}

void MotdPanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  if (!HasContent || !Document)
    return;
  if (!isVisible())
    return;

  const double remaining = ShowTime - ClientTimeNow;
  if (remaining > 0.0) {
    if (Rml::Element *progress = element("progress")) {
      const float ratio = static_cast<float>(remaining / kKeepTime);
      progress->SetProperty(
          "width",
          std::to_string(122.0f * std::clamp(ratio, 0.0f, 1.0f)) + "dp");
    }
    Fading = false;
    return;
  }
  // Fade window: opacity rides the CSS transition (fade_time 1s), the box
  // hides when the window closes.
  if (ClientTimeNow > ShowTime + kFadeTime) {
    HasContent = false;
    hide();
    return;
  }
  if (!Fading) {
    Fading = true;
    if (Rml::Element *box = element("box"))
      box->SetProperty("opacity", "0");
  }
}

} // namespace panels
