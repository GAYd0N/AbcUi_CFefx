// abcui.motd - the mission briefing / server message window. Moved from
// src/panels/motd (old CMotdPanel, ABCEnchance sources: src/hud/vgui/motd.*).
//
// The server's MOTD chunks accumulate until the final chunk, the
// missionbriefing command (and the client's ShowVGUIMenu(MOTD) request) opens
// the window, and a keep_time + fade_time countdown drives the progress strip
// and the fade-out.
//
// D-17 deviations kept from the port: RmlUi has no HTML control, so HTML
// MOTDs render as their stripped text content and http(s) MOTD URLs render as
// text with a console note (the old vgui HTML control embedded a browser;
// auto-opening server URLs in the OS browser would be an untrusted-content
// hole). The old character-count pagination rode vgui font metrics - one
// scrollable page with the same keep_time/fade rhythm replaces it.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

// Old ctor cvar (persisted contract - never rename).
constexpr const char *kHudMotd = "hud_motd";

constexpr const char *kPanelName = "motd";
constexpr const char *kDocumentPath = "mod://abcui.motd/hud/rml/motd.rml";

// ShowVGUIMenu ids the old TFV_ShowVGUIMenu hook cared about.
constexpr int kMenuMissionBrief = 4;
constexpr int kMenuMotd = 5;

// Old MotdPanel.res timing.
constexpr double kKeepTime = 5.0;
constexpr double kFadeTime = 1.0;
// Old AppendMotd / mission-brief read bound.
constexpr uint32_t kMaxMotdBytes = 65536;

AbcUiCvarHandle HudMotd = nullptr;

std::string Buffer;
std::string Page;
bool LoadedMissionBrief = false;
bool HasContent = false;
double ShowTime = 0.0;
double ClientTimeNow = 0.0;
bool Fading = false;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

void setOpacity(const char *value) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *box = Api->ui->querySelector(doc, "box"))
    Api->ui->elementSetProperty(box, "opacity", value);
}

int motdValue() {
  return HudMotd ? static_cast<int>(Api->console->cvarValue(HudMotd, 1.0f)) : 1;
}

// The old TFV_ShowVGUIMenu hook's gate: hud_motd != 0 keeps the vanilla MOTD
// window down (>= 1 shows this panel instead, < 0 suppresses both).
bool blocksVanillaWindow() {
  return motdValue() != 0;
}

void hideWindow() {
  AbcUiDocument *doc = panelDocument();
  if (doc)
    Api->ui->documentHide(doc);
}

void showWindow() {
  AbcUiDocument *doc = panelDocument();
  if (doc)
    Api->ui->documentShow(doc);
}

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

