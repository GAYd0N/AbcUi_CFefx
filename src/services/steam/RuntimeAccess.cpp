// Shared by server discovery/favorites and the avatar backend. A missing DLL
// or not-yet-initialized user is retried on the next call, never latched.
#include "RuntimeAccess.h"
#include <Windows.h>

namespace services::steam {

void *findUserInterface(const char *Version) {
  if (!Version)
    return nullptr;
  const HMODULE Module = GetModuleHandleA("steam_api.dll");
  if (!Module)
    return nullptr;
  using GetUserFn = int (*)();
  using FindInterfaceFn = void *(*)(int, const char *);
  const auto GetUser = reinterpret_cast<GetUserFn>(
      GetProcAddress(Module, "SteamAPI_GetHSteamUser"));
  const auto FindInterface = reinterpret_cast<FindInterfaceFn>(
      GetProcAddress(Module, "SteamInternal_FindOrCreateUserInterface"));
  if (!GetUser || !FindInterface)
    return nullptr;
  const int User = GetUser();
  return User ? FindInterface(User, Version) : nullptr;
}

} // namespace services::steam
