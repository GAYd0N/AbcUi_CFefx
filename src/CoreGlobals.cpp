// Per-plugin metahook globals. AbcCore.dll keeps its own copy; CAbcPluginBase
// copies these host pointers into the shared library through the exported
// setters during Init/LoadEngine/LoadClient.
#include <metahook.h>

mh_interface_t *g_pInterface = nullptr;
cl_enginefunc_t gEngfuncs = {};
metahook_api_t *g_pMetaHookAPI = nullptr;
mh_enginesave_t *g_pMetaSave = nullptr;
cl_exportfuncs_t gExportfuncs = {};

namespace abc {
IFileSystem *g_pFileSystem = nullptr;
DWORD g_dwEngineBuildnum = 0;
int g_iEngineType = 0;
PVOID g_dwEngineBase = nullptr;
DWORD g_dwEngineSize = 0;
PVOID g_dwEngineTextBase = nullptr;
DWORD g_dwEngineTextSize = 0;
PVOID g_dwEngineDataBase = nullptr;
DWORD g_dwEngineDataSize = 0;
PVOID g_dwEngineRdataBase = nullptr;
DWORD g_dwEngineRdataSize = 0;
PVOID g_dwClientBase = nullptr;
DWORD g_dwClientSize = 0;
} // namespace abc
