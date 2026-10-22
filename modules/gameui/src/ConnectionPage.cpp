// The T14 connection surfaces (moved from panels::loading): the loading
// page, the connection-error overlay, the password dialog and the
// direct-connect dialog. One TU, four module documents, each presenting
// from the menu state (the page from the top-page fact, the dialogs from
// the overlay bitmask).
//
// The loading page projects the drained attempt model (the sdk connection
// section) with the T14 honesty rules: unknown totals stay visibly unknown
// (no fill, no percentage - never a fabricated animation), and the title/
// status wording keys on the attempt kind riding the state payload. The
// cancel entry reflects the model's once-only latch.
//
// The error overlay shows the controller's owned reason (resolved through
// the language tables and escaped before the DOM); Retry starts a NEW
// attempt generation, Return goes back to the no-map home. The password
// dialog's value travels only into the submit call (never stored, never
// logged); the lead line keys on the model's per-generation prompt count.
// The direct-connect dialog validates with the model's grammar inside the
// section - a refused entry shows the inline hint and stays put.
#include "PageShared.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace connectionpage {
namespace {

constexpr const char *kLoadingName = "menuloading";
constexpr const char *kLoadingPath =
    "mod://abcui.gameui/gameui/rml/loading.rml";
constexpr const char *kErrorName = "menuconnerror";
constexpr const char *kErrorPath =
    "mod://abcui.gameui/gameui/rml/connerror.rml";
constexpr const char *kPasswordName = "menupassword";
constexpr const char *kPasswordPath =
    "mod://abcui.gameui/gameui/rml/password.rml";
constexpr const char *kDirectName = "menudirectconnect";
constexpr const char *kDirectPath =
    "mod://abcui.gameui/gameui/rml/directconnect.rml";

// The document z-index tiers (the frozen T14 record): loading above the
// page shells and the console; the three dialogs above it.
constexpr int kLoadingZIndex = 500;
constexpr int kConnectionDialogZIndex = 550;

AbcUiDocument *LoadingPage = nullptr;
AbcUiDocument *ErrorPage = nullptr;
AbcUiDocument *PasswordPage = nullptr;
AbcUiDocument *DirectPage = nullptr;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;

// The dialogs' fresh-open preparation (each prepare clears the fields
// exactly once per opening; repeated syncs are idempotent shows).
bool ErrorShown = false;
std::string ErrorReasonShown;
bool PasswordPrepared = false;
bool LastPasswordInvalid = false;
bool DirectPrepared = false;

AbcUiElement *nodeOf(AbcUiDocument *page, const char *id) {
  return page ? Api->ui->querySelector(page, id) : nullptr;
}

void setInnerRml(AbcUiDocument *page, const char *id, const std::string &rml) {
  if (AbcUiElement *element = nodeOf(page, id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

void setDisplay(AbcUiDocument *page, const char *id, bool visible) {
  if (AbcUiElement *element = nodeOf(page, id))
    Api->ui->elementSetProperty(element, "display", visible ? "block" : "none");
}

std::string inputValue(AbcUiDocument *page, const char *id) {
  AbcUiElement *control = nodeOf(page, id);
  if (!control)
    return {};
  const int needed = Api->ui->elementValue(control, nullptr, 0);
  if (needed <= 0)
    return {};
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->elementValue(control, out.data(), static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

void setInputValue(AbcUiDocument *page, const char *id, const std::string &v) {
  if (AbcUiElement *control = nodeOf(page, id))
    Api->ui->elementSetValue(control, v.c_str());
}

std::string resolveText(const std::string &raw) {
  if (raw.empty())
    return {};
  if (raw[0] != '#')
    return raw;
  return translate(raw.c_str() + 1);
}

std::string percentText(float fraction) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%.0f%%",
                std::clamp(fraction, 0.0f, 1.0f) * 100.0f);
  return buffer;
}

// --- the loading page --------------------------------------------------------

void refocusLoading() {
  if (AbcUiElement *cancel = nodeOf(LoadingPage, "loading-cancel"))
    Api->ui->elementFocus(cancel);
}

void renderLoading() {
  if (!LoadingPage || !Api->documents->visible(LoadingPage) || !Api->connection)
    return;

  // Title: the attempt-kind token from the state payload (the T19 local
  // start / an engine map change), else the shipped connect title. Always
  // re-applied so a reused page cannot keep the previous attempt's title.
  const char *titleToken = "#Loading_Title";
  const char *statusFallback = "#Loading_Connecting";
  const AbcUiMenuState state = [&] {
    AbcUiMenuState out{};
    out.structSize = sizeof(out);
    Api->menu->state(&out);
    return out;
  }();
  if (state.attemptKind == ABCUI_MENU_ATTEMPT_LOCAL_GAME) {
    titleToken = "#CG_LoadingTitle";
  } else if (state.attemptKind == ABCUI_MENU_ATTEMPT_MAP_LOAD) {
    titleToken = "#Loading_MapTitle";
    statusFallback = "#Loading_MapStatus";
  }
  setInnerRml(LoadingPage, "loading-title",
              escapeText(resolveText(titleToken)));

  // Status line: the engine truth when it has spoken, else the attempt's
  // own default (the few frames before the first callback). A map load
  // must not claim it is contacting a server.
  char text[512];
  text[0] = '\0';
  Api->connection->statusText(text, sizeof(text));
  const std::string status =
      text[0] ? resolveText(text) : resolveText(statusFallback);
  setInnerRml(LoadingPage, "loading-status", escapeText(status));

  text[0] = '\0';
  Api->connection->address(text, sizeof(text));
  setInnerRml(LoadingPage, "loading-target", escapeText(text));

  text[0] = '\0';
  Api->connection->resourceText(text, sizeof(text));
  const std::string resource = resolveText(text);
  setInnerRml(LoadingPage, "loading-resource", escapeText(resource));
  setDisplay(LoadingPage, "loading-resource", !resource.empty());

  // Primary channel: honest unknown stays hidden (no fake fill).
  const bool known = Api->connection->primaryKnown() != 0;
  setDisplay(LoadingPage, "loading-progress", known);
  setDisplay(LoadingPage, "loading-progress-text", known);
  if (known) {
    const float fraction = Api->connection->primaryFraction();
    if (AbcUiElement *bar = nodeOf(LoadingPage, "loading-progress"))
      Api->ui->elementProgressValue(bar, std::clamp(fraction, 0.0f, 1.0f));
    setInnerRml(LoadingPage, "loading-progress-text",
                escapeText(percentText(fraction)));
  }

  // Secondary channel: shows only once the engine drives it.
  const std::string secondaryTextRaw = [&] {
    char buffer[512];
    buffer[0] = '\0';
    Api->connection->secondaryText(buffer, sizeof(buffer));
    return std::string(buffer);
  }();
  const float secondaryFraction = Api->connection->secondaryFraction();
  const bool secondaryShown =
      secondaryFraction > 0.0f || !secondaryTextRaw.empty();
  setDisplay(LoadingPage, "loading-secondary", secondaryShown);
  setDisplay(LoadingPage, "loading-secondary-text", secondaryShown);
  if (secondaryShown) {
    if (AbcUiElement *bar = nodeOf(LoadingPage, "loading-secondary"))
      Api->ui->elementProgressValue(bar,
                                    std::clamp(secondaryFraction, 0.0f, 1.0f));
    const std::string value = secondaryTextRaw.empty()
                                  ? percentText(secondaryFraction)
                                  : resolveText(secondaryTextRaw);
    setInnerRml(LoadingPage, "loading-secondary-text", escapeText(value));
  }

  // The cancel entry reflects the model's once-only latch: exactly one
  // termination per attempt; the visual state makes the one-frame
  // double-click window honest.
  if (AbcUiElement *cancel = nodeOf(LoadingPage, "loading-cancel"))
    Api->ui->elementSetClass(cancel, "loading-disabled",
                             Api->connection->canceled() ? 1 : 0);
}

// --- the connection dialogs
// ----------------------------------------------------

void showError() {
  if (!ErrorPage || !Api->connection)
    return;
  char reason[512];
  reason[0] = '\0';
  Api->connection->errorReason(reason, sizeof(reason));
  const std::string key = reason;
  // The reason is the controller's owned copy: re-project only when it
  // moved (or on the first show).
  if (!ErrorShown || key != ErrorReasonShown) {
    ErrorReasonShown = key;
    const std::string text =
        reason[0] ? resolveText(reason) : resolveText("#Loading_ErrorGeneric");
    setInnerRml(ErrorPage, "connerror-reason", escapeText(text));
    ErrorShown = true;
  }
  Api->documents->show(ErrorPage);
  if (AbcUiElement *fallback = nodeOf(ErrorPage, "connerror-return"))
    Api->ui->elementFocus(fallback);
}

void hideError() {
  ErrorShown = false;
  ErrorReasonShown.clear();
  if (ErrorPage && Api->documents->visible(ErrorPage))
    Api->documents->hide(ErrorPage);
}

// Clears the field and updates the lead line; called once per opening
// (`invalid` = the engine asked again after a rejected password).
void preparePassword() {
  const bool invalid = Api->connection->passwordInvalid() != 0;
  setInnerRml(PasswordPage, "password-lead",
              escapeText(resolveText(invalid ? "#Loading_PasswordInvalid"
                                             : "#Loading_PasswordRequired")));
  setInnerRml(PasswordPage, "password-hint", "");
  setInputValue(PasswordPage, "password-field", "");
}

void showPassword() {
  if (!PasswordPage || !Api->connection)
    return;
  // Re-prepare when a new prompt landed (the field clears - the rejected
  // password must not survive into the retry). The lead line keys on the
  // model's per-generation prompt count, read through the section.
  const bool invalid = Api->connection->passwordInvalid() != 0;
  const bool newPrompt = !PasswordPrepared || invalid != LastPasswordInvalid;
  if (newPrompt) {
    LastPasswordInvalid = invalid;
    PasswordPrepared = true;
    preparePassword();
  }
  Api->documents->show(PasswordPage);
  if (AbcUiElement *field = nodeOf(PasswordPage, "password-field"))
    Api->ui->elementFocus(field);
}

void hidePassword() {
  PasswordPrepared = false;
  LastPasswordInvalid = false;
  if (PasswordPage && Api->documents->visible(PasswordPage))
    Api->documents->hide(PasswordPage);
}

void showDirect() {
  if (!DirectPage || !Api->connection)
    return;
  if (!DirectPrepared) {
    DirectPrepared = true;
    setInputValue(DirectPage, "directconnect-field", "");
    setInnerRml(DirectPage, "directconnect-hint", "");
  }
  Api->documents->show(DirectPage);
  if (AbcUiElement *field = nodeOf(DirectPage, "directconnect-field"))
    Api->ui->elementFocus(field);
}

void hideDirect() {
  DirectPrepared = false;
  if (DirectPage && Api->documents->visible(DirectPage))
    Api->documents->hide(DirectPage);
}

// --- event handlers ---------------------------------------------------------

int32_t onLoadingClick(void *, AbcUiElement *target,
                       const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click"))
    return 0;
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (action != "loading-cancel")
    return 0;
  // The model latch is the once-only guard: a double-click is honest dead
  // air, not a second termination.
  Api->connection->cancelLoading();
  return 1;
}

int32_t onErrorClick(void *, AbcUiElement *target,
                     const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click"))
    return 0;
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (action == "error-retry") {
    Api->connection->retryFailed(); // a NEW attempt generation
    return 1;
  }
  if (action == "error-return") {
    Api->connection->returnHome(); // back to the no-map home
    return 1;
  }
  return 0;
}

int32_t onPasswordClick(void *, AbcUiElement *target,
                        const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click"))
    return 0;
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (action == "password-connect") {
    // The typed password travels only into the submit call (never stored,
    // never logged); empty is not a typed intent.
    Api->connection->submitPassword(
        inputValue(PasswordPage, "password-field").c_str());
    return 1;
  }
  if (action == "password-cancel") {
    // Vanilla parity: the attempt survives; the loading page's Cancel is
    // the termination path.
    Api->connection->passwordCancel();
    return 1;
  }
  return 0;
}

int32_t onDirectClick(void *, AbcUiElement *target,
                      const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click"))
    return 0;
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (action == "directconnect-connect") {
    // Validated inside the section (the model's grammar); a refused entry
    // shows the inline hint and stays put.
    char error[128];
    error[0] = '\0';
    if (!Api->connection->connectAddress(
            inputValue(DirectPage, "directconnect-field").c_str(), error,
            sizeof(error))) {
      setInnerRml(DirectPage, "directconnect-hint",
                  escapeText(resolveText(error)));
    }
    return 1;
  }
  if (action == "directconnect-cancel") {
    Api->menu->back();
    return 1;
  }
  return 0;
}

// --- presentation
// -------------------------------------------------------------

void dismissAll() {
  if (LoadingPage && Api->documents->visible(LoadingPage))
    Api->documents->hide(LoadingPage);
  hideError();
  hidePassword();
  hideDirect();
}

bool wantsLoading(const AbcUiMenuState *state) {
  return state && state->up && state->page == ABCUI_MENU_PAGE_LOADING;
}

void onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!state || !Api->connection)
    return;
  if (wantsLoading(state)) {
    if (LoadingPage && !Api->documents->visible(LoadingPage)) {
      Api->documents->show(LoadingPage);
      refocusLoading();
    }
  } else if (LoadingPage && Api->documents->visible(LoadingPage)) {
    Api->documents->hide(LoadingPage);
  }

  const bool errorUp =
      (state->overlays & ABCUI_MENU_OVERLAY_CONNECTION_ERROR) != 0;
  if (errorUp)
    showError();
  else
    hideError();

  const bool passwordUp = (state->overlays & ABCUI_MENU_OVERLAY_PASSWORD) != 0;
  if (passwordUp)
    showPassword();
  else
    hidePassword();

  const bool directUp =
      (state->overlays & ABCUI_MENU_OVERLAY_DIRECT_CONNECT) != 0;
  if (directUp)
    showDirect();
  else
    hideDirect();
}

void onMenuPaint(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!state || !Api->connection)
    return;
  // The attempt's values move with the drained engine feed: the loading
  // page re-projects every frame while it is up.
  if (wantsLoading(state) && LoadingPage &&
      Api->documents->visible(LoadingPage))
    renderLoading();
}

void onDocumentReadyClose(void *) {}

} // namespace

