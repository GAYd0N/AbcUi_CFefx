#pragma once

// services::EngineUiBackend - the production IGameUiBackend (T09): executes
// the typed menu actions against the real engine while the state machine
// stays pure. Every engine call sits behind an injected seam so the exact
// production translation unit links into the offline acceptance test (05 doc
// tier 1); the module wires the thunks, the test records.
//
// Command semantics verified against the installed GameUI.dll (Ghidra,
// image base 0x10000000):
//   - ResumeGame falls through to IBaseUI::HideGameUI (0x1000cc30 ->
//     vtable slot 0x1c) - never a console command. resumeGame() therefore
//     lowers the bridge state first (so the suppressed inner dispatch finds
//     the menu already down and emits no routed-close echo) and then runs
//     the outer hide through the hooked vtable for the engine-root effects.
//   - Disconnect issues pfnClientCmd("disconnect\n") (0x1000cc30).
//   - Quit is a two-step flow: the menu-side confirm overlay owns the
//     decision (cancel never reaches here), and the confirmed branch is
//     pfnClientCmd("quit\n") (QuitNoConfirm, 0x1000cc30). The latch makes
//     the execution strictly once, regardless of how many resolved events
//     or clicks arrive.
//   - The vanilla ActivateGameUI/HideGameUI pair issue setpause/unpause
//     (0x100061d0/0x10006420, Hide connected-only) - the takeover suppresses
//     that inner layer, so the equivalent lives here: menuShownInGame/
//     menuHiddenInGame keep the parity for singleplayer only (maxclients==1;
//     T09 forbids an unconditional pause in multiplayer). hideconsole is not
//     replicated: the legacy console it targets is fully suppressed by the
//     takeover and the replacement console is T16.
//
// T14 wires the connect chain (all verified against the installed binaries):
//   - connect(generation, address): "connect <address>\n" - the engine's own
//     connect command (hw.dll string table; the shipped ServerBrowser builds
//     the same string). The address is re-validated here (fail closed) and
//     only ever travels as this one command's payload.
//   - cancelConnect(generation): "disconnect\n" - the verbatim command of the
//     vanilla LoadingDialog Cancel button (GameUI.dll CLoadingDialog handler
//     0x10007c00; its HTTP sub-state uses "httpstop\n" instead - that state
//     is not observable through the extension-proxied callbacks, recorded as
//     a T14 limitation). Once per generation: a repeated cancel for the same
//     attempt issues nothing.
//   - submitPassword(generation, password): "password %s\nretry\n" - the
//     verbatim command of the vanilla ServerPasswordDialog Connect handler
//     (GameUI.dll 0x1000bca0). The password is never stored and never logged;
//     it is charset-validated first (printable ASCII without space, quote,
//     backslash or semicolon, <= 63 bytes - the engine PasswordEntry limit)
//     so the value cannot escape its single command argument.
//
// T19 wires the local-game start:
//   - startLocalGame(generation, map): the validated map token is handed to
//     the create-game launcher seam injected by the module
//     (services::creategame::CreateGameModel wired with cvar/command thunks).
//     The launcher executes the shipped CCreateMultiplayerGameDialog::Apply
//     sequence (GameUI.dll FUN_10003a10 / format 0x1007f7a0) - disconnect,
//     sv_lan, setmaster, maxplayers, sv_password, hostname, cd fadeout,
//     map, wait - with per-write read-back and snapshot restore on failure.
//     This class only relays the typed call; generator/attempt lifecycle is
//     the controller's (T19 step 5: no success declared at command time).
//     A refused launch routes the SAME typed failure path a connect failure
//     uses (the caller feeds a kLoadingFinished(kFailed) with the reason),
//     never a silent no-op.

#include "GameUiBridge.h"

namespace services {

// T19: the launcher seam. The module injects a stateless thunk that owns the
// create-game request value object (the page's four fields, validated before
// the controller ever sees the intent) and runs the launcher; the backend only
// relays and enforces "one launch per generation". False = refused before or
// at a cvar write (snapshots already restored); `reasonToken` then names the
// error-overlay token, and the MODULE turns that into the typed
// kLoadingFinished(kFailed) - never this class.
using LocalGameLauncher = bool (*)(const char *map, const char **reasonToken);

class EngineUiBackend final : public IGameUiBackend {
public:
  // Engine seams. Plain function pointers, no captured state: production
  // wires thin thunks, the offline test records the calls.
  struct Seams {
    void (*clientCmd)(const char *command) = nullptr; // pfnClientCmd wrapper
    bool (*hideGameUi)() = nullptr;                   // vgui2ext::hideGameUi
    int (*maxClients)() = nullptr;    // live maxplayers (1 = singleplayer)
    NativeUiBridge *bridge = nullptr; // menu-state pre-lower on resume
  };

  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }
  // Wired separately because the thunk belongs to the module class (the
  // request value object is module-held); must be set before the first
  // local-game intent.
  void setLocalGameLauncher(LocalGameLauncher launcher) {
    Launcher = launcher;
  }

  // IGameUiBackend (the T04 contract, unchanged).
  void connect(uint32_t generation, const char *address) override;
  void startLocalGame(uint32_t generation, const char *map) override;
  void cancelConnect(uint32_t generation) override;
  void submitPassword(uint32_t generation, const char *password) override;
  void disconnectFromGame() override;
  void quitGame() override;
  void resumeGame() override;

  // Single-player pause parity (not part of the T04 interface on purpose:
  // the frozen contract predates the T09 pause decision). The module calls
  // these on the controller's in-game menu transitions.
  void menuShownInGame();
  void menuHiddenInGame();

  // The live maxplayers fact (1 = singleplayer), the same source the
  // pause parity reads; the module feeds the GameMenu.res "notsingle"
  // entry visibility from it. Defaults to 1 (fail closed: hide the
  // multiplayer-only entries) while the seam is unwired.
  int maxClientsFact() const {
    return Seams_.maxClients ? Seams_.maxClients() : 1;
  }

private:
  bool singlePlayer() const;
  void clientCmd(const char *command);
  // One cancel per generation: the controller retires the attempt after the
  // first, this latch is the second guard at the engine edge.
  bool cancelIssued(uint32_t generation) const {
    return generation != 0 && generation == CanceledGeneration;
  }

  Seams Seams_;
  // T19: the create-game launcher thunk (module-held request) and the
  // one-launch-per-generation latch.
  LocalGameLauncher Launcher = nullptr;
  uint32_t LocalGameGeneration = 0;
  // Terminal latch: once the confirmed quit left, nothing re-executes it.
  bool QuitIssued = false;
  // T14: generation of the last issued connect cancel (0 = none).
  uint32_t CanceledGeneration = 0;
};

} // namespace services
