// AbcUi - RmlUi HUD plugin (plan 05). Plugin entry only: CAbcPluginBase
// derivation + EXPOSE_SINGLE_INTERFACE. All assembly lives in rmlui/ (RmlUi
// integration) and framework/ (panel framework).
//
// The metahook globals are defined per plugin in src/CoreGlobals.cpp;
// CAbcPluginBase copies them into AbcCore.dll through the versioned C ABI
// interface (AbcCore_CreateInterface) during Init/LoadEngine/LoadClient.
#include <metahook.h>

#include <abc/Console.h>
#include <abc/Plugin.h>

#include "hooks.h"
#include "modsys/FrameworkVersion.h"
#include "modsys/ModuleManager.h"
#include "render/Render.h"
#include "rmlui/RmlUiModule.h"
#include "services/steam/SteamAPI.h"
#include "rmlui/VGUI2ExtensionImport.h"

#ifndef NDEBUG
// Debug-build assert routing (headless acceptance hardening): the CRT's
// default assert report pops a MODAL dialog - the main loop stalls and the
// RCON acceptance channel dies with no visible cause (P6 audit round). The
// hook prints the report plus a resolved stack to the game console instead
// and suppresses the dialog. Release builds keep the CRT default.
#include <crtdbg.h>
#include <dbghelp.h>
#include <filesystem>

#pragma comment(lib, "dbghelp.lib")

constexpr unsigned int kStackFrames = 16;

int debugAssertReportHook([[maybe_unused]] int reportType, char *message,
                          int *returnValue) {
  abc::ConsolePrint("[AbcUi][crt] %s\n", message ? message : "(no message)");

  // The same lines to a file: this assert class fail-fasts after the hook
  // returns, killing the console channel with it (P6 audit round).
  FILE *trace = nullptr;
  std::error_code pathError;
  const auto directory = std::filesystem::temp_directory_path(pathError);
  if (!pathError)
    _wfopen_s(&trace, (directory / L"abcui-assert-stack.log").c_str(), L"a");
  if (trace)
    fprintf(trace, "=== %s", message ? message : "(no message)");

  void *frames[kStackFrames] = {};
  const WORD count = RtlCaptureStackBackTrace(1, kStackFrames, frames, nullptr);
  static bool symbolsReady = []() {
    return SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  }();
  for (WORD i = 0; i < count; i++) {
    char line[320];
    DWORD64 displacement = 0;
    SYMBOL_INFO *symbol =
        static_cast<SYMBOL_INFO *>(alloca(sizeof(SYMBOL_INFO) + 128));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 127;
    if (symbolsReady &&
        SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[i]),
                    &displacement, symbol)) {
      IMAGEHLP_LINEW64 srcLine{};
      srcLine.SizeOfStruct = sizeof(srcLine);
      DWORD lineDisplacement = 0;
      if (symbolsReady &&
          SymGetLineFromAddrW64(GetCurrentProcess(),
                                reinterpret_cast<DWORD64>(frames[i]),
                                &lineDisplacement, &srcLine))
        sprintf_s(line, "  %2u %s+0x%llx (%ws:%lu)", i, symbol->Name,
                  static_cast<unsigned long long>(displacement),
                  srcLine.FileName ? srcLine.FileName : L"?",
                  srcLine.LineNumber);
      else
        sprintf_s(line, "  %2u %s+0x%llx", i, symbol->Name,
                  static_cast<unsigned long long>(displacement));
    } else
      sprintf_s(line, "  %2u %p", i, frames[i]);
    abc::ConsolePrint("[AbcUi][crt]%s\n", line);
    if (trace) {
      fprintf(trace, "%s\n", line);
      fflush(trace);
    }
  }
  if (trace)
    fclose(trace);
  if (returnValue)
    *returnValue = 0;
  return TRUE; // handled - no dialog, execution continues
}

void installDebugAssertHook() {
  _CrtSetReportHook(debugAssertReportHook);
}
#endif // NDEBUG

class CAbcUi : public abc::CAbcPluginBase {
public:
  CAbcUi() : abc::CAbcPluginBase("AbcUi", framework::kFrameworkVersion) {}

protected:
  void OnLoadClient(cl_exportfuncs_t *pExportFuncs) override {
#ifndef NDEBUG
    installDebugAssertHook();
#endif
    // Fatal Renderer check + interface grab (plan 05 §2 startup order).
    // Rml assembly itself defers to the first HUD_Frame (see
    // RmlUiModule.h): the VGUI surface texture channel the font engine
    // needs is not usable this early (Phase 3 trace finding).
    abc::MetaRendererInit();
    vgui2ext::init();
    steamapi::init();
    hooks::locateVanillaHudList();
    hooks::locateNativeHudPanelInfo();
    hooks::locatePlayerColor();
    hooks::hookVanillaMenuRoute();
    hooks::attachExports(pExportFuncs);
    abc::ConsolePrint("[AbcUi] loaded %s\n", Version());
  }

  void OnShutdown() override {
    rmlui::shutdown();
    // Modules release what they own only after Rml is down (their documents
    // died with the context; onShutdown already ran through the registry).
    framework::modmanager::unloadAll();
    steamapi::shutdown();
    vgui2ext::shutdown();
    // T08: the native-UI takeover callbacks and any still-installed
    // paint gate come down last (plugin teardown only - the HUD-cycle
    // shutdown() above must not drop them, or map changes would leave
    // suppression gaps).
    vgui2ext::shutdownNative();
  }
};

EXPOSE_SINGLE_INTERFACE(CAbcUi, IPluginsV4, METAHOOK_PLUGIN_API_VERSION_V4);