void load() {
  StateSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_STATE, 0,
                                             &onMenuState, nullptr);
  PaintSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_PAINT, 0,
                                             &onMenuPaint, nullptr);

  const auto create = [](const char *name, const char *path, bool backdrop,
                         int zIndex, AbcUiElementListener listener) {
    AbcUiDocumentDesc desc{};
    desc.structSize = sizeof(desc);
    desc.name = name;
    desc.path = path;
    desc.flags = ABCUI_DOC_FLAG_MENU | (backdrop ? ABCUI_DOC_FLAG_BACKDROP : 0);
    desc.zIndex = zIndex;
    desc.userdata = nullptr;
    desc.onReady = &onDocumentReadyClose;
    desc.onShow = nullptr;
    desc.onHide = nullptr;
    desc.onClose = nullptr;
    (void)listener;
    return Api->documents->create(&desc);
  };
  LoadingPage =
      create(kLoadingName, kLoadingPath, true, kLoadingZIndex, nullptr);
  ErrorPage =
      create(kErrorName, kErrorPath, false, kConnectionDialogZIndex, nullptr);
  PasswordPage = create(kPasswordName, kPasswordPath, false,
                        kConnectionDialogZIndex, nullptr);
  DirectPage =
      create(kDirectName, kDirectPath, false, kConnectionDialogZIndex, nullptr);

  const auto bind = [](AbcUiDocument *page, AbcUiElementListener listener) {
    if (AbcUiElement *root = Api->documents->root(page))
      Api->ui->elementAddListener(root, "click", listener, nullptr);
  };
  bind(LoadingPage, &onLoadingClick);
  bind(ErrorPage, &onErrorClick);
  bind(PasswordPage, &onPasswordClick);
  bind(DirectPage, &onDirectClick);
}

void shutdown() {
  // The documents die with the context; forget every borrowed handle.
  LoadingPage = ErrorPage = PasswordPage = DirectPage = nullptr;
  ErrorShown = false;
  ErrorReasonShown.clear();
  PasswordPrepared = false;
  LastPasswordInvalid = false;
  DirectPrepared = false;
}

void unload() {
  if (StateSubscription) {
    Api->events->unsubscribe(StateSubscription);
    StateSubscription = 0;
  }
  if (PaintSubscription) {
    Api->events->unsubscribe(PaintSubscription);
    PaintSubscription = 0;
  }
  LoadingPage = ErrorPage = PasswordPage = DirectPage = nullptr;
}

} // namespace connectionpage
