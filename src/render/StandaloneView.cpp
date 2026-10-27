#include <metahook.h>
#include <IStandaloneView.h>

#include <abc/Console.h>
#include <abc/Plugin.h>

#include "StandaloneView.h"

namespace framework::standaloneview {

IStandaloneViewManager *manager() {
  // MetaHook loads all plugin modules before client/UI initialization.
  // plugins.lst must place StandaloneView first so it also outlives AbcUi.
  static IStandaloneViewManager *instance = []() -> IStandaloneViewManager * {
    mh_plugininfo_t info{};
    if (!g_pMetaHookAPI ||
        !g_pMetaHookAPI->GetPluginInfoByBaseFileName("StandaloneView", &info)) {
      abc::ConsoleWarn("[AbcUi model] StandaloneView plugin missing; "
                       "character previews disabled\n");
      return nullptr;
    }
    const auto factory =
        Sys_GetFactory(static_cast<HINTERFACEMODULE>(info.PluginModuleBase));
    auto *result =
        factory ? static_cast<IStandaloneViewManager *>(factory(
                      STANDALONEVIEW_MANAGER_INTERFACE_VERSION_V003, nullptr))
                : nullptr;
    if (!result)
      abc::ConsoleWarn("[AbcUi model] StandaloneView API_003 unavailable; "
                       "update StandaloneView and restart the game\n");
    else
      abc::ConsolePrint("[AbcUi model] using StandaloneView API_003\n");
    return result;
  }();
  return instance;
}

} // namespace framework::standaloneview
