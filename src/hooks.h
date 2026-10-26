#pragma once

// AbcUi hook spine: chains the client exports this plugin drives into the
// export table (R-2 chain semantics, same shape as AbcEssential src/hooks.cpp).
// Consumed exports: HUD_Init (command registration), HUD_VidInit (context
// resize), HUD_Frame (Rml update pump + input poll), HUD_Redraw (vanilla HUD
// suppression + the onRedraw pump for world-projection panels + the
// render-backend redraw pump), HUD_Key_Event
// (keyboard into Rml), HUD_UpdateClientData (fov capture),
// HUD_TxferPredictionData / HUD_TxferLocalOverrides (clientdata capture),
// HUD_AddEntity (grenade tracking), HUD_Shutdown (teardown). Rendering is NOT
// driven from an export: the Rml render pass runs in the render backend's
// frame beat (see rmlui/RmlUiModule) - exactly once per frame, right before
// the final buffer is blitted to screen.
//
// No cvar/command registration may happen before HUD_Init (plan 08 §4.9).

#include <metahook.h>
#include "data/NativePlayerInfo.h"

// Batch C: the engine studio API (player mirror reads) - captured from the
// HUD_GetStudioModelInterface chain (old exportfuncs.cpp gEngineStudio).
// Valid after LoadClient; the struct is the engine's, never written.
struct engine_studio_api_s;

namespace hooks {

// LoadClient time: chain our HUD_XXX implementations into the export table.
void attachExports(cl_exportfuncs_t *pExportFuncs);
// LoadClient time: locate the client's vanilla HUD list (gamedata; fatal on
// miss like the old Sig_NotFound). See the suppression note in hooks.cpp.
void locateVanillaHudList();
// LoadClient time: locate the client's native HUD panel info array (fatal on
// miss - same Sig_NotFound policy as the old FillAddress).
void locateNativeHudPanelInfo();
// LoadClient time: locate the client's GetClientColor accessor (fatal on
// miss; the scoreboard/playerboard tints ride it).
void locatePlayerColor();
// LoadClient time: inline-hook the client's ShowVGUIMenu (gamedata, fatal on
// miss) so the vanilla mission-brief/MOTD windows yield to the new motd
// panel (see the hook in hooks.cpp for the menu id gate).
void hookVanillaMenuRoute();
// HUD_Shutdown time: remove the ShowVGUIMenu hook (old UninstallClientHook).
void unhookVanillaMenuRoute();

// Direct-call captures (09 §3.4): data-layer read-only access.
extern engine_studio_api_s *EngineStudioApi;
extern NativePlayerInfoView NativeHudPanelInfo;
// Client dll's GetClientColor(index) - the per-player tint (float[3] 0..1).
// Gamedata-resolved; null = engineless gray.
using GetClientColorFn = float *(*)(int index);
extern GetClientColorFn GetClientColor;

} // namespace hooks
