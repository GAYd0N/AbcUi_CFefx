// abcui.textfeed - the centerprint / notify text panels. Moved from
// src/panels/notice (old CNoticePanel, ABCEnchance sources:
// src/hud/vgui/notice.*). Two instances: "notice" (PRINTNOTIFY, top-left) and
// "notice-center" (PRINTCENTER, screen-center); both fed by the
// textfeed.notice channel with the old keep_time countdown.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;

// Old NoticePanel.res keep_time.
constexpr double kKeepTime = 3.0;

struct NoticeState {
  AbcUiPanelDesc Panel{};
  int target = 0;
  double closeTime = 0.0;
  double clientTimeNow = 0.0;
  bool hideAll = false;
};

NoticeState Notify;
NoticeState Center;

NoticeState *stateOf(void *userdata) {
  return static_cast<NoticeState *>(userdata);
}

AbcUiDocument *panelDocument(const NoticeState &state) {
  return Api->ui->panelDocument(state.Panel.name);
}

void setVisible(NoticeState &state, bool visible) {
  AbcUiDocument *doc = panelDocument(state);
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
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

void showMessage(NoticeState &state, const char *text) {
  AbcUiDocument *doc = panelDocument(state);
  if (!doc)
    return;
  if (AbcUiElement *message = Api->ui->querySelector(doc, "message")) {
    // D-17 #3: server-derived text goes in escaped (the old vgui Label drew
    // it plain; SetInnerRML would hand the server an element-injection
    // surface the original never had).
    const std::string escaped = escapeText(text);
    Api->ui->elementSetInnerRML(message, escaped.c_str());
  }
  // Old ShowMessage: close at client time + keep_time.
  state.closeTime = state.clientTimeNow + kKeepTime;
  // The old viewport-wide HIDEALL bit hid the notice with everything else
  // (messages still armed the close timer).
  if (!state.hideAll)
    setVisible(state, true);
}

void updateHidden(NoticeState &state) {
  if (state.hideAll)
    setVisible(state, false);
  else if (state.closeTime > state.clientTimeNow)
    setVisible(state, true);
}

// ---- event sinks ----

int onNotice(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNoticeEvent *>(payload);
  NoticeState *state = event->target == Notify.target   ? &Notify
                       : event->target == Center.target ? &Center
                                                        : nullptr;
  if (!state || !Api->ui->panelDocument(state->Panel.name))
    return 0;
  showMessage(*state, event->text);
  return 1;
}

void onHideToken(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiIntEvent *>(payload);
  Notify.hideAll = (event->value & ABCUI_HIDEBIT_ALL) != 0;
  Center.hideAll = Notify.hideAll;
  updateHidden(Notify);
  updateHidden(Center);
}

// ---- panel callbacks ----

void panelOnData(void *userdata, double frameTime) {
  NoticeState &state = *stateOf(userdata);
  state.clientTimeNow += frameTime;
  if (state.closeTime > 0.0 && state.clientTimeNow > state.closeTime) {
    state.closeTime = 0.0;
    setVisible(state, false);
  }
}

void panelReset(void *userdata) {
  // Old Reset: drop the countdown and hide.
  NoticeState &state = *stateOf(userdata);
  state.closeTime = 0.0;
  setVisible(state, false);
}

void registerPanel(NoticeState &state, const char *name, const char *document,
                   int target) {
  state.target = target;
  state.Panel.structSize = sizeof(state.Panel);
  state.Panel.name = name;
  state.Panel.documentPath = document;
  state.Panel.zIndex = 0;
  state.Panel.userdata = &state;
  state.Panel.onDocumentReady = nullptr;
  state.Panel.onData = &panelOnData;
  state.Panel.onRedraw = nullptr;
  state.Panel.reset = &panelReset;
  Api->panels->registerPanel(&state.Panel);
}

// ---- lifecycle ----

void onModuleLoad() {
  registerPanel(Notify, "notice", "mod://abcui.textfeed/hud/rml/notice.rml", 1);
  registerPanel(Center, "notice-center",
                "mod://abcui.textfeed/hud/rml/notice_center.rml", 4);

  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  Notify.hideAll = (state.hideBits & ABCUI_HIDEBIT_ALL) != 0;
  Center.hideAll = Notify.hideAll;

  Api->events->subscribeVeto(ABCUI_CHANNEL_TEXTFEED_NOTICE, 0, &onNotice,
                             nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_HIDE_TOKEN, 0, &onHideToken,
                         nullptr);
}

void onModuleShutdown() {
  // The documents die with the context; forget the countdowns so the next
  // cycle starts hidden.
  Notify.closeTime = 0.0;
  Center.closeTime = 0.0;
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.textfeed";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = nullptr;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
