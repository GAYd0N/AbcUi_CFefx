// rmlui::steamapi bridge backend - MetaHookSv SteamAPIBridge.dll (C ABI).
// The upstream bridge currently ships SteamID/screenshot/HTTP only; this
// backend reports its real coverage so the selector (SteamAPI.cpp) skips it
// until the bridge carries persona+avatar too - at which point it takes over
// without a consumer-side change (user directive: keep the swap door open).
// from: upstream include/SteamAPIBridge.h (probed dynamically, no import lib).

#include "SteamApiBackend.h"

#include <Windows.h>

namespace steamapi {

namespace {

using SteamBridgeContextHandle = void *;
using FnCreateContext = SteamBridgeContextHandle (*)();
using FnDestroyContext = void (*)(SteamBridgeContextHandle);
using FnGetSteamID = int(__cdecl *)(SteamBridgeContextHandle, SteamId64 *);

struct BridgeExports {
  FnCreateContext createContext = nullptr;
  FnDestroyContext destroyContext = nullptr;
  FnGetSteamID getSteamId = nullptr;

  bool fullCoverage() const {
    // Upstream ships SteamID/screenshot/HTTP only - there are no
    // persona/avatar operations to bind (upstream surface checked
    // 2026-10-11). Until those bindings exist this stays false and the
    // native backend serves; extend here when the bridge grows the
    // Friends surface and it takes over through the selector.
    return false;
  }
};

BridgeExports Bridge;
SteamBridgeContextHandle Context = nullptr;
SteamId64 CachedLocalId = 0;

} // namespace

SteamApiBackend *bridgeBackend() {
  static struct Backend final : SteamApiBackend {
    bool connect() override {
      const HMODULE module = GetModuleHandleA("SteamAPIBridge.dll");
      if (!module)
        return false;
      Bridge.createContext = reinterpret_cast<FnCreateContext>(
          GetProcAddress(module, "SteamBridge_CreateContext"));
      Bridge.destroyContext = reinterpret_cast<FnDestroyContext>(
          GetProcAddress(module, "SteamBridge_DestroyContext"));
      Bridge.getSteamId = reinterpret_cast<FnGetSteamID>(
          GetProcAddress(module, "SteamBridge_GetSteamID"));
      if (!Bridge.fullCoverage())
        return false;
      Context = Bridge.createContext();
      if (Context)
        Bridge.getSteamId(Context, &CachedLocalId);
      return Context != nullptr;
    }

    steamapi::Backend kind() override {
      return steamapi::Backend::Bridge;
    }

    SteamId64 localSteamId() override {
      return CachedLocalId;
    }

    std::string personaName(SteamId64) override {
      return {};
    }

    bool avatar(SteamId64, AvatarRgba &) override {
      return false;
    }

    AvatarAnimationState animatedAvatar(SteamId64, AvatarAnimation &) override {
      // The bridge C ABI has no animated-avatar surface yet; the native
      // backend keeps serving and the scoreboard keeps its static avatar.
      return AvatarAnimationState::Unavailable;
    }

    void abandonAnimatedAvatar(SteamId64) override {}
  } instance;
  return &instance;
}

} // namespace steamapi
