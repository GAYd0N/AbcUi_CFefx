#pragma once

// sdk/abcui/module_api.h - the AbcUiModuleApi table and module descriptor.
//
// A module's DLL exports exactly one function:
//
//   ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
//                                            const AbcUiModuleApi *api,
//                                            AbcUiModuleDescriptor *out);
//
// It receives the framework service table and fills the descriptor. Return
// nonzero on success; a zero return (or an inconsistent descriptor) skips
// the module and everything that depends on it. Descriptor callbacks run on
// the engine main thread at the same lifecycle points the framework's
// built-in modules use (see AGENTS.md "UI module architecture").
// All service calls are main-thread-only. Worker results must be handed off
// through the module's main-thread callbacks, never registered from a worker.
// Entries/callbacks must not throw across this C ABI: manually mapped images
// do not guarantee cross-image C++ unwinding. Report expected startup errors
// through core->failInitialization and return from the callback normally.

#include <stdint.h>

#include "abi.h"

#pragma pack(push, 8)

#ifdef __cplusplus
extern "C" {
#endif

// Packed semantic version: (major << 16) | (minor << 8) | patch.
#define ABCUI_VERSION_PACK(major, minor, patch)                                \
  (((uint32_t)(major) << 16) | ((uint32_t)(minor) << 8) | (uint32_t)(patch))

typedef uint64_t AbcUiSubscription; // 0 is never issued.

// Data-channel sink: the payload pointer is only valid for the call.
typedef void(ABCUI_CALL *AbcUiEventSink)(void *userdata, const void *payload);
// Veto-channel sink: returning nonzero vetoes and stops the dispatch here.
typedef int(ABCUI_CALL *AbcUiEventVeto)(void *userdata, const void *payload);

// Console command callback (no arguments; use engine argv through the
// console api when needed).
typedef void(ABCUI_CALL *AbcUiCommandFn)(void *userdata);

// Synchronous module service handler. Requests/responses are byte buffers;
// exact service version defines their POD wire layout. No pointers, C++
// objects or ownership transfers inside these buffers. Return >= 0 for an
// application result; negative values are reserved for host errors.
typedef int32_t(ABCUI_CALL *AbcUiServiceHandler)(
    void *userdata, const void *request, uint32_t requestSize, void *response,
    uint32_t responseCapacity, uint32_t *responseSize);
typedef uint64_t AbcUiServiceRegistration;
#define ABCUI_SERVICE_UNAVAILABLE (-1)
#define ABCUI_SERVICE_FORBIDDEN (-2)
#define ABCUI_SERVICE_INVALID (-3)
#define ABCUI_SERVICE_DEPTH (-4)
#define ABCUI_SERVICE_FAILED (-5)

// ---------------------------------------------------------------- core ----

typedef struct AbcUiCoreApi {
  uint32_t structSize;
  uint32_t frameworkVersion; // ABCUI_VERSION_PACK of the running framework
  const char *frameworkVersionString;
  // Console output through the framework channel ("[module]" prefix). The
  // printf-style formatting happens framework-side.
  void(ABCUI_CALL *print)(const char *format, ...);
  void(ABCUI_CALL *warn)(const char *format, ...);
  void(ABCUI_CALL *error)(const char *format, ...);
  // AbcUi >= 0.8.1: reports a startup failure without throwing. Valid during
  // Main/onLoad/onInit (1 accepted, 0 outside startup/already reported).
  // Stops new registrations immediately; rollback occurs when the callback
  // returns. Existing engine side effects are not undone. Check availability
  // with ABCUI_API_HAS_MEMBER, or declare minAbcUi >= 0.8.1.
  int32_t(ABCUI_CALL *failInitialization)(const char *reason);
  // AbcUi >= 0.8.5: exact-version, owner-scoped collaboration. Local names
  // are [a-z0-9_]+ (1..64 bytes); provider is a manifest id. Register from
  // Main/onLoad/onInit, callable only after provider initialization. Callers
  // must declare provider in dependencies (self calls allowed). All handles
  // are process-scoped and auto-revoked. No raw provider function table leaks.
  AbcUiServiceRegistration(ABCUI_CALL *registerService)(
      const char *name, uint32_t version, AbcUiServiceHandler handler,
      void *userdata);
  void(ABCUI_CALL *unregisterService)(AbcUiServiceRegistration registration);
  int32_t(ABCUI_CALL *serviceAvailable)(const char *provider, const char *name,
                                        uint32_t version);
  // responseSize required; written even for a size probe (null response,
  // capacity 0). Each buffer/provided size <= 1 MiB, dispatch depth <= 16.
  // Handler writes only capacity bytes and may report a larger required size.
  int32_t(ABCUI_CALL *callService)(const char *provider, const char *name,
                                   uint32_t version, const void *request,
                                   uint32_t requestSize, void *response,
                                   uint32_t responseCapacity,
                                   uint32_t *responseSize);
} AbcUiCoreApi;

// ------------------------------------------------------------- console ----

typedef void *AbcUiCvarHandle; // opaque registered cvar

// Flags for console->registerCvar: mirrors AbcCore's abc::FCVAR_VALUE
// (FCVAR_PRINTABLEONLY | FCVAR_CLIENTDLL | FCVAR_ARCHIVE) so a module needs
// no AbcCore or engine header. Other engine flags are not part of the ABI.
#define ABCUI_CVAR_VALUE 0x00000091

typedef struct AbcUiConsoleApi {
  uint32_t structSize;
  // Registers a cvar and returns its handle (null when the engine refused).
  // Cvars survive HUD_Shutdown cycles; register from onInit.
  AbcUiCvarHandle(ABCUI_CALL *registerCvar)(const char *name, const char *value,
                                            int flags);
  float(ABCUI_CALL *cvarValue)(AbcUiCvarHandle cvar, float fallback);
  // Copies the cvar string (at most outSize-1 bytes + terminator).
  void(ABCUI_CALL *cvarString)(AbcUiCvarHandle cvar, char *out,
                               uint32_t outSize, const char *fallback);
  // Registers a console command. The same module can update its handler
  // without touching the engine table; another live owner's name is refused.
  void(ABCUI_CALL *registerCommand)(const char *name, AbcUiCommandFn fn,
                                    void *userdata);
  // Runs a client command through the engine command path (menus send
  // "menuselect 3" and friends through this).
  void(ABCUI_CALL *engineCommand)(const char *command);
  // Runs a server command (abc::ServerCmd; the vote module answers
  // "voteyes"/"voteno").
  void(ABCUI_CALL *serverCommand)(const char *command);
  // Hands the hud_vote gate cvar to the netmsg VoteMenu consumer: while the
  // gate is off the message passes through untouched (old IsVoteEnable).
  // Pass null to remove the gate (the message is then not routed at all).
  void(ABCUI_CALL *setVoteGate)(AbcUiCvarHandle cvar);
  // Registers a cvar and arms an engine-side change callback (the old
  // CREATE_CVAR callback contract: clamping and re-set reactions). The
  // callback runs on the engine cvar-set path with the module's userdata.
  AbcUiCvarHandle(ABCUI_CALL *registerCvarWatch)(const char *name,
                                                 const char *value, int flags,
                                                 AbcUiCommandFn fn,
                                                 void *userdata);
  // Plays an engine sound by path (gEngfuncs.pfnPlaySoundByName).
  void(ABCUI_CALL *playSound)(const char *path, float volume);
  // Looks up an already-registered cvar by name (null when absent) - shared
  // settings such as hud_danger_health or the engine's default_fov.
  AbcUiCvarHandle(ABCUI_CALL *findCvar)(const char *name);
  // Engine key binding for a command name ("use"); "" when unbound. Same
  // measure-then-copy contract as ui->escapeRml.
  int32_t(ABCUI_CALL *keyBinding)(const char *name, char *out,
                                  uint32_t outSize);
  // Writes a cvar's float value (the old clamp-style cvar callbacks did this
  // directly; registerCvarWatch's callback has no cvar argument).
  void(ABCUI_CALL *setCvarValue)(AbcUiCvarHandle cvar, float value);
} AbcUiConsoleApi;

// -------------------------------------------------------------- events ----

// Channels are lowercase dotted strings owned by the publishing subsystem
// (see sdk/abcui/events.h for the constants and their payload structs).
// Priority > 0 intercepts before the publisher's default 0, < 0 runs after;
// the same priority keeps subscription order. Subscribe from onLoad or
// onDocumentReady. Explicit cleanup is allowed; the framework also revokes
// remaining subscriptions on module failure/final unload, not HUD_Shutdown
// (requires minAbcUi >= 0.8.1 when relying on automatic reclamation).
typedef struct AbcUiEventsApi {
  uint32_t structSize;
  AbcUiSubscription(ABCUI_CALL *subscribe)(const char *channel, int priority,
                                           AbcUiEventSink sink, void *userdata);
  AbcUiSubscription(ABCUI_CALL *subscribeVeto)(const char *channel,
                                               int priority,
                                               AbcUiEventVeto sink,
                                               void *userdata);
  void(ABCUI_CALL *unsubscribe)(AbcUiSubscription subscription);
  // Aggregate veto variant: every sink runs in priority order regardless of
  // the other sinks' results; the return of any sink consumes the event.
  AbcUiSubscription(ABCUI_CALL *subscribeVetoAggregate)(const char *channel,
                                                        int priority,
                                                        AbcUiEventVeto sink,
                                                        void *userdata);
  // AbcUi >= 0.8.5: synchronous data publication on
  // module.<publishing-manifest-id>.<name>. Local name [a-z0-9_]+, 1..64.
  // Sinks receive AbcUiModuleEvent (events.h), not the raw bytes. Version >0,
  // payload <=1 MiB, depth <=16. 1 accepted, 0 invalid/retiring/depth limit.
  // Does not publish engine channels or veto channels. Copy borrowed data.
  int32_t(ABCUI_CALL *publish)(const char *name, uint32_t schemaVersion,
                               const void *payload, uint32_t payloadSize);
} AbcUiEventsApi;

// ----------------------------------------------------------------- vfs ----

typedef struct AbcUiFileView AbcUiFileView; // opaque opened package entry

// Resource results: success is zero; failures never supply a fallback asset.
#define ABCUI_OK 0
#define ABCUI_ERROR_INVALID (-1)
#define ABCUI_ERROR_NOT_FOUND (-2)
#define ABCUI_ERROR_FORBIDDEN (-3)
#define ABCUI_ERROR_UNAVAILABLE (-4)
#define ABCUI_ERROR_DECODE (-5)
#define ABCUI_ERROR_LIMIT (-6)
#define ABCUI_ERROR_PENDING (-7)
#define ABCUI_ERROR_UPLOAD (-8)

typedef void(ABCUI_CALL *AbcUiFileSink)(void *userdata, const char *uri);

typedef struct AbcUiVfsApi {
  uint32_t structSize;
  // Only mod:// URIs, inside self or a direct manifest dependency. Read-only.
  // Views retain their backing zip until free/unload, even after unmount.
  int32_t(ABCUI_CALL *open)(const char *uri, AbcUiFileView **out);
  const void *(ABCUI_CALL *viewData)(AbcUiFileView *view);
  uint32_t(ABCUI_CALL *viewSize)(AbcUiFileView *view);
  void(ABCUI_CALL *viewFree)(AbcUiFileView *view);
  // Recursive, sorted package enumeration; borrowed URIs during each call.
  int32_t(ABCUI_CALL *enumerate)(const char *directory, AbcUiFileSink sink,
                                 void *userdata);
  // Resolves relative references without leaving the package root. Writes a
  // complete URI or returns LIMIT; cross-package reads require a dependency.
  int32_t(ABCUI_CALL *resolve)(const char *base, const char *path, char *out,
                               uint32_t outSize);
  // Explicit game-content seam. Never searches old framework asset folders.
  // Byte count, or negative on error; null output measures the required size.
  int32_t(ABCUI_CALL *readEngineFile)(const char *path, void *out,
                                      uint32_t outSize, uint32_t maxBytes);
} AbcUiVfsApi;

typedef uint64_t AbcUiTexture;
typedef struct AbcUiTexturesApi {
  uint32_t structSize;
  // Context-owned handles, revoked on shutdown/failure/unload. RGBA8, tightly
  // packed, straight alpha. additive selects src*alpha+dst, not source-over.
  int32_t(ABCUI_CALL *upload)(const void *rgba, uint32_t bytes, int32_t width,
                              int32_t height, int32_t additive,
                              AbcUiTexture *out);
  int32_t(ABCUI_CALL *load)(const char *uri, int32_t additive,
                            AbcUiTexture *out);
  int32_t(ABCUI_CALL *uri)(AbcUiTexture texture, char *out, uint32_t outSize);
  void(ABCUI_CALL *release)(AbcUiTexture texture);
} AbcUiTexturesApi;

typedef uint64_t AbcUiAudio;
typedef struct AbcUiAudioApi {
  uint32_t structSize;
  // Async package-only decode; retains a mapped view. No temporary files.
  // Channels start paused. status: PENDING / OK / negative resource error.
  int32_t(ABCUI_CALL *open)(const char *uri, int32_t loop, AbcUiAudio *out);
  int32_t(ABCUI_CALL *status)(AbcUiAudio audio);
  void(ABCUI_CALL *setPaused)(AbcUiAudio audio, int32_t paused);
  // Per-channel linear gain, 0..1. The module selects its cvar/volume policy.
  void(ABCUI_CALL *setVolume)(AbcUiAudio audio, float volume);
  void(ABCUI_CALL *close)(AbcUiAudio audio);
} AbcUiAudioApi;

// -------------------------------------------------------------- panels ----

// Registers a HUD panel: one Rml document fed by the framework frame pumps.
// documentPath is a mod:// uri inside the module package ("mod://<id>/rml/
// health.rml"). Registration is only legal from onLoad; the panel joins the
// next HUD_Init install pass (the same HUD_Init cycle).
typedef struct AbcUiPanelDesc {
  uint32_t structSize;
  const char *name;         // unique panel name (debug lookup, cvar prefix)
  const char *documentPath; // mod:// uri of the panel's rml
  int zIndex;               // document paint order; 0 = default
  void *userdata;           // passed back to every callback
  void(ABCUI_CALL *onDocumentReady)(void *userdata);
  void(ABCUI_CALL *onData)(void *userdata, double frameTime); // HUD_Frame pump
  void(ABCUI_CALL *onRedraw)(void *userdata,
                             double frameTime); // HUD_Redraw pump
  void(ABCUI_CALL *reset)(void *userdata);      // map transition
} AbcUiPanelDesc;

typedef struct AbcUiPanelsApi {
  uint32_t structSize;
  int(ABCUI_CALL *registerPanel)(const AbcUiPanelDesc *desc);
} AbcUiPanelsApi;

// ----------------------------------------------------------------- data ----

// Snapshot of the local player HUD state the visibility rules hang off
// (predicted client data plus the usermsg-fed values). Values are copied
// out; int32_t flags are 0/1.
typedef struct AbcUiHudState {
  uint32_t structSize;
  int32_t suit;
  int32_t spectate;
  int32_t longjump;
  int32_t hideBits; // raw last-write HideWeapon/HideHUD token
  int32_t flashOn;
  int32_t flashBattery;
  int32_t health; // predicted health (0 until the first prediction frame)
  int32_t armor;  // message-fed armor value
  int32_t waterLevel;
  float fov;
  float punchAngle[3]; // predicted view punch (old hudstate::punchangle)
  // Local player observer state (entity_state iuser1/iuser2). spectateMode is
  // 0 outside observer mode; spectateTarget is the target entindex (1..32).
  // Native SpectatorPanel reads the same pair; appended in AbcUi 0.9.1.
  int32_t spectateMode;
  int32_t spectateTarget;
} AbcUiHudState;

// Local player readout (data->playerState): copied out, never a pointer.
typedef struct AbcUiPlayerState {
  uint32_t structSize;
  int32_t index;      // engine entindex, 0 when there is no local player
  int32_t frags;      // score from the ScoreInfo feed
  float speed;        // horizontal velocity (the old side readout)
  float origin[3];    // local player origin (grenade range / projection)
  float angles[3];    // local view angles (gEngfuncs.GetViewAngles)
  int32_t messageNum; // entity message generation (stale-entity gate)
} AbcUiPlayerState;

// PlayerTrace types shared with data->traceEndPos.
#define ABCUI_TRACE_VIEW 0
#define ABCUI_TRACE_HEAD 1
#define ABCUI_TRACE_FOOT 2

typedef struct AbcUiDataApi {
  uint32_t structSize;
  void(ABCUI_CALL *hudState)(AbcUiHudState *out);
  // Current level name ("maps/<map>.bsp"); empty when the engine has none.
  // Same measure-then-copy contract as ui->escapeRml.
  int(ABCUI_CALL *levelName)(char *out, uint32_t outSize);
  // Local player readout: entindex (0 when there is none), frags from the
  // ScoreInfo feed and the horizontal speed the HUD readouts display.
  void(ABCUI_CALL *playerState)(AbcUiPlayerState *out);
  // Engine third-person view flag (gExportfuncs.CL_IsThirdPerson).
  int32_t(ABCUI_CALL *thirdPerson)(void);
  // Cached player trace end position (the same traces the HUD projects);
  // 1 on success. `type` is one of ABCUI_TRACE_*.
  int32_t(ABCUI_CALL *traceEndPos)(int32_t type, float out[3]);
  // Engine model index for a model path (EV_FindModelIndex); 0 when the
  // engine does not know the model.
  int32_t(ABCUI_CALL *modelIndex)(const char *path);
  // Local player view height offset (EV_LocalPlayerViewheight).
  void(ABCUI_CALL *viewHeight)(float out[3]);
  // Scoreboard header data: server name, next map and the time limit the
  // server announced (0 = none). Strings use the measure-then-copy contract
  // (out == null to measure); either string may be null when not needed.
  void(ABCUI_CALL *serverInfo)(char *serverName, uint32_t serverNameSize,
                               char *nextMap, uint32_t nextMapSize,
                               int32_t *timeEnd);
  // AbcUi >= 0.9.1: promotes the framework's SayText usermsg hook to the head
  // of the engine chain and remembers the displaced head as its upstream
  // link. The outermost hook publishes the textfeed.saytext veto once and
  // forwards to that link only while no sink consumes the line; an older
  // displaced hook forwards straight to the chain tail. Returns 1 when the
  // hook is at the head, 0 on refusal. Main thread only.
  int32_t(ABCUI_CALL *ensureSayTextFirst)(void);
} AbcUiDataApi;

// ------------------------------------------------------------------- ui ----

// Dynamic crosshair layout request (the old services::crosshair::Layout).
typedef struct AbcUiCrosshairLayout {
  float thickness;
  float length;
  float offset;       // gap + the caller's punch drift
  float outlineWidth; // 0 = no outline
  int32_t dot;
  int32_t tShape;
} AbcUiCrosshairLayout;

typedef struct AbcUiDocument AbcUiDocument; // opaque panel-backed document
// Element handles are only valid for the callback that queried them; query
// again per event (the built-ins do the same).
typedef struct AbcUiElement AbcUiElement;

// One DOM event delivered to a module listener. Mouse coordinates are in
// context pixels; `button` is -1 for non-mouse events. Keyboard events
// (keydown/keyup) carry the engine key number and an ABCUI_MOD_* bitmask.
#define ABCUI_MOD_CTRL 0x1
#define ABCUI_MOD_SHIFT 0x2

typedef struct AbcUiElementEvent {
  uint32_t structSize;
  const char *type; // "click", "mousedown", ...
  int32_t button;
  float mouseX;
  float mouseY;
  int32_t keynum;    // engine key number (keyboard events; 0 otherwise)
  int32_t modifiers; // ABCUI_MOD_* (keyboard events; 0 otherwise)
} AbcUiElementEvent;

// Returns nonzero to stop propagation (the module handled the event).
typedef int32_t(ABCUI_CALL *AbcUiElementListener)(
    void *userdata, AbcUiElement *target, const AbcUiElementEvent *event);

// One laid-out line of an element's text: its position inside the text
// element (context pixels) and its byte range within the element's full
// text (byteOffset is a character boundary; bytes is the wrapped line's
// length).
typedef struct AbcUiTextLine {
  float x;
  float y;
  uint32_t byteOffset;
  uint32_t bytes;
} AbcUiTextLine;

typedef struct AbcUiUiApi {
  uint32_t structSize;
  // The document of one of this module's registered panels, by name.
  AbcUiDocument *(ABCUI_CALL *panelDocument)(const char *panelName);
  // Session-aware visibility (the same gating the built-in panels use).
  void(ABCUI_CALL *documentShow)(AbcUiDocument *doc);
  void(ABCUI_CALL *documentHide)(AbcUiDocument *doc);
  // Sets a document-level RCSS property (e.g. a --token).
  void(ABCUI_CALL *documentSetProperty)(AbcUiDocument *doc, const char *name,
                                        const char *value);
  // "#id" lookup inside the document; null when missing.
  AbcUiElement *(ABCUI_CALL *querySelector)(AbcUiDocument *doc, const char *id);
  void(ABCUI_CALL *elementSetProperty)(AbcUiElement *elem, const char *name,
                                       const char *value);
  void(ABCUI_CALL *elementSetInnerRML)(AbcUiElement *elem, const char *rml);
  // Localizes a translation token ("Vote_DefaultYes") through the game
  // language table. Returns 1 and writes the translation (at most outSize-1
  // bytes plus terminator); 0 when the token is unknown (out untouched).
  int(ABCUI_CALL *translate)(const char *token, char *out, uint32_t outSize);
  // Escapes server-derived text for elementSetInnerRML (the built-in panels'
  // TextUtil::escapeRml). Returns the byte count the escaped text needs
  // (terminator excluded) and writes at most outSize-1 bytes plus the
  // terminator; pass out == null to measure first.
  int(ABCUI_CALL *escapeRml)(const char *text, char *out, uint32_t outSize);
  // Display name for an engine keynum (IGameUIFuncs::Key_NameForKey); empty
  // when the engine interface is missing. Same measure-then-copy contract
  // as escapeRml.
  int(ABCUI_CALL *keyNameForKey)(int keynum, char *out, uint32_t outSize);
  // True while the panel's document is actually on screen (the session gate
  // included) - the built-in panels' isVisible().
  int(ABCUI_CALL *documentVisible)(AbcUiDocument *doc);
  // Appends one child element to `parent`, built from `rml` (the element's
  // inner markup) with `className` applied when non-empty. Returns a handle
  // that stays valid until the element is removed or its document closes -
  // the dynamic-row pattern (kill feed, chat log). Do not cache handles
  // across a context rebuild.
  AbcUiElement *(ABCUI_CALL *elementAppendRml)(AbcUiElement *parent,
                                               const char *rml,
                                               const char *className);
  // Removes and destroys an element appended through elementAppendRml.
  void(ABCUI_CALL *elementRemove)(AbcUiElement *element);
  // Reads an element's inner RML; same measure-then-copy contract as
  // escapeRml.
  int(ABCUI_CALL *elementInnerRML)(AbcUiElement *element, char *out,
                                   uint32_t outSize);
  // Writes the RCSS "rgb(r, g, b)" text for a 0..255 RGB triple
  // (ColorUtil::format).
  void(ABCUI_CALL *formatColor)(const float rgb[3], char *out,
                                uint32_t outSize);
  // Old ColorUtil::differColor: the hue of `base` pulled toward `danger` by
  // `ratio` (1 = base, 0 = danger; saturation/value stay base's), written as
  // RCSS text. Both inputs are 0..255 RGB triples.
  void(ABCUI_CALL *differColor)(const float base[3], const float danger[3],
                                float ratio, char *out, uint32_t outSize);
  // Sets an HTML attribute ("src", "class", ...); a null value removes it.
  void(ABCUI_CALL *elementSetAttribute)(AbcUiElement *element, const char *name,
                                        const char *value);
  // Toggles one class token on an element.
  void(ABCUI_CALL *elementSetClass)(AbcUiElement *element, const char *name,
                                    int enabled);
  // hud.txt sprite lookup (the sprite resource tables): fills the atlas name
  // and crop rect; 1 when the name resolves.
  int(ABCUI_CALL *hudSprite)(const char *name, char *atlas, uint32_t atlasSize,
                             int32_t rect[4]);
  // Restarts an <animimg> element's frame sequence (no-op on other
  // elements).
  void(ABCUI_CALL *elementRestartAnimation)(AbcUiElement *element);
  // Writes the RCSS "rgba(r, g, b, a)" text (alpha as a 0..255 integer).
  void(ABCUI_CALL *formatRgba)(int r, int g, int b, int a, char *out,
                               uint32_t outSize);
  // World -> screen projection in Rml pixel coordinates. Returns the raw
  // engine result: nonzero means the point is behind the camera.
  int(ABCUI_CALL *project)(const float world[3], float screen[2]);
  // Document/context pixel dimensions; 0 when the document has no context.
  int(ABCUI_CALL *documentSize)(AbcUiDocument *doc, int *width, int *height);
  // Dynamic crosshair geometry (the single implementation the settings
  // preview also uses). Fills core[5][4] and outline[5][4] as x, y, width,
  // height relative to the centre, plus visible[5] (0 for the parts a switch
  // hides). Part order: center, top, bottom, left, right.
  void(ABCUI_CALL *crosshairLayout)(const AbcUiCrosshairLayout *layout,
                                    float core[5][4], float outline[5][4],
                                    int32_t visible[5]);
  // HUD density scale (framework::Theme::scale) - the old GetWide/GetTall
  // proportional scale pixel geometry multiplies by.
  float(ABCUI_CALL *scale)(void);
  // Nth child element (0-based); null when out of range. Stable for children
  // appended through elementAppendRml until one is removed.
  AbcUiElement *(ABCUI_CALL *elementChild)(AbcUiElement *element,
                                           int32_t index);
  // Subtree-scoped id lookup (document-scoped lookups would collapse
  // repeated per-tile ids into the first tile).
  AbcUiElement *(ABCUI_CALL *elementFind)(AbcUiElement *element,
                                          const char *id);
  // DOM listener on one element; the callback receives the event's TARGET
  // element (walk up with elementParent to find the owning row). Returns a
  // non-zero id, 0 on failure; remove it in onShutdown/onUnload.
  uint64_t(ABCUI_CALL *elementAddListener)(AbcUiElement *element,
                                           const char *type,
                                           AbcUiElementListener listener,
                                           void *userdata);
  void(ABCUI_CALL *elementRemoveListener)(AbcUiElement *element,
                                          const char *type, uint64_t id);
  // Attribute readers/helpers for the delegated-row pattern.
  int32_t(ABCUI_CALL *elementAttributeInt)(AbcUiElement *element,
                                           const char *name, int32_t fallback);
  AbcUiElement *(ABCUI_CALL *elementParent)(AbcUiElement *element);
  int32_t(ABCUI_CALL *elementClassSet)(AbcUiElement *element, const char *name);
  const char *(ABCUI_CALL *elementId)(AbcUiElement *element);
  // Text entry (the RmlUi form control the module focuses): focus/blur and
  // the UTF-8 value. elementValue follows the measure-then-copy contract.
  void(ABCUI_CALL *elementFocus)(AbcUiElement *element);
  void(ABCUI_CALL *elementBlur)(AbcUiElement *element);
  int32_t(ABCUI_CALL *elementValue)(AbcUiElement *element, char *out,
                                    uint32_t outSize);
  void(ABCUI_CALL *elementSetValue)(AbcUiElement *element, const char *text);
  // Scrolling: the log windows use these to follow the tail and to browse
  // older lines (values in Rml pixels).
  float(ABCUI_CALL *elementScrollTop)(AbcUiElement *element);
  void(ABCUI_CALL *elementSetScrollTop)(AbcUiElement *element, float top);
  float(ABCUI_CALL *elementScrollHeight)(AbcUiElement *element);
  float(ABCUI_CALL *elementClientHeight)(AbcUiElement *element);
  // Text caret range (the chat editor parks it at the end after a recall).
  void(ABCUI_CALL *elementSetSelection)(AbcUiElement *element, int32_t start,
                                        int32_t end);
  // Document focus (focused root: keys reach the document, the page keeps its
  // last control focus) and focus release (removes the document from the
  // context's focus history). Safe on panel and page handles.
  void(ABCUI_CALL *documentFocus)(AbcUiDocument *doc);
  void(ABCUI_CALL *documentBlur)(AbcUiDocument *doc);
  // Attribute reader (delegated rows carry their intent in data-* values).
  // Same measure-then-copy contract as escapeRml; an absent attribute is
  // empty (0 bytes).
  int32_t(ABCUI_CALL *elementAttribute)(AbcUiElement *element, const char *name,
                                        char *out, uint32_t outSize);
  // Attribute-value variant of escapeRml: additionally escapes the quote
  // characters that would close a double-quoted attribute. A server-derived
  // string placed inside an attribute (data-*, title, value) needs this one,
  // or a quote in the text escapes its attribute and injects markup.
  int32_t(ABCUI_CALL *escapeRmlAttribute)(const char *text, char *out,
                                          uint32_t outSize);
  // Child count for DOM walks (elementChild indexes into it).
  int32_t(ABCUI_CALL *elementChildCount)(AbcUiElement *element);
  // Select (ElementFormControlSelect) helpers for option rows. `value` is
  // the option's payload string (elementValue reads it back verbatim).
  void(ABCUI_CALL *elementSelectRemoveAll)(AbcUiElement *element);
  void(ABCUI_CALL *elementSelectAdd)(AbcUiElement *element, const char *label,
                                     const char *value);
  int32_t(ABCUI_CALL *elementSelectCount)(AbcUiElement *element);
  void(ABCUI_CALL *elementSelectSetSelection)(AbcUiElement *element,
                                              int32_t index);
  int32_t(ABCUI_CALL *elementSelectSelection)(AbcUiElement *element);
  // Scroll helper (bring a row into view after a programmatic selection;
  // alignTop picks the alignment edge).
  void(ABCUI_CALL *elementScrollIntoView)(AbcUiElement *element,
                                          int32_t alignTop);
  // Capture-phase listener (focus/blur never bubble; a capture keydown
  // consumes interaction keys before the focused widget sees them).
  uint64_t(ABCUI_CALL *elementAddListenerCapture)(AbcUiElement *element,
                                                  const char *type,
                                                  AbcUiElementListener listener,
                                                  void *userdata);
  // Layout queries for text-adjacent widgets (the console's caret mapping
  // and selection highlight). Absolute offsets are context pixels.
  int32_t(ABCUI_CALL *elementAbsoluteOffset)(AbcUiElement *element, float *x,
                                             float *y);
  int32_t(ABCUI_CALL *elementOffsetSize)(AbcUiElement *element, float *width,
                                         float *height);
  // One element's laid-out text lines (its first text child): fills up to
  // `max` AbcUiTextLine entries and returns the total line count. Byte
  // offsets index the element's full text (slice with the row's own text
  // copy); `lineHeight` receives the text's line height and
  // `originX`/`originY` the text child's absolute offset (context pixels),
  // the coordinate space the line positions live in.
  int32_t(ABCUI_CALL *elementTextLayout)(AbcUiElement *element,
                                         AbcUiTextLine *out, uint32_t max,
                                         float *lineHeight, float *originX,
                                         float *originY);
  // The laid-out pixel width of a UTF-8 byte range, measured in the
  // element's text face (the same path that wrapped the line).
  int32_t(ABCUI_CALL *elementTextPrefixWidth)(AbcUiElement *element,
                                              const char *text, uint32_t bytes);
  // The document's density-independent pixel ratio (dp -> context pixels).
  int32_t(ABCUI_CALL *documentScale)(AbcUiDocument *doc, float *scale);
  // Pseudo-class read (":focus" on a text control: a projection must not
  // clobber what the user is typing).
  int32_t(ABCUI_CALL *elementPseudoClassSet)(AbcUiElement *element,
                                             const char *name);
  // Writes a <progress> element's value (0..max).
  void(ABCUI_CALL *elementProgressValue)(AbcUiElement *element, float value);
  // The last child (DOM or non-DOM widget part) with this tag name; null
  // when there is none. A range slider's thumb is a non-DOM child that
  // WidgetSlider appends after the DOM ones, so it is found by tag, never by
  // selector or child index.
  AbcUiElement *(ABCUI_CALL *elementFindTag)(AbcUiElement *element,
                                             const char *tag);
  // Disabled state of a form control (<input>/<select>; a <button> stays a
  // generic element and gates through a class instead).
  void(ABCUI_CALL *elementSetDisabled)(AbcUiElement *element, int32_t disabled);
  // Writes the RCSS "rgb(r, g, b)" text of a player-palette hue (0..255, the
  // engine's topcolor/bottomcolor domain: ColorUtil::playerHueColor then
  // format).
  void(ABCUI_CALL *formatHueColor)(float value, char *out, uint32_t outSize);
  // Drops an Rml-cached texture by source URI. A preview <img> whose src is
  // about to change must retire the previous variant, or every palette it
  // showed stays resident on the GPU.
  void(ABCUI_CALL *releaseTexture)(const char *source);
  // Moves an existing element under another parent (the settings page's
  // singleton widget blocks move between cards); 1 on success, 0 for an
  // invalid handle or a cycle. The element keeps its attributes and events.
  int32_t(ABCUI_CALL *elementAppendChild)(AbcUiElement *parent,
                                          AbcUiElement *child);
} AbcUiUiApi;

// ------------------------------------------------------------- commands ----

typedef struct AbcUiCommandsApi {
  uint32_t structSize;
  // Dynamically hooks any existing callback-backed engine command. AbcUi
  // >= 0.8.4 has no whitelist, fixed slot count or init-only restriction.
  // ASCII case variants share one bridge; punctuation/non-ASCII names work.
  // Supply one command token (no arguments, whitespace, quotes or semicolons).
  // Returns 0 for invalid/missing names, null native callbacks or unavailable
  // engine seams. Aliases, cvars and server-only verbs have no callback to
  // hook; register a new client command via console->registerCommand first if
  // needed. Every sink runs in priority order; nonzero skips the original.
  // Revoke via events->unsubscribe (also automatic on module failure/unload).
  // Changes during dispatch are safe: removed sinks are skipped; new sinks join
  // the next dispatch. Empty bridges remain process-owned native pass-throughs
  // so another plugin may safely retain the dispatcher as its predecessor.
  AbcUiSubscription(ABCUI_CALL *subscribeCommand)(const char *name,
                                                  int priority,
                                                  AbcUiEventVeto sink,
                                                  void *userdata);
  // The shared number-menu-open flag: the text-menu module writes it, other
  // modules and the framework gate on it.
  int(ABCUI_CALL *textMenuOpen)(void);
  void(ABCUI_CALL *setTextMenuOpen)(int open);
  // AbcUi >= 0.9.1: promotes an existing subscribeCommand bridge to the head
  // of the engine's command chain (the rule a replacement chat uses to win
  // over another plugin regardless of load order). The displaced head becomes
  // the bridge's upstream link: when no sink consumed the command the
  // outermost link calls it, so a stand-down still reaches the other plugin.
  // Returns 1 when the bridge is at the head, 0 when the name has no bridge,
  // the engine callback is missing, or the bridge is currently dispatching.
  // Main thread only.
  int32_t(ABCUI_CALL *ensureFirstCommand)(const char *name);
} AbcUiCommandsApi;

// -------------------------------------------------------------- weapons ----

#define ABCUI_WEAPON_NAME_MAX 64
#define ABCUI_WEAPON_ATLAS_MAX 96
// Registry slot count (old MAX_WEAPON_SLOT / weaponres::kMaxWeaponSlot).
#define ABCUI_WEAPON_SLOT_COUNT 10

// Old ITEM_FLAGS bits the selection logic keys on.
#define ABCUI_ITEM_FLAG_SELECT_ON_EMPTY (1 << 0)
#define ABCUI_ITEM_FLAG_EXHAUSTIBLE (1 << 4)

// One sprite crop ("sprites/<atlas>.txt" entry). An error means the weapon's
// sprite list or atlas could not be read; the module chooses its fallback.
// Embedded resource record for AbcUiWeaponInfo.
typedef struct AbcUiWeaponSprite {
  int32_t status; // ABCUI_OK or a resource error; no framework fallback
  char atlas[ABCUI_WEAPON_ATLAS_MAX];
  int32_t rect[4]; // left, top, right, bottom
} AbcUiWeaponSprite;

// Point-in-time snapshot of one registry weapon. Identity is (slot, slotPos):
// the server's WeaponList assigns both and the registry keeps them stable.
// Array snapshots use caller-provided byte strides.
typedef struct AbcUiWeaponInfo {
  uint32_t structSize;
  int32_t slot;
  int32_t slotPos;
  int32_t ammoType;
  int32_t ammo2Type;
  int32_t max1;
  int32_t max2;
  int32_t flags;   // ABCUI_ITEM_FLAG_*
  int32_t owned;   // owned by the local player right now
  int32_t hasAmmo; // usable right now (unlimited weapons always true)
  int32_t clip;    // primary magazine (-1 when the weapon has none)
  int32_t clip2;   // secondary magazine (-1 when the weapon has none)
  int32_t state;   // CurWeapon state bits (valid/onTarget/uzi)
  char name[ABCUI_WEAPON_NAME_MAX];
  AbcUiWeaponSprite active;          // "weapon_s" sprite (highlighted item)
  AbcUiWeaponSprite inactive;        // "weapon" sprite
  AbcUiWeaponSprite ammo;            // "ammo" sprite (ammo bar / pickup stack)
  AbcUiWeaponSprite ammo2;           // "ammo2" sprite (module chooses fallback)
  AbcUiWeaponSprite crosshair;       // per-weapon crosshair ("crosshair")
  AbcUiWeaponSprite autoaim;         // on-target crosshair ("autoaim")
  AbcUiWeaponSprite zoomedCrosshair; // zoomed crosshair
  AbcUiWeaponSprite zoomedAutoaim;   // zoomed on-target crosshair
} AbcUiWeaponInfo;

typedef struct AbcUiWeaponsApi {
  uint32_t structSize;
  // Fills up to `capacity` snapshots of every registered weapon; returns the
  // total registry size (a larger total means the list was truncated).
  // Caller supplies byte stride and initializes each row's structSize.
  // Null output measures the count; invalid layouts return INVALID.
  int32_t(ABCUI_CALL *list)(void *out, uint32_t capacity, uint32_t stride);
  // Snapshot by identity; 1 when the weapon exists.
  int32_t(ABCUI_CALL *get)(int32_t slot, int32_t slotPos, AbcUiWeaponInfo *out);
  int32_t(ABCUI_CALL *selectedSlot)(void);
  // Snapshot of the highlighted weapon; 1 when there is one.
  int32_t(ABCUI_CALL *selected)(AbcUiWeaponInfo *out);
  int32_t(ABCUI_CALL *countAmmo)(int32_t ammoType);
  // hud_fastswitch: the engine command already went out, the bar stays down.
  int32_t(ABCUI_CALL *fastSwitch)(void);
  // Old SelectSlot: a slot key enters/cycles one slot, invnext/invprev walk
  // slots and positions (`wheel` = the walk flavor).
  void(ABCUI_CALL *selectSlot)(int32_t slot, int32_t advance, int32_t wheel);
  // ResourceLimits::weaponCommandName - the console-token gate for the
  // server-provided weapon name (defense in depth; the registry already
  // rejects non-identifiers).
  int32_t(ABCUI_CALL *validCommandName)(const char *name);
  // Snapshot of the current (CurWeapon) weapon; 1 when known.
  int32_t(ABCUI_CALL *current)(AbcUiWeaponInfo *out);
  // Snapshot by the engine/server weapon id; 1 when the registry knows it.
  int32_t(ABCUI_CALL *getById)(int32_t id, AbcUiWeaponInfo *out);
  // Icon of an ammo type, taken from a registered weapon; resource result.
  int32_t(ABCUI_CALL *ammoIcon)(int32_t ammoType, AbcUiWeaponSprite *out);
} AbcUiWeaponsApi;

// -------------------------------------------------------------- players ----

#define ABCUI_MAX_PLAYERS 32

// One player-resource row (ScoreInfo wire values + engine mirror).
typedef struct AbcUiPlayerInfo {
  uint32_t structSize;
  int32_t index; // 1..32 entindex
  int32_t valid; // connected AND named
  int32_t spectate;
  int32_t team;  // teamres::TeamId
  int32_t admin; // 0 none, 1 operator, 2 server owner
  int32_t donor; // old native donor kind (0x10000 + kind)
  int32_t topColor;
  int32_t bottomColor;
  int32_t ping;
  int32_t loss;
  float frags;
  float health;
  float armor;
  uint64_t steamId64;
  char name[64];
  char model[96];
  int32_t deaths;
  char realName[64];
} AbcUiPlayerInfo;

// Engine entity snapshot (GetEntityByIndex + curstate/baseline fields the HUD
// panels gate on).
typedef struct AbcUiEntityState {
  uint32_t structSize;
  int32_t valid;
  int32_t messageNum;
  int32_t isPlayer;
  int32_t effects;
  float origin[3];
  float angles[3];
  float mins[3];
  float maxs[3];
} AbcUiEntityState;

typedef struct AbcUiPlayersApi {
  uint32_t structSize;
  // The local player's row; valid == 0 when there is none.
  void(ABCUI_CALL *local)(AbcUiPlayerInfo *out);
  // One slot's row; 1 when the player resource knows the slot at all.
  int32_t(ABCUI_CALL *get)(int32_t index, AbcUiPlayerInfo *out);
  // Engine entity snapshot; 1 when live.
  int32_t(ABCUI_CALL *entity)(int32_t index, AbcUiEntityState *out);
  // Client dll per-player tint (GetClientColor, 0..1 RGB); 1 when available.
  int32_t(ABCUI_CALL *clientColor)(int32_t index, float out[3]);
  // Fetch/poll a real avatar. PENDING, NOT_FOUND, LIMIT, UPLOAD or OK.
  // Generation is positive only for an uploaded image; modules choose their
  // own fallback URI whenever this returns an error.
  int32_t(ABCUI_CALL *avatar)(uint64_t steamId64, int32_t *generation);
  // Localization token for a team id ("Team_Red", ...); same
  // measure-then-copy contract as ui->escapeRml.
  int32_t(ABCUI_CALL *teamNameToken)(int32_t team, char *out, uint32_t outSize);
  // Re-reads the engine mirrors (identity + live ping/loss). `full` = the
  // heavy identity pass (old per-row update()); 0 = ping/loss only.
  void(ABCUI_CALL *refresh)(int32_t full);
  // Steam animated-avatar (APNG) probe/cache. OK when the animation bytes are
  // cached: build "avataranim://<steamid64>?gen=<generation>". PENDING while
  // the profile/download is in flight (keep the static avatar), NOT_FOUND when
  // the user has none (or the runtime cannot serve one), UNAVAILABLE without
  // Steam, LIMIT at the cache bound. Appended; use ABCUI_API_HAS_MEMBER.
  int32_t(ABCUI_CALL *avatarAnimated)(uint64_t steamId64,
                                      int32_t *generation);
} AbcUiPlayersApi;

// ---------------------------------------------------------------- radar ----

// The framework's world-to-FBO producer (the render half of the radar).
// The module owns the document, the plate geometry and the avatars; the
// framework renders the world from above when the module says the plate is
// on screen.
#define ABCUI_RADAR_ORTHO_BASE 4096.0f

typedef struct AbcUiRadarApi {
  uint32_t structSize;
  // 1 when the FBO colour texture exists and is registered.
  int32_t(ABCUI_CALL *ready)(void);
  // Bumped whenever the FBO texture handle changes; build the map uri as
  // "radarfbo://map?gen=<generation>" (RmlUi caches textures per source).
  int32_t(ABCUI_CALL *generation)(void);
  // The plate is on screen: render the world pass on the next pre-view.
  void(ABCUI_CALL *setActive)(int32_t active);
  // A texture uploaded by this module; zero removes the mask. No built-in
  // mask textures. The host borrows the texture until replaced or released.
  int32_t(ABCUI_CALL *setMask)(AbcUiTexture texture);
  // hud_radar_zoom: visible span = 2 * (ABCUI_RADAR_ORTHO_BASE / zoom).
  void(ABCUI_CALL *setZoom)(float zoom);
} AbcUiRadarApi;

// --------------------------------------------------------------- score ----

// The framework's scoreboard interaction seat (see framework/ScoreSeat.h):
// the +/-showscores latch that becomes usercmd IN_SCORE, the intermission
// flag HUD_Redraw feeds, and the interactive-HUD pointer tier (MOUSE2
// unlocks the cursor; the router token gates Rml mouse input).
typedef struct AbcUiScoreApi {
  uint32_t structSize;
  // The module holds the scores key: nonzero = CL_CreateMove reports
  // IN_SCORE so the server streams every player's ping/loss.
  void(ABCUI_CALL *setHeld)(int32_t held);
  int32_t(ABCUI_CALL *held)(void);
  // The module's table is on screen (pointer mode needs it).
  void(ABCUI_CALL *setVisible)(int32_t visible);
  int32_t(ABCUI_CALL *pointerMode)(void);
  // Refused while the table is not visible or the intermission board is up.
  void(ABCUI_CALL *setPointerMode)(int32_t enabled);
  int32_t(ABCUI_CALL *wantsWheel)(void);
  int32_t(ABCUI_CALL *intermission)(void);
} AbcUiScoreApi;

// --------------------------------------------------------------- input ----

// One ownership claim on the UI input router (the framework's single input
// truth). The callbacks run on the engine main thread while the token is
// top-of-stack; a null callback behaves like the default (no claim / no
// block / nothing to do).
typedef struct AbcUiInputCallbacks {
  uint32_t structSize;
  void *userdata;
  // Swallow the edge from the engine bindings and feed it to the UI.
  int32_t(ABCUI_CALL *claimsKey)(void *userdata, int32_t down, int32_t keynum);
  int32_t(ABCUI_CALL *claimsWheel)(void *userdata);
  // The game must not act on its own input state this frame (a text field
  // owns the keyboard: the chat line keeps the player standing still).
  int32_t(ABCUI_CALL *blocksGameplay)(void *userdata);
  // Exactly once per physical press of a claimed key.
  void(ABCUI_CALL *onClaimedKeyDown)(void *userdata, int32_t keynum);
  // A claimed wheel pulse offered before the scroll claim (key capture).
  int32_t(ABCUI_CALL *onClaimedWheelPulse)(void *userdata, int32_t keynum);
  // Force-release (focus loss / reset): the token is already stale.
  void(ABCUI_CALL *onOwnershipLost)(void *userdata);
} AbcUiInputCallbacks;

typedef uint64_t AbcUiInputToken; // 0 is never issued.

typedef struct AbcUiImeState {
  uint32_t structSize;
  int32_t visible;
  int32_t selected;
  int32_t candidateCount;
  int32_t cursorBytes;
  float x, y, lineHeight; // context pixels
  char composition[1024];
  char candidates[9][256];
} AbcUiImeState;

typedef struct AbcUiInputApi {
  uint32_t structSize;
  // Takes an interactive-HUD token. Callbacks are copied; the module keeps
  // the userdata alive until release.
  AbcUiInputToken(ABCUI_CALL *takeToken)(const AbcUiInputCallbacks *callbacks);
  void(ABCUI_CALL *releaseToken)(AbcUiInputToken token);
  int32_t(ABCUI_CALL *tokenHeld)(AbcUiInputToken token);
  // True while some owner holds UI input (the engine must not act on keys).
  int32_t(ABCUI_CALL *exclusive)(void);
  // An IME composition is live (the editor must not act on Enter/ESC; the
  // input method consumes them) / drop the live composition.
  int32_t(ABCUI_CALL *textComposing)(void);
  void(ABCUI_CALL *cancelComposition)(void);
  // The native pause menu is up (a focused editor must close).
  int32_t(ABCUI_CALL *menuUp)(void);
  // Active input-method short tag (CN/TW/HK/JP/KR), "" when none. Same
  // measure-then-copy contract as ui->escapeRml.
  int32_t(ABCUI_CALL *imeLanguageTag)(char *out, uint32_t outSize);
  int32_t(ABCUI_CALL *imeState)(AbcUiImeState *out);
} AbcUiInputApi;

// --------------------------------------------------------------- shell ----

// OS-shell seams the interactive panels need (profile links, copy actions).
// Text is UTF-8; both are no-ops on null/empty input.
typedef struct AbcUiShellApi {
  uint32_t structSize;
  // Opens the URL in the default browser (framework::OpenExternalUrl).
  void(ABCUI_CALL *openUrl)(const char *url);
  // Puts the text on the system clipboard (Rml system interface).
  void(ABCUI_CALL *copyText)(const char *text);
  // Reads the system clipboard (measure-then-copy; 0 when unavailable or
  // empty). The multi-line paste gate inspects it before the editor sees
  // a Ctrl+V.
  int32_t(ABCUI_CALL *clipboardText)(char *out, uint32_t outSize);
} AbcUiShellApi;

// --------------------------------------------------------------- voice ----

// The client's per-player voice blocking service (mute list + server vban
// sync). Identity comes from the player resource's SteamID64: players
// without one answer false and the local player can never be muted.
typedef struct AbcUiVoiceApi {
  uint32_t structSize;
  int32_t(ABCUI_CALL *available)(void);
  int32_t(ABCUI_CALL *isBlocked)(int32_t index);
  // Sets the ban and syncs the server; 0 when the toggle was refused.
  int32_t(ABCUI_CALL *setBlocked)(int32_t index, int32_t blocked);
} AbcUiVoiceApi;

// ----------------------------------------------------------- documents ----

// Interactive (menu / dialogue) documents. A HUD panel is passive: pointer
// events off, no focus. A module page is the complement - one Rml document
// shown with document focus, pulled to the front of the context z-order,
// hit-testable, and closable. Register the descriptor once (the package
// image owns the document assets); the framework loads it on every Rml
// context cycle, because documents die with the context.
//
// The page keeps its DOM work through the ui section: root() is the document
// element for delegated listeners (querySelector resolves ids inside it),
// and every element handle follows the same per-callback lifetime rule.
#define ABCUI_DOC_FLAG_BACKDROP                                                \
  (1 << 0)                                 // wall-sized surface: attach the
                                           // shared blurred backdrop
#define ABCUI_DOC_FLAG_DECORATION (1 << 2) // noninteractive, never takes focus
#define ABCUI_DOC_FLAG_MENU                                                    \
  (1 << 1) // part of the menu layer: hidden
           // when the menu lowers and honored
           // by the focus guard

// Cross-document z-order tiers. A page shell uses 0; the display-mode
// confirmation outranks every menu surface (it is the only way back from a
// bad mode) and carries no menu flag so a menu hide cannot drop it.
#define ABCUI_DOC_ZINDEX_CONFIRM 650
// The quit confirmation sits below the display-mode confirmation but above
// every page surface (the old framework overlay used the same tier).
#define ABCUI_DOC_ZINDEX_QUIT_CONFIRM 600

typedef struct AbcUiDocumentDesc {
  uint32_t structSize;
  const char *name; // unique document name (diagnostics, lookup)
  const char *path; // document path ("abc/abcui/...", "mod://<id>/...")
  uint32_t flags;   // ABCUI_DOC_FLAG_*
  int32_t zIndex;   // cross-document tier; 0 = default
  void *userdata;   // passed back to every callback
  // The main document loaded (never fired for the recovery provider page;
  // use documents->degraded to detect that case). Bind listeners here.
  void(ABCUI_CALL *onReady)(void *userdata);
  void(ABCUI_CALL *onShow)(void *userdata);
  void(ABCUI_CALL *onHide)(void *userdata);
  // Document closed: a context shutdown or documents->destroy. Handles are
  // already invalid; drop anything the owner cached.
  void(ABCUI_CALL *onClose)(void *userdata);
} AbcUiDocumentDesc;

// Optional recovery UI provider, independent of feature pages. All callbacks
// run with the provider identity; document handles are borrowed. onClose is
// called with the DOM already unavailable. One provider per process, onLoad
// only. The path must point into its own package. No framework UI is embedded.
typedef struct AbcUiRecoveryDesc {
  uint32_t structSize;
  const char *path;
  void *userdata;
  void(ABCUI_CALL *onReady)(void *userdata, AbcUiDocument *document);
  void(ABCUI_CALL *onClose)(void *userdata, AbcUiDocument *document);
} AbcUiRecoveryDesc;
typedef uint64_t AbcUiRecoveryRegistration;

typedef struct AbcUiDocumentsApi {
  uint32_t structSize;
  // Registers a page. The descriptor strings are copied; legal at any time
  // (a document created after contextReady loads immediately). Returns null
  // on a duplicate name or an incomplete descriptor.
  AbcUiDocument *(ABCUI_CALL *create)(const AbcUiDocumentDesc *desc);
  // Unregisters and closes (onClose fires while the module is still loaded).
  void(ABCUI_CALL *destroy)(AbcUiDocument *doc);
  int32_t(ABCUI_CALL *loaded)(AbcUiDocument *doc);
  int32_t(ABCUI_CALL *degraded)(AbcUiDocument *doc);
  int32_t(ABCUI_CALL *visible)(AbcUiDocument *doc);
  void(ABCUI_CALL *show)(AbcUiDocument *doc);
  void(ABCUI_CALL *hide)(AbcUiDocument *doc);
  // The document element, for delegated listeners; null while unloaded.
  AbcUiElement *(ABCUI_CALL *root)(AbcUiDocument *doc);
  // Recovery for a degraded document: re-attempt the main document. Returns
  // the load state afterwards.
  int32_t(ABCUI_CALL *retry)(AbcUiDocument *doc);
  // AbcUi >= 0.8.5: process-owned provider; revoked on failure/unload.
  AbcUiRecoveryRegistration(ABCUI_CALL *registerRecovery)(
      const AbcUiRecoveryDesc *desc);
  void(ABCUI_CALL *unregisterRecovery)(AbcUiRecoveryRegistration registration);
  // Deferred retry, safe inside DOM event dispatch. 1 queued, 0 invalid.
  int32_t(ABCUI_CALL *requestRetry)(AbcUiDocument *doc);
} AbcUiDocumentsApi;

// ---------------------------------------------------------------- menu ----

// The replacement GameUI's navigation state, published after every
// presentation sync as the payload of "menu.state" (transitions only) and
// "menu.paint" (every presentation beat while a page layer is up). Page
// modules present their own document when the reported top page is theirs;
// the framework owns the takeover, the input claim and the page stack.
//
// Numeric ids are part of the ABI: they mirror gameui::menu::MenuPage and
// BackendPhase.
#define ABCUI_MENU_PAGE_NONE (-1)
#define ABCUI_MENU_PAGE_MAIN 0
#define ABCUI_MENU_PAGE_PAUSE 1
#define ABCUI_MENU_PAGE_SETTINGS 2
#define ABCUI_MENU_PAGE_SERVERS 3
#define ABCUI_MENU_PAGE_CREATE_GAME 4
#define ABCUI_MENU_PAGE_PLAYER_LIST 5
#define ABCUI_MENU_PAGE_VOTE 6
#define ABCUI_MENU_PAGE_COMMAND 7
#define ABCUI_MENU_PAGE_INVENTORY 8
#define ABCUI_MENU_PAGE_LOADING 9

#define ABCUI_MENU_PHASE_BOOTING 0
#define ABCUI_MENU_PHASE_DISCONNECTED 1
#define ABCUI_MENU_PHASE_CONNECTING 2
#define ABCUI_MENU_PHASE_LOADING 3
#define ABCUI_MENU_PHASE_IN_GAME 4
#define ABCUI_MENU_PHASE_SHUTTING_DOWN 5
#define ABCUI_MENU_PHASE_FATAL 6

typedef struct AbcUiMenuState {
  uint32_t structSize;
  int32_t phase;       // ABCUI_MENU_PHASE_*
  int32_t page;        // top page, ABCUI_MENU_PAGE_NONE when the stack is empty
  int32_t up;          // the menu layer is presented
  int32_t loading;     // a load attempt owns the screen
  int32_t consoleOpen; // the console overlay is up
  int32_t hasLevel;    // a level is loaded
  int32_t canClose;    // closing the menu resumes gameplay
  uint32_t generation; // live connect/local attempt id, 0 = none
  // Overlays holding the screen, ABCUI_MENU_OVERLAY_* bitmask. A page
  // module must not steal keyboard focus onto itself while one is up.
  int32_t overlays;
  // The live attempt's kind (the loading page's title/status wording).
  int32_t attemptKind;
} AbcUiMenuState;

#define ABCUI_MENU_OVERLAY_CONSOLE 0x1
#define ABCUI_MENU_OVERLAY_CONFIRM_QUIT 0x2
#define ABCUI_MENU_OVERLAY_PASSWORD 0x4
#define ABCUI_MENU_OVERLAY_CONNECTION_ERROR 0x8
#define ABCUI_MENU_OVERLAY_DIRECT_CONNECT 0x10

#define ABCUI_MENU_ATTEMPT_NONE 0
#define ABCUI_MENU_ATTEMPT_CONNECT 1
#define ABCUI_MENU_ATTEMPT_LOCAL_GAME 2
#define ABCUI_MENU_ATTEMPT_MAP_LOAD 3

typedef struct AbcUiMenuApi {
  uint32_t structSize;
  void(ABCUI_CALL *state)(AbcUiMenuState *out);
  // Full open intent of one page (ensure the menu is up, reconcile the
  // phase, push the page, sync the presentation). 1 when the page is on top
  // afterwards. `page` is an ABCUI_MENU_PAGE_* value.
  int32_t(ABCUI_CALL *openPage)(int32_t page);
  // One back intent plus the presentation sync (the page's Cancel action).
  void(ABCUI_CALL *back)(void);
  // Raise the menu without pushing a page (idempotent); 1 when it is up.
  int32_t(ABCUI_CALL *ensureUp)(void);
  // Close the menu and resume gameplay (in-game only); 1 on a decided close.
  int32_t(ABCUI_CALL *resume)(void);
  // The shell's remaining fixed intents (the same typed controller events the
  // built-in shell sends; never raw engine commands):
  // - open the quit confirmation (the page module presents the overlay);
  // - leave the current game through the backend's disconnect path;
  // - toggle the replacement console overlay (close when it is up, otherwise
  //   open through the native bridge, then sync).
  void(ABCUI_CALL *quit)(void);
  void(ABCUI_CALL *disconnect)(void);
  void(ABCUI_CALL *toggleConsole)(void);
  // Toggles the direct-connect overlay (the shell's direct-connect entry,
  // when a definition declares it).
  void(ABCUI_CALL *directConnect)(void);
  // Resolves the quit confirmation overlay from its own buttons: 1 quits
  // through the backend, 0 cancels. The controller pops the overlay and runs
  // the confirmed quit exactly once.
  void(ABCUI_CALL *quitResolve)(int32_t confirmed);
  // Explicit process-scoped ownership of the native GameUI/console takeover.
  // Declare during Main/onLoad/onInit only. One module may own it;
  // failure/unload revokes it. Does not hook commands.
  int32_t(ABCUI_CALL *setNativeTakeover)(int32_t enabled);
  // Runs a replacement menu flow for votemenu, +/-commandmenu and
  // inventory/+inventory/-inventory. Call from a subscribeCommand handler.
  // 0 means unavailable/unknown, so the handler should allow the native path.
  int32_t(ABCUI_CALL *dispatchCommand)(const char *name);
  // AbcUi >= 0.8.5: confirmed exit, independent of the quit-dialog resources.
  void(ABCUI_CALL *quitNow)(void);
} AbcUiMenuApi;

// ------------------------------------------------------------- votemenu ----

// The vote-creation page's model (ban / kick / kill / map, the filtered list
// and the validated submit). The model, the roster seam and the server map
// list stay in the framework; a page module projects it and sends the
// returned command through console->serverCommand.
#define ABCUI_VOTE_KIND_BAN 0
#define ABCUI_VOTE_KIND_KICK 1
#define ABCUI_VOTE_KIND_KILL 2
#define ABCUI_VOTE_KIND_MAP 3

#define ABCUI_VOTE_NAME_MAX 64
#define ABCUI_VOTE_ID_MAX 24

// One visible row. `selected` is computed against the model's selection, so
// the page never keeps a second selection truth. `steamId` is the decimal
// SteamID64 (empty when unavailable); map rows carry `name` only.
typedef struct AbcUiVoteCandidate {
  uint32_t structSize;
  int32_t kind; // ABCUI_VOTE_KIND_*
  int32_t slot; // player slot (players only; 0 for maps)
  int32_t selected;
  char name[ABCUI_VOTE_NAME_MAX];     // game name / map token
  char realName[ABCUI_VOTE_NAME_MAX]; // Steam persona
  char steamId[ABCUI_VOTE_ID_MAX];
} AbcUiVoteCandidate;

typedef struct AbcUiVoteMenuApi {
  uint32_t structSize;
  // Fresh opening: Ban page, empty filter, no selection, live roster.
  void(ABCUI_CALL *prepare)(void);
  // Re-reads the live roster (no-op when the change gate says unchanged).
  void(ABCUI_CALL *refresh)(void);
  // Model revision; the page re-projects only when it moved.
  uint32_t(ABCUI_CALL *revision)(void);
  int32_t(ABCUI_CALL *kind)(void);
  void(ABCUI_CALL *setKind)(int32_t kind);
  void(ABCUI_CALL *setFilter)(const char *filter);
  int32_t(ABCUI_CALL *visibleCount)(void);
  // Copies row `index`; 0 when out of range (structSize honored on write).
  int32_t(ABCUI_CALL *visibleAt)(int32_t index, AbcUiVoteCandidate *out);
  // Index of the selected visible row, -1 when nothing is selected.
  int32_t(ABCUI_CALL *selectedIndex)(void);
  void(ABCUI_CALL *select)(int32_t index);
  // Revalidates the selection against the live roster and writes the exact
  // server command (e.g. "voteban <name>"). Returns 1; on refusal returns 0
  // and writes a localization token (or text) into `error`. Both buffers
  // follow the measure-then-copy contract; either may be null/0.
  int32_t(ABCUI_CALL *submit)(char *command, uint32_t commandSize, char *error,
                              uint32_t errorSize);
} AbcUiVoteMenuApi;

// ----------------------------------------------------------- playerlist ----

// The player-list page's model (roster snapshot, identity-keyed selection
// and the read-back mute toggle). The roster seam and the voice mute
// backend stay in the framework; a page module projects the snapshot and
// reports the select/toggle/back intents. Selection is keyed by the row's
// identity (SteamID64 when present, slot+name otherwise), so "same slot,
// new player" invalidates the selection instead of retargeting the action.
#define ABCUI_PL_FLAG_LOCAL 0x1
#define ABCUI_PL_FLAG_SPECTATING 0x2
#define ABCUI_PL_FLAG_BOT 0x4
#define ABCUI_PL_FLAG_MUTED 0x8

#define ABCUI_PL_NAME_MAX 64

// One roster row. `selected` is computed against the model's selection, so
// the page never keeps a second selection truth. `steamId64` is 0 for bots
// and LAN placeholders (their key is slot+name).
typedef struct AbcUiPlayerRow {
  uint32_t structSize;
  int32_t slot;       // 1..32 entity slot
  int32_t selected;   // computed against the model's selection
  uint32_t flags;     // ABCUI_PL_FLAG_*
  uint64_t steamId64; // 0 = no SteamID (bot / LAN placeholder)
  char name[ABCUI_PL_NAME_MAX];
} AbcUiPlayerRow;

// toggleMute outcomes (the model's revalidated toggle discipline: success
// only when the immediate backend read-back agrees with the request).
#define ABCUI_PL_TOGGLE_OK 0
#define ABCUI_PL_TOGGLE_UNAVAILABLE 1
#define ABCUI_PL_TOGGLE_NO_SELECTION 2
#define ABCUI_PL_TOGGLE_STALE 3
#define ABCUI_PL_TOGGLE_IDENTITY_CHANGED 4
#define ABCUI_PL_TOGGLE_LOCAL_PLAYER 5
#define ABCUI_PL_TOGGLE_READBACK_FAILED 6

typedef struct AbcUiPlayerListApi {
  uint32_t structSize;
  // Re-reads the live roster and revalidates the selection (idempotent per
  // frame; the page calls it before projecting).
  void(ABCUI_CALL *refresh)(void);
  // Model revision; the page re-projects only when it moved.
  uint32_t(ABCUI_CALL *revision)(void);
  int32_t(ABCUI_CALL *rosterCount)(void);
  // Copies row `index`; 0 when out of range (structSize honored on write).
  int32_t(ABCUI_CALL *rosterAt)(int32_t index, AbcUiPlayerRow *out);
  // Selects by the row's identity (the same values the row carried: a
  // SteamID64, or slot+name when the id is 0). A key without a live
  // occupant is refused, not remembered.
  void(ABCUI_CALL *select)(uint64_t steamId64, int32_t slot, const char *name);
  int32_t(ABCUI_CALL *muteAvailable)(void);
  // Revalidated toggle for the current selection. Returns an
  // ABCUI_PL_TOGGLE_* code and writes the read-back state on OK (either
  // argument may be null/absent).
  int32_t(ABCUI_CALL *toggleMute)(int32_t *mutedAfter);
} AbcUiPlayerListApi;

// --------------------------------------------------------- commandmenu ----

// The in-game command menu's model: the parsed commandmenu.json tree and its
// column navigation. The definition JSON and its parser stay in the
// framework; a page module projects the open columns and drives the
// navigation. Activations execute framework-side: a command result closes
// the menu first and then runs the engine command (the shared command path
// may synchronously raise another page), a close result lowers the page
// with the flow's raised-menu discipline - the page only re-projects.
#define ABCUI_CMDMENU_RESULT_NONE 0
#define ABCUI_CMDMENU_RESULT_SUBMENU 1
#define ABCUI_CMDMENU_RESULT_COMMAND 2
#define ABCUI_CMDMENU_RESULT_CLOSE 3

// The parser bounds labels at this length.
#define ABCUI_CMDMENU_LABEL_MAX 512

// One item of an open column. `hasItems` marks a submenu branch.
typedef struct AbcUiCommandMenuItem {
  uint32_t structSize;
  int32_t hasItems;
  char label[ABCUI_CMDMENU_LABEL_MAX];
} AbcUiCommandMenuItem;

typedef struct AbcUiCommandMenuApi {
  uint32_t structSize;
  uint32_t(ABCUI_CALL *revision)(void);
  // Definition title token (may be empty; the page falls back to its own).
  const char *(ABCUI_CALL *title)(void);
  // The open columns; the projection window is fixed at three (the page's
  // composition shows three consecutive levels at once).
  int32_t(ABCUI_CALL *columnCount)(void);
  int32_t(ABCUI_CALL *firstVisibleColumn)(void);
  int32_t(ABCUI_CALL *visibleColumnCount)(void);
  int32_t(ABCUI_CALL *itemCountAt)(int32_t column);
  int32_t(ABCUI_CALL *selectedAt)(int32_t column);
  // Copies item `index` of `column`; 0 out of range (structSize honored).
  int32_t(ABCUI_CALL *itemAt)(int32_t column, int32_t index,
                              AbcUiCommandMenuItem *out);
  // Moves the deepest column's selection; 1 when it moved.
  int32_t(ABCUI_CALL *moveSelection)(int32_t delta);
  // Navigation/activation; every call returns an ABCUI_CMDMENU_RESULT_*.
  int32_t(ABCUI_CALL *openSelected)(void);
  int32_t(ABCUI_CALL *activateSelected)(void);
  int32_t(ABCUI_CALL *activateAt)(int32_t column, int32_t index);
  // `digit` is 0..9 (the one-based gutter number is the page's markup).
  int32_t(ABCUI_CALL *activateShortcut)(int32_t digit);
  // Pops one open column; kClose at the root (the caller follows with
  // dismiss).
  int32_t(ABCUI_CALL *back)(void);
  // The full dismiss gesture (ESC): the flow's close, raised-menu latch
  // included.
  void(ABCUI_CALL *dismiss)(void);
} AbcUiCommandMenuApi;

// ----------------------------------------------------------- menuconfig ----

// The menu shell's definition (uiconfig's mainmenu.json: the title and the
// main/pause entry lists) plus the engine facts the shell projects. The
// JSON and its parser stay in the framework; a shell module builds the
// entry buttons from this section and resolves the fixed action names
// through the menu section's typed intents.
#define ABCUI_MENU_SECTION_MAIN 0
#define ABCUI_MENU_SECTION_PAUSE 1

// The parser bounds ids at 96 bytes and free text at 1024.
#define ABCUI_MENU_ID_MAX 96
#define ABCUI_MENU_LABEL_MAX 1024

// The character card: the player name line and the Studio model the live
// <model> element renders (an empty model path hides the preview box).
typedef struct AbcUiPlayerCard {
  uint32_t structSize;
  char playerName[ABCUI_MENU_LABEL_MAX];
  char modelPath[ABCUI_MENU_LABEL_MAX];
  char modelLabel[ABCUI_MENU_LABEL_MAX];
} AbcUiPlayerCard;

typedef struct AbcUiMenuConfigEntry {
  uint32_t structSize;
  char id[ABCUI_MENU_ID_MAX];        // anchors the DOM node
  char label[ABCUI_MENU_LABEL_MAX];  // "#token" or literal text
  char action[ABCUI_MENU_ID_MAX];    // fixed intent name (menu section)
  char condition[ABCUI_MENU_ID_MAX]; // always | multiplayer | singleplayer
} AbcUiMenuConfigEntry;

typedef struct AbcUiMenuConfigApi {
  uint32_t structSize;
  // Startup only: configuration root in the caller's package. The menu
  // controller parses this catalog during its onInit. No built-in defaults.
  int32_t(ABCUI_CALL *setResourceRoot)(const char *uri);

  // Definition title token (may be empty; the shell falls back to its own).
  const char *(ABCUI_CALL *title)(void);
  int32_t(ABCUI_CALL *entryCount)(int32_t section); // ABCUI_MENU_SECTION_*
  // Copies entry `index`; 0 out of range (structSize honored on write).
  int32_t(ABCUI_CALL *entryAt)(int32_t section, int32_t index,
                               AbcUiMenuConfigEntry *out);
  // Backend facts the shell projects (re-read per presentation beat):
  // the maxclients split (Main vs Pause rows and their conditions), the
  // character card (engine cvars; empty strings when unregistered) and
  // the footer build note.
  int32_t(ABCUI_CALL *multiplayer)(void);
  int32_t(ABCUI_CALL *playerCard)(AbcUiPlayerCard *out);
  int32_t(ABCUI_CALL *buildNote)(char *out, uint32_t outSize);
} AbcUiMenuConfigApi;

// --------------------------------------------------------------- social ----

// The shell's friends board (services::friends): a bounded, sorted top list
// with the local player first, refreshed on the model's throttle. `join`
// hands one row's intent back to the framework, which validates the
// address with the direct-connect grammar before it becomes an attempt.
#define ABCUI_SOCIAL_TEXT_MAX 128
#define ABCUI_SOCIAL_CONNECT_MAX 64

typedef struct AbcUiFriendRow {
  uint32_t structSize;
  uint64_t steamId; // 0 = no Steam id (avatar omitted)
  int32_t self;     // row 0 is the local player (the model's row order)
  int32_t inGame;
  // The friend plays a game that is not this one; the board names that
  // fact instead of printing the other game's AppID.
  int32_t otherGame;
  char name[ABCUI_SOCIAL_TEXT_MAX];
  char game[ABCUI_SOCIAL_TEXT_MAX]; // joinable friends carry this game's name
  char connect[ABCUI_SOCIAL_CONNECT_MAX]; // "ip:port" when joinable
} AbcUiFriendRow;

typedef struct AbcUiSocialApi {
  uint32_t structSize;
  // The model's generation; the page re-projects only when it moved.
  uint32_t(ABCUI_CALL *generation)(void);
  int32_t(ABCUI_CALL *steamAvailable)(void);
  int32_t(ABCUI_CALL *rowCount)(void);
  // Copies row `index`; 0 out of range (structSize honored on write).
  int32_t(ABCUI_CALL *rowAt)(int32_t index, AbcUiFriendRow *out);
  // One friend row's join intent (validated framework-side).
  void(ABCUI_CALL *join)(uint64_t steamId, const char *address);
} AbcUiSocialApi;

// ------------------------------------------------------------ inventory ----

// The item window's model: the list the server streams through InvAdd/
// InvRemove plus the selection. Actions execute framework-side (the native
// order: the window closes, then the client command runs); the read-through
// rule means a refused action never mutates the list - an item disappears
// only when the server's InvRemove arrives.
#define ABCUI_INV_TEXT_MAX 512

typedef struct AbcUiInvItem {
  uint32_t structSize;
  uint64_t serial;     // model-local identity (the click payload)
  int32_t selected;    // computed against the model's selection
  int32_t canActivate; // warning tint, not a disable (the server decides)
  int32_t canDrop;
  char name[ABCUI_INV_TEXT_MAX];
  char description[ABCUI_INV_TEXT_MAX];
} AbcUiInvItem;

// Action verbs (the native client commands they issue).
#define ABCUI_INV_ACTION_ACTIVATE 0
#define ABCUI_INV_ACTION_DROP 1
#define ABCUI_INV_ACTION_DROP_ALL 2

typedef struct AbcUiInventoryApi {
  uint32_t structSize;
  uint32_t(ABCUI_CALL *revision)(void);
  int32_t(ABCUI_CALL *itemCount)(void);
  // Copies item `index`; 0 out of range (structSize honored on write).
  int32_t(ABCUI_CALL *itemAt)(int32_t index, AbcUiInvItem *out);
  // Selects an existing serial; an unknown serial clears the selection.
  void(ABCUI_CALL *select)(uint64_t serial);
  // Keyboard walk (clamps at the ends); 1 when the selection moved.
  int32_t(ABCUI_CALL *moveSelection)(int32_t delta);
  // The action's client command: 1 when issued (the window already
  // closed), 0 when the action has no target (no selection, or drop-all
  // on an empty list - the window stays and the page re-projects).
  int32_t(ABCUI_CALL *activate)(int32_t action);
  // The full dismiss gesture (close button, scrim, ESC): the flow's close
  // with the raised-menu discipline.
  void(ABCUI_CALL *dismiss)(void);
} AbcUiInventoryApi;

// ----------------------------------------------------------- creategame ----

// The create-local-game page's form data (the enumerated map catalog and
// the server options from settings.scr) plus the validated submit. The
// model, the catalog enumeration and the launch path stay framework-side;
// the page builds the form and sends the fields once on Apply.
#define ABCUI_CG_NAME_MAX 96
#define ABCUI_CG_LABEL_MAX 256
#define ABCUI_CG_VALUE_MAX 256

// One server option (a settings.scr row): a toggle or enum renders as a
// select; a bounded numeric renders as a slider; anything else is a text
// field.
typedef struct AbcUiCgOption {
  uint32_t structSize;
  char name[ABCUI_CG_NAME_MAX];
  char label[ABCUI_CG_LABEL_MAX];
  int32_t toggle;
  int32_t hasChoices;
  int32_t slider; // bounded numeric (min/max valid)
  float min;
  float max;
  float defaultValue;
  int32_t choiceCount;
} AbcUiCgOption;

// One enum choice of an option (the select's label/value pair).
typedef struct AbcUiCgChoice {
  uint32_t structSize;
  float value;
  char label[ABCUI_CG_LABEL_MAX];
} AbcUiCgChoice;

// One submitted field. The fixed form fields are "map", "players", "name",
// "password" and "lan" ("1"/"0"); every option follows with its control's
// value under the option's name.
typedef struct AbcUiCgField {
  uint32_t structSize;
  char name[ABCUI_CG_NAME_MAX];
  char value[ABCUI_CG_VALUE_MAX];
} AbcUiCgField;

typedef struct AbcUiCreateGameApi {
  uint32_t structSize;
  // Fresh opening defaults: the live hostname (empty -> the page projects
  // its localized default token).
  const char *(ABCUI_CALL *defaultName)(void);
  uint32_t(ABCUI_CALL *mapCount)(void);
  // Catalog name `index` (stable per world; the module rebuilds the picker
  // on every fresh open).
  const char *(ABCUI_CALL *mapAt)(int32_t index);
  int32_t(ABCUI_CALL *optionCount)(void);
  int32_t(ABCUI_CALL *optionAt)(int32_t index, AbcUiCgOption *out);
  int32_t(ABCUI_CALL *choiceAt)(int32_t option, int32_t index,
                                AbcUiCgChoice *out);
  // Apply: assembles and validates the request from the fields. 1 when
  // accepted (the attempt dispatched, the request kept for the local
  // retry); 0 on refusal with a hint token/text in `hint` (the page can
  // correct and re-Apply - the latch re-arms on any shown hint).
  int32_t(ABCUI_CALL *submit)(const AbcUiCgField *fields, int32_t fieldCount,
                              char *hint, uint32_t hintSize);
} AbcUiCreateGameApi;

// -------------------------------------------------------------- console ----

// The replacement console's data and services: the bounded engine output
// mirror (the page's local input records ride the same scrollback), the
// submission channel with its client-catalog answer, the input history and
// the completion catalog. The projection discipline (bounded window,
// incremental appends, tail follow, honest scroll) stays page-side.
#define ABCUI_CONSOLE_TEXT_MAX 1024 // the buffer's own line byte cap

// Console snapshots use caller-provided byte strides.
typedef struct AbcUiConsoleLine {
  uint32_t structSize;
  int32_t developer; // assembled from DPrintf fragments
  int32_t truncated; // the buffer cut the line (the page appends " ...")
  char text[ABCUI_CONSOLE_TEXT_MAX]; // null-terminated copy
} AbcUiConsoleLine;

#define ABCUI_CONSOLE_NAME_MAX 128

typedef struct AbcUiConsoleSuggestion {
  uint32_t structSize;
  char name[ABCUI_CONSOLE_NAME_MAX];
} AbcUiConsoleSuggestion;

typedef struct AbcUiConsolePageApi {
  uint32_t structSize;
  // Output mirror. The sequence is process-monotonic and never reused; a
  // generation change means the buffer was cleared and the view restarts
  // from the tail window.
  uint64_t(ABCUI_CALL *generation)(void);
  uint64_t(ABCUI_CALL *latestSequence)(void);
  int32_t(ABCUI_CALL *lineCount)(void);
  // Copies up to `max` lines newer than `since` (the newest window when
  // more qualify); returns the count. Every row needs a nonzero structSize
  // no larger than the supplied byte stride.
  int32_t(ABCUI_CALL *copySince)(uint64_t since, void *out, uint32_t max,
                                 uint32_t stride);
  // One local line into the same scrollback (the "] <command>" input
  // record; one call is one complete line).
  void(ABCUI_CALL *pushLocalLine)(const char *text);
  // The submission channel: validates and hands one command line to the
  // engine command buffer. Returns 1 when executed; `unknown` (optional)
  // then reports the client-catalog answer (1 = the verb is unknown here:
  // the engine prints nothing for it and the console answers itself).
  int32_t(ABCUI_CALL *submit)(const char *command, int32_t *unknown);
  // The console's own "unknown command" answer (localized), written into
  // the scrollback exactly where the vanilla console put its messages.
  void(ABCUI_CALL *noteUnknownCommand)(const char *command);
  // Input history (oldest first, newest last; sensitive lines absent).
  int32_t(ABCUI_CALL *historyCount)(void);
  int32_t(ABCUI_CALL *historyAt)(int32_t index, char *out, uint32_t outSize);
  // Case-insensitive prefix completion over the runtime catalogs (sorted,
  // exact-name deduplicated). Returns the match count which MAY exceed
  // `max` (the "more exist" row); `out` receives at most `max`.
  int32_t(ABCUI_CALL *complete)(const char *prefix, void *out, uint32_t max,
                                uint32_t stride);
  // The close funnel (the page's close button): the same bridge request
  // path the engine console hotkey uses, engine flag included.
  void(ABCUI_CALL *requestClose)(void);
  // ESC layering: the page claims an ESC press it consumed on one of its
  // own layers (context menu / confirm pane / completion list); the menu
  // owner's routed ESC reads-and-clears the claim so one physical press
  // never closes two layers.
  void(ABCUI_CALL *noteEscapeClaim)(void);
} AbcUiConsolePageApi;

// -------------------------------------------------------- serverbrowser ----

// The find-servers page's data (the query snapshot through the filter/sort/
// paging view model) and its intents. The query session, the Steam
// favorites/history stores and the blacklist (with its save sink) stay in
// the framework; the page projects the bounded page window and reports the
// fixed intents. Read-through rules: an entry without a response shows its
// unknown facts as such (the module formats "?" / "-"), and the selected
// server's favorite state is Steam's own truth.
#define ABCUI_SB_TAB_INTERNET 0
#define ABCUI_SB_TAB_LAN 1
#define ABCUI_SB_TAB_FAVORITES 2
#define ABCUI_SB_TAB_HISTORY 3
#define ABCUI_SB_TAB_FRIENDS 4

#define ABCUI_SB_STATUS_IDLE 0
#define ABCUI_SB_STATUS_QUERYING 1
#define ABCUI_SB_STATUS_COMPLETED 2
#define ABCUI_SB_STATUS_CANCELLED 3
#define ABCUI_SB_STATUS_FAILED 4

#define ABCUI_SB_FAILURE_UNAVAILABLE 0
#define ABCUI_SB_FAILURE_REJECTED 1
#define ABCUI_SB_FAILURE_TIMED_OUT 2
#define ABCUI_SB_FAILURE_MASTER_NO_RESPONSE 3

#define ABCUI_SB_DETAIL_OFF 0
#define ABCUI_SB_DETAIL_PLAYERS 1

#define ABCUI_SB_SORT_NAME 0
#define ABCUI_SB_SORT_MAP 1
#define ABCUI_SB_SORT_PLAYERS 2
#define ABCUI_SB_SORT_PING 3

// The bounded page window (one render page of the list).
#define ABCUI_SB_PAGE_SIZE 64

#define ABCUI_SB_TEXT_MAX 128
#define ABCUI_SB_MAP_MAX 64

typedef struct AbcUiSbEntry {
  uint32_t structSize;
  int32_t responded;
  int32_t secure;
  int32_t password;
  int32_t players;
  int32_t maxPlayers;
  int32_t botPlayers;
  int32_t pingMs; // negative = unknown
  uint32_t ip;    // host byte order, 0 = unknown
  int32_t connectionPort;
  int32_t queryPort;
  char name[ABCUI_SB_TEXT_MAX];
  char map[ABCUI_SB_MAP_MAX];
  char gameDir[ABCUI_SB_MAP_MAX];
} AbcUiSbEntry;

typedef struct AbcUiSbPlayer {
  uint32_t structSize;
  int32_t score;
  int32_t seconds; // truncated play time (the page formats h:mm:ss)
  char name[ABCUI_SB_TEXT_MAX];
} AbcUiSbPlayer;

typedef struct AbcUiSbFilter {
  uint32_t structSize;
  int32_t hideEmpty;
  int32_t hideFull;
  int32_t secureOnly;
  int32_t noPassword;
  int32_t insecureOnly;
  int32_t hideUnresponsive; // cvar-owned; the framework re-samples per open
  int32_t maxPing;          // 0 = any
  char gameDir[ABCUI_SB_MAP_MAX];
  char name[ABCUI_SB_TEXT_MAX];
  char map[ABCUI_SB_TEXT_MAX];
} AbcUiSbFilter;

// One blacklist rule: the blocked server, its address and the moment it was
// blocked (0 = unknown - never fabricated).
typedef struct AbcUiSbRule {
  uint32_t structSize;
  uint64_t blockedAt;
  char name[ABCUI_SB_TEXT_MAX];
  char address[ABCUI_SB_MAP_MAX];
} AbcUiSbRule;

typedef struct AbcUiServerBrowserApi {
  uint32_t structSize;
  // Query snapshot facts.
  int32_t(ABCUI_CALL *status)(void); // ABCUI_SB_STATUS_*
  int32_t(ABCUI_CALL *failure)(
      void); // ABCUI_SB_FAILURE_* (meaningful when failed)
  int32_t(ABCUI_CALL *kind)(void); // ABCUI_SB_TAB_*
  uint32_t(ABCUI_CALL *generation)(void);
  int32_t(ABCUI_CALL *serverCount)(void);
  int32_t(ABCUI_CALL *noServersListed)(void);
  int32_t(ABCUI_CALL *detailKind)(void);   // ABCUI_SB_DETAIL_*
  int32_t(ABCUI_CALL *detailStatus)(void); // ABCUI_SB_STATUS_*
  int32_t(ABCUI_CALL *detailFailed)(void);
  int32_t(ABCUI_CALL *playerCount)(void);
  // View-model beat: re-syncs the filter/sort/paging view from the
  // snapshot; 1 when the view moved (the page rebuilds once).
  int32_t(ABCUI_CALL *sync)(void);
  // The bounded page window (the list view; the blacklist view counts
  // rules through ruleCount instead).
  int32_t(ABCUI_CALL *rowCount)(void);
  int32_t(ABCUI_CALL *page)(void);
  int32_t(ABCUI_CALL *pageCount)(void);
  int32_t(ABCUI_CALL *sortColumn)(void); // ABCUI_SB_SORT_*
  int32_t(ABCUI_CALL *sortAscending)(void);
  int32_t(ABCUI_CALL *blockedOnly)(void);
  // Copies the entry shown at page row `row`; 0 when the row is empty.
  int32_t(ABCUI_CALL *entryAt)(int32_t row, AbcUiSbEntry *out);
  // Copies detail player `index`; 0 out of range.
  int32_t(ABCUI_CALL *playerAt)(int32_t index, AbcUiSbPlayer *out);
  int32_t(ABCUI_CALL *selectedRow)(void); // -1 when nothing is selected
  // The selected server's favorite state (Steam read-through); -1 when
  // nothing is selected or the address is unrepresentable.
  int32_t(ABCUI_CALL *selectedFavorite)(void);
  // The favorites store is reachable (the row menu's favorite entry dims
  // when it is not).
  int32_t(ABCUI_CALL *favoritesAvailable)(void);
  int32_t(ABCUI_CALL *filter)(AbcUiSbFilter *out);
  // View actions (the page's own state through the model).
  // Selects a page row; `requestPlayers` issues the single-slot players
  // query (the left click opens the detail window; a right click stays a
  // view action). Either way the selection replaces the previous detail
  // request and the view syncs.
  void(ABCUI_CALL *selectRow)(int32_t row, int32_t requestPlayers);
  void(ABCUI_CALL *setSort)(int32_t column, int32_t ascending);
  int32_t(ABCUI_CALL *pagePrev)(void);
  int32_t(ABCUI_CALL *pageNext)(void);
  void(ABCUI_CALL *setFilter)(const AbcUiSbFilter *filter);
  // Restore the filter defaults (the game identity and the cvar-owned
  // unresponsive rule stay); 1 = the master pairs moved, re-issue the
  // query.
  int32_t(ABCUI_CALL *resetFilters)(void);
  void(ABCUI_CALL *setBlockedOnly)(int32_t blocked); // cancels the detail slot
  // Blacklist rules.
  int32_t(ABCUI_CALL *ruleCount)(void);
  uint32_t(ABCUI_CALL *ruleRevision)(void);
  int32_t(ABCUI_CALL *ruleAt)(int32_t index, AbcUiSbRule *out);
  int32_t(ABCUI_CALL *blockSelected)(int32_t entireIp);   // 1 saved, 0 refused
  int32_t(ABCUI_CALL *blockAddress)(const char *address); // 1 saved, 0 refused
  int32_t(ABCUI_CALL *removeRule)(const char *address);   // 1 saved, 0 refused
  // Intents (the query lifecycle and the Steam mutations, framework-side).
  void(ABCUI_CALL *setTab)(int32_t kind);
  void(ABCUI_CALL *refresh)(void);
  void(ABCUI_CALL *stop)(void);
  void(ABCUI_CALL *connect)(void);
  void(ABCUI_CALL *toggleFavorite)(void);
  void(ABCUI_CALL *applyFilters)(
      void); // the master pairs moved: re-issue the query
  void(ABCUI_CALL *closeDetail)(void); // the window is the only slot consumer
  // The join auto-retry toggle's truth (the framework's retry path reads
  // its copy when a join fails).
  void(ABCUI_CALL *setAutoRetryJoin)(int32_t enabled);
  // ESC layering: the page claims an ESC press it spent on one of its
  // subpages (row menu / detail window); the menu owner's routed ESC
  // reads-and-clears it so one press never leaves the page AND closes a
  // subpage.
  void(ABCUI_CALL *noteEscapeClaim)(void);
} AbcUiServerBrowserApi;

// ------------------------------------------------------------ connection ----

// The T14 connection surfaces' data and intents: the loading attempt's
// stream facts (the framework drains the engine feed into the model), the
// error overlay's controller-owned reason, and the typed intents (cancel,
// retry, return, the password submit, the validated direct connect). The
// presentation latches are page-side.
typedef struct AbcUiConnectionApi {
  uint32_t structSize;
  // The attempt model's facts (a retired generation keeps answering until
  // the next start; the page renders whatever the model holds).
  uint32_t(ABCUI_CALL *generation)(void);
  int32_t(ABCUI_CALL *canceled)(
      void); // the once-only cancel latch (visual state)
  int32_t(ABCUI_CALL *address)(char *out, uint32_t outSize);
  // The engine's streamed status text (empty before the first callback).
  int32_t(ABCUI_CALL *statusText)(char *out, uint32_t outSize);
  int32_t(ABCUI_CALL *resourceText)(char *out, uint32_t outSize);
  int32_t(ABCUI_CALL *secondaryText)(char *out, uint32_t outSize);
  float(ABCUI_CALL *secondaryFraction)(void);
  int32_t(ABCUI_CALL *primaryKnown)(void);
  float(ABCUI_CALL *primaryFraction)(void);
  // The connection-error overlay's reason (the controller's owned copy;
  // empty when none).
  int32_t(ABCUI_CALL *errorReason)(char *out, uint32_t outSize);
  // The password dialog's lead-line state (the second prompt of a
  // generation means the previous password was rejected).
  int32_t(ABCUI_CALL *passwordInvalid)(void);
  // Intents.
  int32_t(ABCUI_CALL *cancelLoading)(
      void); // the model latch; 0 when already latched
  void(ABCUI_CALL *retryFailed)(void); // a NEW attempt generation (typed retry)
  void(ABCUI_CALL *returnHome)(void);  // back to the no-map home
  // Hands the typed password to the controller (never stored, never
  // logged); 0 for an empty submission.
  int32_t(ABCUI_CALL *submitPassword)(const char *password);
  void(ABCUI_CALL *passwordCancel)(
      void); // the attempt survives (vanilla parity)
  // Validates the address with the model's grammar, then dispatches the
  // attempt; 0 refused with the hint token in `error`.
  int32_t(ABCUI_CALL *connectAddress)(const char *address, char *error,
                                      uint32_t errorSize);
} AbcUiConnectionApi;

// ------------------------------------------------------------ settings ----

// The settings page's data and transactions. The four models (cvar rows, key
// bindings, display modes, microphone hardware), the JSON page tree, the
// model-favorite store and the spray maker stay framework-side; a page module
// projects them and calls back through this section. Read-through rules:
//
//  - a row's draft is the ONLY value an edit may change; nothing here writes
//    an engine value outside apply(), and a refused write keeps the draft;
//  - kCommandToggle rows land kQueued after apply (the engine command buffer
//    flushes between frames) - the page re-snapshots while hasQueued() until
//    the state settles;
//  - a key-binding capture session stays framework-side: beginCapture() asks
//    for one physical edge, the framework owns the router token and feeds the
//    model, and the page re-projects from the bind rows (capturing()/
//    conflictPending());
//  - the display-mode transaction stays framework-side too (apply() persists
//    a recovery record, writes the changed commands and restarts), so the
//    page reports videoApply() and follows videoFacts();
//  - the spray maker (decode/encode/write + the preview texture upload) is a
//    framework service: sprayMake() starts it and the page reads the stored
//    answer back (the picker's directory state is page-side).
//
// Row/section text limits mirror the JSON parser's bounds.

#define ABCUI_SETTINGS_ID_MAX 96
#define ABCUI_SETTINGS_LABEL_MAX 256
#define ABCUI_SETTINGS_CVAR_MAX 64
#define ABCUI_SETTINGS_TEXT_MAX 256
#define ABCUI_SETTINGS_KEY_MAX 64

// The row's control kind (mirrors services::settings::SettingKind).
#define ABCUI_SET_KIND_BOOL 0
#define ABCUI_SET_KIND_FLOAT 1
#define ABCUI_SET_KIND_ENUM 2
#define ABCUI_SET_KIND_NEGATE 3
#define ABCUI_SET_KIND_TEXT 4
#define ABCUI_SET_KIND_STRING_ENUM 5
#define ABCUI_SET_KIND_COMMAND_TOGGLE 6

// The row's lifecycle (mirrors services::settings::RowState).
#define ABCUI_SET_STATE_CLEAN 0
#define ABCUI_SET_STATE_DIRTY 1
#define ABCUI_SET_STATE_INVALID 2
#define ABCUI_SET_STATE_CONFLICT 3
#define ABCUI_SET_STATE_QUEUED 4
#define ABCUI_SET_STATE_APPLY_FAILED 5
#define ABCUI_SET_STATE_MISSING 6

// Row facts (AbcUiSettingRow::flags).
#define ABCUI_SET_ROW_BOUNDED 0x1      // a real [min, max] domain
#define ABCUI_SET_ROW_READ_ONLY 0x2    // shown, never editable
#define ABCUI_SET_ROW_HUE_SWATCH 0x4   // the value is a player-palette hue
#define ABCUI_SET_ROW_FILE_CHOICES 0x8 // scanned values are file paths
#define ABCUI_SET_ROW_HAS_DEFAULT 0x10 // the default is verified
#define ABCUI_SET_ROW_REQUIRE_ENABLED 0x20
#define ABCUI_SET_ROW_MISSING 0x40  // the cvar is not registered
#define ABCUI_SET_ROW_EDITABLE 0x80 // editable(cvar) right now

// One projected row. The static LIST options (optionCount/optionAt) and the
// scanned string choices (choiceCount/choiceAt) are read through the section
// by cvar; this struct carries only the row's own facts and its current
// draft/engine values.
typedef struct AbcUiSettingRow {
  uint32_t structSize;
  char cvar[ABCUI_SETTINGS_CVAR_MAX];
  int32_t kind;        // ABCUI_SET_KIND_*
  int32_t state;       // ABCUI_SET_STATE_*
  int32_t flags;       // ABCUI_SET_ROW_*
  int32_t optionCount; // static enum options
  int32_t choiceCount; // scanned string choices
  float minValue;
  float maxValue;
  float step;
  float defaultValue;
  float engineValue;
  float draftValue; // meaningful when state != ABCUI_SET_STATE_INVALID
  char labelToken[ABCUI_SETTINGS_LABEL_MAX]; // "#Settings_*" or literal
  char section[ABCUI_SETTINGS_ID_MAX];       // the row's section id
  char stateName[ABCUI_SETTINGS_ID_MAX];     // kCommandToggle state / scan
  char requireCvar[ABCUI_SETTINGS_ID_MAX];   // dependency cvar (may be empty)
  char engineText[ABCUI_SETTINGS_TEXT_MAX];  // kText/kStringEnum engine value
  char draftText[ABCUI_SETTINGS_TEXT_MAX];   // committed field text
} AbcUiSettingRow;

// One page of the JSON page tree (settings/*.json).
typedef struct AbcUiSettingsPage {
  uint32_t structSize;
  int32_t sectionCount;
  int32_t cvarCount;
  char id[ABCUI_SETTINGS_ID_MAX];
  char label[ABCUI_SETTINGS_LABEL_MAX];
  char description[ABCUI_SETTINGS_LABEL_MAX];
} AbcUiSettingsPage;

// One declared section of a page: the card the rows group into, plus the
// widget components it declares.
typedef struct AbcUiSettingsSection {
  uint32_t structSize;
  int32_t componentCount;
  char id[ABCUI_SETTINGS_ID_MAX];
  char label[ABCUI_SETTINGS_LABEL_MAX];
  char layout[ABCUI_SETTINGS_ID_MAX]; // "two-up" or empty
} AbcUiSettingsSection;

// The applied-row tally apply() reports (the page composes its status line).
typedef struct AbcUiSettingsApply {
  uint32_t structSize;
  int32_t applied;
  int32_t queued; // engine command buffer (kCommandToggle rows)
  int32_t failed;
} AbcUiSettingsApply;

// The key-binding section's per-action lifecycle (mirrors
// services::settings::BindRowState).
#define ABCUI_BIND_STATE_CLEAN 0
#define ABCUI_BIND_STATE_DIRTY 1
#define ABCUI_BIND_STATE_QUEUED 2
#define ABCUI_BIND_STATE_APPLY_FAILED 3
#define ABCUI_BIND_STATE_UNAVAILABLE 4

// One kb_act.lst row. Header rows carry no command; the label is the raw
// file text ("#Valve_*" or a literal - resolve it through ui->translate or
// bindLookupLabel). The draft key names are read with bindKeyAt().
typedef struct AbcUiBindAction {
  uint32_t structSize;
  int32_t index; // action index (the DOM anchor and every write target)
  int32_t header;
  int32_t state;    // ABCUI_BIND_STATE_*
  int32_t keyCount; // draft keys (bindKeyAt)
  char label[ABCUI_SETTINGS_LABEL_MAX];
  char command[ABCUI_SETTINGS_TEXT_MAX];
} AbcUiBindAction;

// The display-mode facts (mirrors services::settings::VideoModeModel).
typedef struct AbcUiVideoFacts {
  uint32_t structSize;
  int32_t available;        // the engine queries are reachable
  int32_t actualKnown;      // the actual mode could be read
  int32_t awaitingConfirm;  // an unconfirmed switch owns the screen
  int32_t hasStaged;        // a staged request waits for videoApply
  int32_t remainingSeconds; // confirmation window countdown
  int32_t modeCount;        // enumerated modes (videoModeAt)
  int32_t actualWidth;
  int32_t actualHeight;
  int32_t actualWindowed;
  int32_t stagedWidth; // the staged target (staged over actual)
  int32_t stagedHeight;
  int32_t stagedWindowed;
  // While awaitingConfirm: how the actual mode compares with the requested
  // one (ABCUI_VIDEO_CONFIRM_*) and the requested mode the recovery record
  // holds.
  int32_t confirmKind;
  int32_t requestedWidth;
  int32_t requestedHeight;
  int32_t requestedWindowed;
} AbcUiVideoFacts;

#define ABCUI_VIDEO_CONFIRM_APPLIED 0     // actual == requested
#define ABCUI_VIDEO_CONFIRM_NOT_APPLIED 1 // the switch never took
#define ABCUI_VIDEO_CONFIRM_UNKNOWN 2     // unreadable / matches neither

// The microphone hardware facts (mirrors services::settings::VoiceModel).
// Volume is the engine scale 0..1 (the page shows x100); speakingLevel is
// the engine's own GetSpeakingVolume answer, never an animation.
typedef struct AbcUiVoiceFacts {
  uint32_t structSize;
  int32_t available;
  int32_t testing;
  int32_t micBoost;
  int32_t micBoostDraft;
  int32_t micBoostDirty;
  int32_t micDirty;
  float micVolume;
  float micVolumeDraft;
  float speakingLevel;
} AbcUiVoiceFacts;

typedef struct AbcUiSettingsApi {
  uint32_t structSize;
  // Model generation: moves whenever anything the page projects changed
  // (drafts, states, capture, video stage, voice, favorites, spray answer).
  // The page re-projects only when it moved.
  uint32_t(ABCUI_CALL *revision)(void);

  // --- cvar rows -------------------------------------------------------
  int32_t(ABCUI_CALL *rowCount)(void);
  // Copies row `index` (declaration order); 0 when out of range.
  int32_t(ABCUI_CALL *rowAt)(int32_t index, AbcUiSettingRow *out);
  // Copies the row for one cvar; 0 when the table has no such row.
  int32_t(ABCUI_CALL *rowFind)(const char *cvar, AbcUiSettingRow *out);
  // The row's static LIST option; 0 when out of range. `label` is the raw
  // table label ("#Valve_*" or a literal).
  int32_t(ABCUI_CALL *optionAt)(const char *cvar, int32_t index, float *value,
                                char *label, uint32_t labelSize);
  // The row's scanned string choice (a directory / file scan); 0 when out of
  // range. `label` may be empty (the page then derives its own).
  int32_t(ABCUI_CALL *choiceAt)(const char *cvar, int32_t index, char *value,
                                uint32_t valueSize, char *label,
                                uint32_t labelSize);

  // --- drafts and transactions ----------------------------------------
  // Re-reads engine truth (cvar rows plus the bind, display and microphone
  // models) - called once per page opening.
  void(ABCUI_CALL *snapshot)(void);
  // The cvar rows only (the kCommandToggle settle beat; the page re-snapshots
  // while hasQueued() until the state lands).
  void(ABCUI_CALL *snapshotRows)(void);
  int32_t(ABCUI_CALL *setDraftText)(const char *cvar, const char *text);
  int32_t(ABCUI_CALL *setDraftBool)(const char *cvar, int32_t value);
  int32_t(ABCUI_CALL *setDraftEnum)(const char *cvar, float value);
  int32_t(ABCUI_CALL *setDraftString)(const char *cvar, const char *text);
  // Drops a row's pending edit and takes the engine value (conflict).
  int32_t(ABCUI_CALL *adopt)(const char *cvar);
  // Drafts := verified defaults. The confirmation is the page's job; null
  // cvars/count 0 resets the whole table.
  void(ABCUI_CALL *resetDefaults)(const char *const *cvars, int32_t count);
  // Discards every draft (provably writes nothing).
  void(ABCUI_CALL *cancel)(void);
  int32_t(ABCUI_CALL *dirty)(void);
  int32_t(ABCUI_CALL *hasConflicts)(void);
  int32_t(ABCUI_CALL *editable)(const char *cvar);
  // A command-channel row is still settling (re-snapshot while nonzero).
  int32_t(ABCUI_CALL *hasQueued)(void);
  // Writes every dirty row over the injected seams and reads back. Fills the
  // tally; null is allowed.
  void(ABCUI_CALL *apply)(AbcUiSettingsApply *out);
  // The same transaction for exactly one row (the model picker commits its
  // selection immediately instead of waiting for Apply); 1 when a row was
  // written, 0 when the cvar is unknown, clean or not writable right now.
  int32_t(ABCUI_CALL *applyOne)(const char *cvar);

  // --- page tree (settings/*.json) -------------------------------------
  int32_t(ABCUI_CALL *pageCount)(void);
  int32_t(ABCUI_CALL *pageAt)(int32_t index, AbcUiSettingsPage *out);
  // One page cvar (the reset scope and the row filter).
  int32_t(ABCUI_CALL *pageCvarAt)(int32_t page, int32_t index, char *out,
                                  uint32_t outSize);
  int32_t(ABCUI_CALL *sectionAt)(int32_t page, int32_t index,
                                 AbcUiSettingsSection *out);
  int32_t(ABCUI_CALL *sectionComponentAt)(int32_t page, int32_t section,
                                          int32_t index, char *out,
                                          uint32_t outSize);
  // The page declares the component (directly or on one of its sections).
  int32_t(ABCUI_CALL *pageHasComponent)(int32_t page, const char *component);

  // --- key bindings ----------------------------------------------------
  int32_t(ABCUI_CALL *bindReadsAvailable)(void);
  int32_t(ABCUI_CALL *bindRowCount)(void);
  int32_t(ABCUI_CALL *bindRowAt)(int32_t index, AbcUiBindAction *out);
  // Draft key `index` of the action row; 0 out of range.
  int32_t(ABCUI_CALL *bindKeyAt)(int32_t actionIndex, int32_t index, char *out,
                                 uint32_t outSize);
  // The kb_def/valve token table ("Valve_X" -> display text); 0 unknown.
  int32_t(ABCUI_CALL *bindLookupLabel)(const char *token, char *out,
                                       uint32_t outSize);
  // The capturing row, -1 when no session is live.
  int32_t(ABCUI_CALL *bindCapturing)(void);
  // Starts one capture session (the framework takes the input token and
  // feeds the next physical edge to the model). 0 when refused.
  int32_t(ABCUI_CALL *bindBeginCapture)(int32_t actionIndex);
  int32_t(ABCUI_CALL *bindConflictPending)(void);
  int32_t(ABCUI_CALL *bindConflictKeyName)(char *out, uint32_t outSize);
  int32_t(ABCUI_CALL *bindConflictOtherLabel)(char *out, uint32_t outSize);
  void(ABCUI_CALL *bindResolveConflict)(int32_t accept);
  void(ABCUI_CALL *bindClear)(int32_t actionIndex);
  void(ABCUI_CALL *bindRemove)(int32_t actionIndex, const char *keyName);
  void(ABCUI_CALL *bindResetDefaults)(void);
  int32_t(ABCUI_CALL *bindDirty)(void);
  int32_t(ABCUI_CALL *bindHasQueued)(void);
  void(ABCUI_CALL *bindApply)(void);
  // Re-reads engine truth: 1 when any row state changed.
  int32_t(ABCUI_CALL *bindVerify)(void);

  // --- display modes ---------------------------------------------------
  // 0 when the model has no data at all.
  int32_t(ABCUI_CALL *videoFacts)(AbcUiVideoFacts *out);
  int32_t(ABCUI_CALL *videoModeAt)(int32_t index, int32_t *width,
                                   int32_t *height);
  int32_t(ABCUI_CALL *videoStageResolution)(int32_t width, int32_t height);
  int32_t(ABCUI_CALL *videoStageWindowed)(int32_t windowed);
  void(ABCUI_CALL *videoCancelStage)(void);
  // The framework transaction (persist record, changed commands, restart);
  // 1 when the batch was issued.
  int32_t(ABCUI_CALL *videoApply)(void);
  // The confirmation window's two outcomes (inert unless a confirmation is
  // outstanding and the kind allows it): keep clears the record, revert
  // restores the previous mode.
  int32_t(ABCUI_CALL *videoKeep)(void);
  int32_t(ABCUI_CALL *videoRevert)(void);
  // "1920 x 1080 windowed" (the model's own one-line description).
  int32_t(ABCUI_CALL *videoDescribe)(int32_t width, int32_t height,
                                     int32_t windowed, char *out,
                                     uint32_t outSize);

  // --- microphone hardware ---------------------------------------------
  int32_t(ABCUI_CALL *voiceFacts)(AbcUiVoiceFacts *out);
  void(ABCUI_CALL *voiceSetVolumeDraft)(float value);
  void(ABCUI_CALL *voiceSetBoostDraft)(int32_t on);
  void(ABCUI_CALL *voiceStartTest)(void);
  void(ABCUI_CALL *voiceStopTest)(void);
  // Applies the pending drafts; 1 applied, 0 a device write failed,
  // -1 nothing to apply (unavailable or clean).
  int32_t(ABCUI_CALL *voiceApply)(void);

  // --- player-model favorites ------------------------------------------
  int32_t(ABCUI_CALL *favoritesAvailable)(void); // the store accepts writes
  uint32_t(ABCUI_CALL *favoritesGeneration)(void);
  int32_t(ABCUI_CALL *favoriteCount)(void);
  int32_t(ABCUI_CALL *favoriteAt)(int32_t index, char *out, uint32_t outSize);
  int32_t(ABCUI_CALL *favoriteContains)(const char *model);
  void(ABCUI_CALL *favoriteSet)(const char *model, int32_t favorite);

  // --- spray maker ------------------------------------------------------
  // Starts the maker for one absolute image path (the framework decodes,
  // encodes the WAD3 spray, writes it and uploads the preview). 1 when the
  // request was accepted.
  int32_t(ABCUI_CALL *sprayMake)(const char *path);
  // The last outcome line (localization token or text; empty before the
  // first request).
  int32_t(ABCUI_CALL *sprayStatus)(char *out, uint32_t outSize);
  // The Rml source of the preview texture of the spray just made (empty
  // clears the preview).
  int32_t(ABCUI_CALL *sprayPreviewUri)(char *out, uint32_t outSize);
} AbcUiSettingsApi;

// ------------------------------------------------- module descriptor ------

typedef struct AbcUiModuleDescriptor {
  uint32_t structSize;
  const char *id;      // must equal the manifest id
  const char *version; // informational
  // Called right after the entry returns: register panels here. The Rml
  // context does not exist yet - never touch the DOM.
  void(ABCUI_CALL *onLoad)(void);
  // HUD_Init one-shot block: cvar/command registration only.
  void(ABCUI_CALL *onInit)(void);
  // The Rml context exists; re-fired after every shutdown cycle because
  // module documents die with the context and must be rebuilt.
  void(ABCUI_CALL *onContextReady)(void);
  // HUD_Frame, after the panel data pump, before the context update.
  void(ABCUI_CALL *onFrame)(double frameTime);
  // HUD_VidInit, after the context resize.
  void(ABCUI_CALL *onVidInit)(void);
  // While the context is still alive: release documents, listeners and
  // borrowed handles. Not re-fired until the next init/contextReady.
  void(ABCUI_CALL *onShutdown)(void);
  // Final teardown or initialization failure: free everything else. Handle
  // partial initialization. Rml may be gone; do not create new registrations.
  // Stop/join module-created workers before returning.
  void(ABCUI_CALL *onUnload)(void);
} AbcUiModuleDescriptor;

// The host must supply the descriptor capacity. ABI 2 has no zero-capacity
// compatibility path.
static inline int AbcUiModule_InitDescriptor(AbcUiModuleDescriptor *out) {
  if (!out || out->structSize < sizeof(*out))
    return 0;
  AbcUiModuleDescriptor empty = {0};
  *out = empty;
  out->structSize = sizeof(*out);
  return 1;
}

// ---------------------------------------------------------------- table ---

typedef struct AbcUiModuleApi {
  uint32_t structSize; // sizeof(AbcUiModuleApi) of the running framework
  uint32_t abiVersion; // ABCUI_ABI_VERSION of the running framework
  const AbcUiCoreApi *core;
  const AbcUiConsoleApi *console;
  const AbcUiEventsApi *events;
  const AbcUiVfsApi *vfs;
  const AbcUiPanelsApi *panels;
  const AbcUiDataApi *data;
  const AbcUiUiApi *ui;
  const AbcUiCommandsApi *commands;
  const AbcUiWeaponsApi *weapons;
  const AbcUiPlayersApi *players;
  const AbcUiRadarApi *radar;
  const AbcUiShellApi *shell;
  const AbcUiVoiceApi *voice;
  const AbcUiScoreApi *score;
  const AbcUiInputApi *input;
  const AbcUiDocumentsApi *documents;
  const AbcUiMenuApi *menu;
  const AbcUiVoteMenuApi *votemenu;
  const AbcUiPlayerListApi *playerlist;
  const AbcUiCommandMenuApi *commandmenu;
  const AbcUiMenuConfigApi *menuconfig;
  const AbcUiSocialApi *social;
  const AbcUiInventoryApi *inventory;
  const AbcUiCreateGameApi *creategame;
  const AbcUiConsolePageApi *consolepage;
  const AbcUiServerBrowserApi *serverbrowser;
  const AbcUiConnectionApi *connection;
  const AbcUiSettingsApi *settings;
  const AbcUiTexturesApi *textures;
  const AbcUiAudioApi *audio;
} AbcUiModuleApi;

#ifdef __cplusplus
} // extern "C"
#endif

// Entry type also makes host casts independent of compiler defaults.
typedef int(ABCUI_CALL *AbcUiModuleEntry)(uint32_t, const AbcUiModuleApi *,
                                          AbcUiModuleDescriptor *);

#pragma pack(pop)
