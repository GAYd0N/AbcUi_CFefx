// services::playerlist::muteBackend implementation - see MuteBackend.h.
//
// Resolution path (the shipped one, through public engine plumbing): the
// MetaHook API exposes the client factory (GetClientFactory -> the client
// DLL's CreateInterface). Asking that factory for "GameClientExports001" is
// exactly what the factory protocol is for; the shipped CGameUI did it with
// the factory list it received. When the name is not served, the interface
// does not exist on this install and the surface stays disabled.
//
// Slot layout (shipped call order, GameUI.dll):
//   vtable slot 2 (+0x08) read      - "is this slot's player muted"
//   vtable slot 3 (+0x0C) mute
//   vtable slot 4 (+0x10) unmute
// All three take the 1-based entity slot as their single argument and return
// a bool-ish value (the shipped dialog tests the read's low byte; the two
// toggles are called for effect). The proxy below matches that shape.
#include "MuteBackend.h"
#include "ClientMuteApi.h"

#include <abc/Console.h>
#include <metahook.h>

#include <cstring>
#include <string>

namespace services {
namespace playerlist {

namespace {

// The interface's exported name and vtable shape. Kept as a minimal
// local declaration: the shipped header never exposed this interface publicly
// (it is GameUI-private), and the slots are pinned by the call sites above.
constexpr const char *kClientExportsInterface = "GameClientExports001";

using CreateInterfaceFn = void *(*)(const char *name, int *returnCode);
// One resolved probe. `object` null = unavailable.
struct Probe {
  bool Done = false;
  ClientMuteApi Api;
};

Probe &probe() {
  static Probe state;
  return state;
}

void resolve() {
  Probe &state = probe();
  if (state.Done)
    return;
  state.Done = true;
  if (!g_pMetaHookAPI || !g_pMetaHookAPI->GetClientFactory) {
    abc::ConsoleWarn(
        "[AbcUi] player mute backend unavailable (no client factory API)\n");
    return;
  }
  CreateInterfaceFn factory =
      reinterpret_cast<CreateInterfaceFn>(g_pMetaHookAPI->GetClientFactory());
  if (!factory) {
    abc::ConsoleWarn("[AbcUi] player mute backend unavailable (client factory "
                     "not served)\n");
    return;
  }
  int returnCode = 0;
  void *object = factory(kClientExportsInterface, &returnCode);
  if (!object) {
    // The shipped GameUI resolves this interface for its own player list;
    // an install whose client DLL does not serve the name has no shipped
    // mute backend either - the correct answer is "unavailable".
    //
    // Positive control: the SAME factory is asked for names the client DLL
    // is known to serve (its own interface strings, see the T20 evidence),
    // so the log distinguishes "the factory path is broken" from "this one
    // interface does not exist".
    static const char *kControlNames[] = {"SCClientDLL001", "VClientVGUI001"};
    std::string controls;
    for (const char *control : kControlNames) {
      int code = 0;
      const bool served = factory(control, &code) != nullptr;
      if (!controls.empty())
        controls += ',';
      controls += control;
      controls += served ? "=1" : "=0";
    }
    abc::ConsoleWarn("[AbcUi] player mute backend unavailable (client DLL does "
                     "not serve %s; factory control %s)\n",
                     kClientExportsInterface, controls.c_str());
    return;
  }
  // MSVC x86 vtable layout for IGameClientExports : IBaseInterface: slot 0
  // is the scalar deleting destructor, slot 1 is the first method (the
  // shipped +4 call returns the game-name string), and the mute methods sit
  // at slots 2/3/4 = +8/+0xc/+0x10 - exactly the call sites in
  // CPlayerListDialog (FUN_10015d70). Indices are the shipped +N/4 values.
  if (!state.Api.attach(object)) {
    abc::ConsoleWarn(
        "[AbcUi] player mute backend unavailable (incomplete vtable)\n");
    return;
  }
  abc::ConsolePrint("[AbcUi] player mute backend resolved (%s)\n",
                    kClientExportsInterface);
}

class Backend final : public IMuteBackend {
public:
  bool available() const override {
    const_cast<Backend *>(this)->ensure();
    return probe().Api.available();
  }
  bool isMuted(int index) const override {
    const_cast<Backend *>(this)->ensure();
    const Probe &state = probe();
    return state.Api.isMuted(index);
  }
  bool mute(int index) override {
    ensure();
    const Probe &state = probe();
    return state.Api.mute(index);
  }
  bool unmute(int index) override {
    ensure();
    const Probe &state = probe();
    return state.Api.unmute(index);
  }

private:
  void ensure() {
    resolve();
  }
};

} // namespace

IMuteBackend *muteBackend() {
  static Backend backend;
  return &backend;
}

} // namespace playerlist
} // namespace services
