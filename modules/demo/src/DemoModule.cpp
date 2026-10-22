// abcui.demo - the package-load smoke module. Proves the whole chain once:
// zip package -> manifest validation -> VFS mount -> in-memory DLL load ->
// AbcUiModule_Main entry -> panel registration -> document load through
// mod:// (the "document ready" console line). Not a feature and not part of
// dist; third-party module authors should copy this shape.
#include <abcui/module_api.h>

static const AbcUiModuleApi *Api = nullptr;
static AbcUiPanelDesc DemoPanel = {};

static void DemoOnDocumentReady(void *userdata) {
  Api->core->print("panel '%s' document ready (mod:// chain ok)\n",
                   static_cast<const char *>(userdata));
}

static void DemoOnLoad() {
  DemoPanel.structSize = sizeof(DemoPanel);
  DemoPanel.name = "abcui-demo";
  DemoPanel.documentPath = "mod://abcui.demo/hud/rml/demo.rml";
  DemoPanel.zIndex = 0;
  DemoPanel.userdata = const_cast<char *>(DemoPanel.name);
  DemoPanel.onDocumentReady = &DemoOnDocumentReady;
  Api->panels->registerPanel(&DemoPanel);
  Api->core->print("panel registered\n");
}

ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
                                         const AbcUiModuleApi *api,
                                         AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api || !out)
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels)
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.demo";
  out->version = "1.0.0";
  out->onLoad = &DemoOnLoad;
  out->onInit = nullptr;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = nullptr;
  out->onUnload = nullptr;
  return 1;
}