// Measure-then-copy over the bounded C API (server text is untrusted).
std::string escapeText(const char *text) {
  if (!text || !*text)
    return std::string();
  const int needed = Api->ui->escapeRml(text, nullptr, 0);
  if (needed <= 0)
    return std::string();
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->escapeRml(text, out.data(), static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

void append(const char *message) {
  if (!message)
    return;
  const size_t size = strlen(message);
  if (size > kMaxMotdBytes || Buffer.size() > kMaxMotdBytes - size) {
    Buffer.clear();
    return;
  }
  Buffer += message;
}

// Old FinishSendMOTD (build only - the old finish never showed the window;
// display waited for missionbriefing/ShowMotd).
void finishSend() {
  if (Buffer.empty())
    return;
  // Old FinishSendMOTD: HTML documents and http(s) URLs went to the vgui
  // HTML control; everything else became text pages. The port has no browser
  // surface: HTML renders stripped, URLs render as text with a console note
  // (D-17 - see the header).
  if (Buffer.starts_with("<!DOCTYPE html>") ||
      Buffer.starts_with("<!doctype html>")) {
    Page = htmlToText(Buffer);
  } else if (Buffer.starts_with("http://") || Buffer.starts_with("https://")) {
    Page = Buffer;
    Api->core->warn("motd is a URL; open it in a browser manually: %s\n",
                    Buffer.c_str());
  } else {
    Page = Buffer;
  }
  Buffer.clear();
  if (Page.empty())
    return;
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *message = Api->ui->querySelector(doc, "message")) {
    const std::string escaped = escapeText(Page.c_str());
    Api->ui->elementSetInnerRML(message, escaped.c_str());
  }
  HasContent = true;
}

bool tryRead(const std::string &path) {
  const int size =
      Api->vfs->readEngineFile(path.c_str(), nullptr, 0, kMaxMotdBytes);
  if (size <= 0)
    return false;
  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  if (Api->vfs->readEngineFile(path.c_str(), bytes.data(),
                               static_cast<uint32_t>(bytes.size()),
                               kMaxMotdBytes) != size)
    return false;
  Buffer.assign(bytes.begin(), bytes.end());
  return true;
}

// Old LoadMissionBrief read GetLevelName() + ".txt" - the level name is
// "maps/<map>.bsp", so the file was "maps/<map>.bsp.txt" (the SC mission
// briefing convention). The port prefers that legacy name and falls back to
// the plain "maps/<map>.txt" (D-17: both conventions served).
void loadMissionBrief() {
  if (LoadedMissionBrief)
    return;
  LoadedMissionBrief = true;
  char level[512];
  if (Api->data->levelName(level, sizeof(level)) <= 0)
    return;
  const std::string levelName = level;
  if (tryRead(levelName + ".txt"))
    return;
  const size_t dot = levelName.rfind('.');
  if (dot != std::string::npos)
    tryRead(levelName.substr(0, dot) + ".txt");
}

void startCountdown() {
  HasContent = !Page.empty();
  ShowTime = ClientTimeNow + kKeepTime;
  Fading = false;
  setOpacity("1");
  showWindow();
}

// Old CViewport::ShowMOTD -> CMotdPanel::ShowMotd: the client's
// ShowVGUIMenu(MOTD) lands here instead of the vanilla window.
void showMotd() {
  if (motdValue() < 1)
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

// ---- event sinks ----

void onMotd(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiMotdEvent *>(payload);
  if (event->text && event->text[0]) {
    append(event->text);
  } else {
    // Old empty-chunk semantics: the server closes the window.
    HasContent = false;
    hideWindow();
  }
  if (event->code > 0)
    finishSend();
}

int onVguiMenu(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiVguiMenuEvent *>(payload);
  // The vanilla mission brief never opens: the missionbriefing command chain
  // feeds this panel instead.
  if (event->menu == kMenuMissionBrief)
    return 1;
  if (event->menu == kMenuMotd && blocksVanillaWindow()) {
    showMotd();
    return 1;
  }
  return 0;
}

int onWheel(void *, const void *) {
  // The open motd scrolls (the old per-page auto-advance died with the
  // pagination); claiming routes the pulse into the Rml context.
  AbcUiDocument *doc = panelDocument();
  return doc && Api->ui->documentVisible(doc) ? 1 : 0;
}

// ---- cvar watch ----

void onHudMotdChanged(void *) {
  // Old callback: re-typing hud_motd 1/-1 mid-game dismissed the open panel
  // (0 left it alone - the vanilla window takes over on the next trigger).
  const int value = motdValue();
  if (value == 1 || value == -1)
    hideWindow();
}

// ---- panel callbacks ----

void panelOnData(void *, double frameTime) {
  ClientTimeNow += frameTime;
  AbcUiDocument *doc = panelDocument();
  if (!HasContent || !doc || !Api->ui->documentVisible(doc))
    return;

  const double remaining = ShowTime - ClientTimeNow;
  if (remaining > 0.0) {
    if (AbcUiElement *progress = Api->ui->querySelector(doc, "progress")) {
      const float ratio = static_cast<float>(remaining / kKeepTime);
      char width[32];
      std::snprintf(width, sizeof(width), "%gdp",
                    122.0 * std::clamp(ratio, 0.0f, 1.0f));
      Api->ui->elementSetProperty(progress, "width", width);
    }
    Fading = false;
    return;
  }
  // Fade window: opacity rides the CSS transition (fade_time 1s), the box
  // hides when the window closes.
  if (ClientTimeNow > ShowTime + kFadeTime) {
    HasContent = false;
    hideWindow();
    return;
  }
  if (!Fading) {
    Fading = true;
    setOpacity("0");
  }
}

void panelReset(void *) {
  Buffer.clear();
  Page.clear();
  HasContent = false;
  Fading = false;
  // Old Reset cleared the mission-brief flag too (m_bLoadedMissionBrief =
  // false) - the brief reloads per map through the VidInit panel reset.
  LoadedMissionBrief = false;
  setOpacity("1");
  hideWindow();
}

void panelDocumentReady(void *) {
  hideWindow();
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_MOTD, 0, &onMotd, nullptr);
  Api->events->subscribeVeto(ABCUI_CHANNEL_MENU_VGUI_MENU, 0, &onVguiMenu,
                             nullptr);
  Api->events->subscribeVeto(ABCUI_CHANNEL_INPUT_WHEEL, 0, &onWheel, nullptr);
  // Old g_EventCmdMissionBrief: the chained command always swallowed the
  // original (the vanilla SC briefing window must not open on top of ours),
  // so the sink always consumes.
  Api->commands->subscribeCommand(
      "missionbriefing", 0,
      [](void *, const void *) -> int {
        showMotd();
        return 1;
      },
      nullptr);
}

void onModuleInit() {
  HudMotd = Api->console->registerCvarWatch(kHudMotd, "1", ABCUI_CVAR_VALUE,
                                            &onHudMotdChanged, nullptr);
}

void onModuleShutdown() {
  // The document dies with the context. The old panel object was rebuilt by
  // every HUD_Init install cycle, so its buffer/page/mission-brief state died
  // with the document as well; do the same here instead of carrying a
  // half-built MOTD into the next cycle.
  Buffer.clear();
  Page.clear();
  HasContent = false;
  Fading = false;
  LoadedMissionBrief = false;
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
                                         const AbcUiModuleApi *api,
                                         AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api || !out)
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data || !api->vfs ||
      !api->commands)
    return 0;
  // The console/vfs/data/ui sections grew for this module; refuse a framework
  // whose tables are shorter than the members used below.
  if (api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->vfs->structSize < sizeof(AbcUiVfsApi) ||
      api->data->structSize < sizeof(AbcUiDataApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.motd";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
