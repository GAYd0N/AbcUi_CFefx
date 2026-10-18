// services::EngineUiBackend implementation - see EngineUiBackend.h.
#include "EngineUiBackend.h"

#include "loading/LoadingModel.h"

#include <abc/Console.h>

#include <cstdio>
#include <cstring>

namespace services {

namespace {

// Password charset (see the header): printable ASCII, no space (the command
// takes one argument), no quote/backslash (delimiter escapes) and no
// semicolon (the engine command separator) - the value can never act as
// anything but the single password argument.
bool validPasswordChar(char c) {
  if (c <= 0x20 || c > 0x7E)
    return false;
  return c != '"' && c != '\\' && c != ';';
}

} // namespace

void EngineUiBackend::connect(uint32_t generation, const char *address) {
  // Fail closed: the address must be the validated "host[:port]" grammar
  // before it may join the command channel as a payload.
  std::string host;
  int port = 0;
  if (!loading::LoadingModel::parseAddress(address, host, port)) {
    abc::ConsoleWarn("[AbcUi] connect refused (invalid address), gen=%u\n",
                     generation);
    return;
  }
  const std::string command = std::string("connect ") + address + "\n";
  clientCmd(command.c_str());
}

void EngineUiBackend::startLocalGame(uint32_t generation, const char *map) {
  // One launch per generation (the module's request validation runs before
  // the controller dispatch; this is the engine-edge latch behind the
  // page's disabled state and the controller's single transition).
  if (generation == 0 || !map || !*map) {
    abc::ConsoleWarn("[AbcUi] local game refused (empty target), gen=%u\n",
                     generation);
    return;
  }
  if (!Launcher) {
    // A wiring bug, not a user path: refuse WITHOUT burning the generation
    // (a later re-attempt of the same live attempt still runs).
    abc::ConsoleWarn("[AbcUi] local game launcher seam missing, gen=%u\n",
                     generation);
    return;
  }
  if (generation == LocalGameGeneration) {
    abc::ConsoleWarn("[AbcUi] duplicate local game launch dropped (gen=%u)\n",
                     generation);
    return;
  }
  LocalGameGeneration = generation;
  // The module thunk owns the request value object and the launcher; it
  // runs the shipped sequence synchronously and answers with a fixed reason
  // token on refusal. Nothing here declares success - the engine's loading
  // stream owns the outcome (T19 step 5).
  const char *reason = nullptr;
  if (!Launcher(map, &reason)) {
    abc::ConsoleWarn("[AbcUi] local game launch refused (gen=%u reason=%s)\n",
                     generation, reason ? reason : "(unset)");
  }
}

void EngineUiBackend::cancelConnect(uint32_t generation) {
  if (generation == 0 || cancelIssued(generation)) {
    abc::ConsoleWarn("[AbcUi] duplicate connect cancel dropped (gen=%u)\n",
                     generation);
    return;
  }
  CanceledGeneration = generation;
  // The vanilla LoadingDialog Cancel button's verbatim command (0x10007c00).
  clientCmd("disconnect\n");
}

void EngineUiBackend::submitPassword(uint32_t generation,
                                     const char *password) {
  if (generation == 0 || !password || !*password)
    return;
  const size_t length = std::strlen(password);
  if (length > 63) {
    abc::ConsoleWarn("[AbcUi] password refused (over 63 bytes), gen=%u\n",
                     generation);
    return;
  }
  for (const char *it = password; *it; ++it) {
    if (!validPasswordChar(*it)) {
      abc::ConsoleWarn(
          "[AbcUi] password refused (unsupported character), gen=%u\n",
          generation);
      return;
    }
  }
  // The vanilla ServerPasswordDialog Connect handler's verbatim command
  // (GameUI.dll 0x1000bca0). Never stored, never logged: the password lives
  // only inside this call and the std::string command it builds.
  const std::string command = std::string("password ") + password + "\nretry\n";
  clientCmd(command.c_str());
}

void EngineUiBackend::disconnectFromGame() {
  // The original Disconnect menu command, verbatim (GameUI.dll 0x1000cc30).
  clientCmd("disconnect\n");
}

void EngineUiBackend::quitGame() {
  if (QuitIssued)
    return; // one execution, whatever arrives afterwards (T09 step 3)
  QuitIssued = true;
  // The confirmed branch of the original Quit flow (QuitNoConfirm).
  clientCmd("quit\n");
}

void EngineUiBackend::resumeGame() {
  // Vanilla ResumeGame = IBaseUI::HideGameUI, never a command. Lower the
  // bridge first: the outer hide re-enters the (suppressed) inner dispatch,
  // which must find the menu already down so no routed-close echo fires.
  if (Seams_.bridge)
    Seams_.bridge->requestMenuDown(NativeUiOrigin::kOwnUi);
  if (Seams_.hideGameUi)
    Seams_.hideGameUi(); // engine root side effects (warn when absent)
  else
    abc::ConsoleWarn("[AbcUi] hideGameUi seam missing - resume skips the "
                     "engine-side hide\n");
  // The vanilla inner HideGameUI unpause rides here (singleplayer only):
  // only a DECIDED close restores the simulation. Force-releases (focus
  // loss) and the loading-end auto-hide keep the pause state untouched.
  menuHiddenInGame();
}

void EngineUiBackend::menuShownInGame() {
  // Vanilla ActivateGameUI inner body (0x100061d0), singleplayer only.
  if (singlePlayer())
    clientCmd("setpause\n");
}

void EngineUiBackend::menuHiddenInGame() {
  // Vanilla HideGameUI inner body minus hideconsole (0x10006420; the legacy
  // console it targets stays suppressed until T16 replaces it).
  if (singlePlayer())
    clientCmd("unpause\n");
}

bool EngineUiBackend::singlePlayer() const {
  return Seams_.maxClients && Seams_.maxClients() == 1;
}

void EngineUiBackend::clientCmd(const char *command) {
  if (!Seams_.clientCmd) {
    abc::ConsoleWarn("[AbcUi] engine command seam missing, dropped \"%s\"\n",
                     command);
    return;
  }
  Seams_.clientCmd(command);
}

} // namespace services
