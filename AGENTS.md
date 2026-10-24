# AGENTS.md

Guidance for AI/automation collaborators working in this repository. Read `README.md` first; it is the only document distributed with the release.

## Hard rules

- Nothing committed (code, comments, docs, commit messages, tags) may contain local machine paths, personal directories, session logs or any credentials; the deployment destination is provided through the `ABC_GAME_DIR` environment variable (optional; unset means no deploy).
- Do not commit test scaffolding, acceptance scripts, evidence screenshots, plans or progress logs; those are session artifacts that do not belong in the release.
- The README is the single public document; UI, config-format or cvar behaviour changes must update it in the same commit.
- AbcCore is managed by the aggregate `ABCEnchance` repository and consumed
  through `ABC_CORE_DIR`; do not add a `thirdparty/AbcCore` submodule back. The
  plugin-facing contract is the versioned C ABI `AbcCore_CreateInterface`
  table, not exported C++ symbols.
- Submodule pins may only point at commits that are already public upstream; confirm the commit is pushed before bumping a pin.
- Rendering draws go exclusively through IMetaRenderer: no direct OpenGL calls.
  VGUI2 `ISurface2` is used only for the texture upload channel
  (`DrawSetTextureRGBA`), cursor handling and engine UI seams, never to draw a
  panel.
- AbcCore must stay renderer-free. AbcUi owns `thirdparty/Renderer` and the renderer-facing bridge in `src/render/`; do not move either back into AbcCore.

## Known structural exceptions

These are deliberate, audited exceptions rather than unfinished cleanups:

- The connection/loading state machine (attempt kind, password prompts, error
  and direct-connect overlays) stays in `GameUiModule`. Its page, controller
  and loading-feed ordering is one interleaved state machine; each attempted
  split needed more host callbacks than the extracted flow removed, so the
  remaining boundary is documented instead of duplicated.
- Parse-boundary exceptions: `ValidationError` is private to the JSON
  validators (`CommandMenuModel`, `UiDefinitions`) and is caught by the same
  translation unit's public parse API. rapidjson parse *errors* are checked via
  `HasParseError`; only `ValidationError` is caught, so allocation failures are
  not claimed as handled. New code must use `std::optional`/error
  out-parameters, not add throw sites.
- RTTI is limited to RmlUi widget downcasts (`ConsolePage`, `TextInput`), the
  Debug probe/video test tools, and RmlUi's own `rmlui_dynamic_cast` for
  types implementing its RTTI macros. New production code should prefer
  `rmlui_dynamic_cast` where the RmlUi RTTI macros exist.
- `src/render/RenderCommon.h` is vendor-synced and excluded from the
  clang-format check; `src/rmlui/TextInput.h` deliberately `#undef Assert`
  after `windows.h` to keep RmlUi's `Rml::Assert` declaration visible.
- `tests/` is session scaffolding per the hard rule above; the release has no
  committed test target. Unit-test additions require a separate policy change.
- Process-lifetime services (`consoleBuffer`, `menuBackend`, `steamFavorites`,
  `friendsModel`, `nativeUiBridge`, `loadingFeed`) are function-local statics:
  the engine calls back through bare C function pointers and the DLL lifetime
  is the service lifetime. Consume them through their named accessor; do not
  add new ad-hoc file-level statics. Module and panel lookup goes through
  `modules::registry()` / `PanelManager::find`, not reverse pointers.
- HUD_Init/HUD_Shutdown repeat within one engine process (the client re-runs
  its HUD cycle on map init and the teardown path runs on the way out).
  Everything HUD_Shutdown tears down must be re-armed by H_HUD_Init: the
  panel list (the panel install when `PanelManager` is empty) and module
  documents (`onContextReady`). Cvars, commands and module registration stay
  process-scoped and are guarded by a one-shot latch; do not move them into
  the per-cycle path. Usermsg hooks are installed once and re-chained only by
  `netmsg::refreshAfterClientLoad()` after a client DLL reload (detected via a
  Ui-exclusive message); re-hooking every cycle would make AbcEssential's and
  this plugin's hooks point at each other and recurse forever.
- GameUiModule's engine-event subscriptions (`votemenu`, command menu,
  inventory commands, InvAdd/InvRemove, MapList) are installed once in
  `onInit` and guarded by `*Bound` flags. The module object and the event
  buses are process-lifetime, so they are deliberately not unsubscribed in
  `onShutdown`; never re-append an unguarded `[this]` subscription from a
  per-cycle callback.
- Server weapon names are untrusted. They may enter the weapon registry only
  as `[A-Za-z0-9_]` identifiers, and only a `weapon_*` token may be sent to
  `pfnServerCmd` (`ResourceLimits::weaponCommandName`). A bare identifier is
  not enough: `kill` is a legal identifier and a real console command.
- Custom Rml elements that are downcast after lookup declare
  `RMLUI_RTTI_DeclareWithParent`/`RMLUI_RTTI_Define` and are reached through
  `rmlui_dynamic_cast`; raw `static_cast` between element types is not
  allowed in production or debug tools.
- `<colorpicker>` queues `change` events and dispatches them from
  `ElementColorPicker::pumpChanges()` in the frame pump, never from the
  drag/render call stack (a page handler may destroy the picker).

## UI module architecture

The plugin is a UI-free framework plus loadable UI modules (zip packages). The
implemented rules the transition must keep:

- AbcUi.dll is UI-free: RmlUi runtime, render bridge, input bridge, IME,
  theme/localization, texture services (sprites/avatars/custom elements), the
  data layer (NetworkMessages / WeaponResource / HudState / PlayerResource /
  PlayerTrace / TextFeed) and the module machinery. No HUD panel, menu page,
  stub page, quit overlay, panel catalog or page stylesheet ships inside the
  framework.
- UI features live in modules under `modules/<id>/`: `manifest.json` + `src/`
  + `assets/` + a CMake `add_library(... MODULE)` target. Migrated so far:
  flashlight, textmenu, vote, motd, weaponchoose, killfeed, textfeed, sidetext,
  health, ammo, hudeffects, crosshair, damage, itemhighlight, playerboard,
  radar, scoreboard, spectator and chat; `abcui.gameui` has every replacement menu
  page (the shell, the console, the server browser, the connection
  surfaces, the vote creation, player-list, command-menu, inventory,
  create-game and settings pages, plus the display-mode confirmation and the
  quit confirmation); `demo` is the minimal example. modeltest/videotest stay
  development-only.
- The module ABI is pure C, versioned struct tables under `sdk/abcui/`
  (same pattern as `AbcCore_CreateInterface`). A module DLL exports exactly
  one entry, `AbcUiModule_Main(abiVersion, api, outDescriptor)`; everything
  else crosses the boundary as C function pointers and POD payloads. Modules
  must not include RmlUi headers or link the framework's static libraries.
  Sections: core, console, events, vfs, panels, documents, data, ui, commands,
  weapons, players, radar, shell, voice, score, input, menu, votemenu,
  playerlist, commandmenu, menuconfig, social, inventory, creategame,
  consolepage, serverbrowser, connection, settings. Adding a function
  means appending it to the section end (and its initializer in
  `ApiTable.cpp` / `ApiSections.cpp` / `ApiWeapons.cpp` / `ApiPlayers.cpp` /
  `ApiDocuments.cpp` / `ApiMenu.cpp` / `ApiVoteMenu.cpp` /
  `ApiPlayerList.cpp` / `ApiCommandMenu.cpp` / `ApiMenuConfig.cpp` /
  `ApiSocial.cpp` / `ApiInventory.cpp` / `ApiCreateGame.cpp` /
  `ApiConsolePage.cpp` / `ApiServerBrowser.cpp` / `ApiConnection.cpp` /
  `ApiSettings.cpp` -
  a missing initializer is silently a null pointer; the table audit in
  `ApiTable::get` catches it at boot), and
  appending to snapshot structs means the writer must honor the caller's
  `structSize`.
- Module packages are zip files at `svencoop/abc/abcui/modules/` (real files,
  never inside a VPK): `manifest.json`, the entry DLL and the module's
  rml / rcss / img / lang trees. Manifest fields: `id`, `name`, `description`,
  `author`, `version`, `abi`, `minAbcUi`, `entry`, `dependencies`,
  `conflicts`. Packaging stores every entry uncompressed (STORED): the loader
  maps the zip with `CreateFileMapping` and serves STORED entries as zero-copy
  views; DEFLATED entries are inflated once through miniz for third-party
  compatibility. `tools/pack.py` (the `pack_modules` target) validates the
  manifests and writes `<id>-<version>.zip`; `dist` includes the archives.
- Loading happens once per process in the HUD_Init one-shot block: scan,
  validate (abi / minAbcUi / dependencies / conflicts), topologically sort
  (ties broken by id), map each package, load the entry DLL from memory
  (manual PE map; imports resolve through `GetModuleHandle` /
  `GetProcAddress`), call the entry, then drive
  `onLoad / onInit / onContextReady / onFrame / onVidInit / onShutdown /
  onUnload` in load order (reverse order for shutdown). A failed module is
  skipped together with its dependents; the process keeps running. Modules
  must avoid static TLS (`__declspec(thread)`) and delayimp.
- Module panel descriptors are kept for the whole process and every
  HUD_Init cycle builds fresh wrappers: `PanelManager::shutdown` destroys the
  panel list on every HUD_Shutdown, so taking the wrappers once would lose
  every module panel after the first map change.
- Interactive pages are module-owned documents (`documents->create`), never
  framework pages: the framework loads them on every context cycle, forwards
  ready/show/hide/close, and keeps the menu layer's focus guard and hide-all
  rule for descriptors carrying `ABCUI_DOC_FLAG_MENU`. Page modules present
  themselves from `menu.state` (transitions) and `menu.paint` (the per-frame
  presentation beat), and navigate through the `menu` section
  (`openPage`/`back`/`ensureUp`/`resume`) - never through a native GameUI
  window. The page stack, phase machine and takeover stay framework-side;
  every page is a module document.
- Data exchange is event-bus based (`src/modsys/EventBus.h`): string channels,
  each backed by priority buckets of `abc::CallbackList` (eventpp reused from
  AbcCore). Priority > 0 runs before the framework default (interception and
  rewriting), < 0 runs after (fallback); the same priority keeps registration
  order, and registration order follows the deterministic load order, so
  behaviour never depends on discovery order. Veto channels aggregate their
  bool result in priority order. The chained-command bridge inverts that
  result: `CChainedCommand` runs the engine's original only when every
  subscriber returned true, while the bus reports "consumed" (any sink
  returned nonzero), so `Commands.cpp` returns `!publishVetoAggregateRaw(...)`.
  Channels and payloads are documented in `sdk/abcui/events.h`.
- Transition rules: each migrated feature moves its sources, assets and cvars
  into `modules/<id>/` in one commit and registers through the new ABI, and
  the built-in copy is deleted in that same commit (the framework must not
  keep a duplicate implementation). Existing lifecycle red lines (one-shot
  latches, usermsg install-once + re-chain, export chaining) keep applying to
  the framework spine unchanged.

## Build

Win32 x86 only / C++20 / dynamic CRT (/MD). AbcCore is consumed as
`AbcCore.dll` from `ABC_CORE_DIR` (sibling `../AbcCore` by default); the plugin
links the import library and the DLL must be deployed under
`metahook/dlls/AbcCore/`. `ABC_GAME_DIR` is optional: unset means the plugin
and assets are built but not deployed.

```bash
cmake --preset win32-debug
cmake --build --preset win32-debug    # Release: win32-release
```

## Conventions

- Public headers may expose their contract types (AIGCJson for `Config`,
  eventpp for `Events`, SteamSDK for `SteamId`, HLSDK for `WeaponResource`,
  IMetaRenderer for `Render`, RmlUi for `AbcRenderInterface` and Rml-facing
  headers). New headers forward-declare or PIMPL anything outside the contract
  and include the standard headers they use directly.
- Formatting follows LLVM (2 spaces, 80 columns, attached braces). Identifier
  casing is per-subsystem and sometimes per-header: types use UpperCamelCase
  and constants use the `k` prefix; AbcCore's public API is UpperCamelCase,
  while AbcUi implementation and framework helpers are mixed (for example
  `PanelManager::installPanels` versus `Localization::load`). Follow the
  surrounding declarations. The legacy UpperCamelCase framework namespaces
  (`Assets`, `ColorUtil`, `ImageLoader`, `ResourceFile`, `TextUtil`, `Theme`)
  and ABCEnchance-derived identifiers are kept; do not mass-rename.
- C-style casts remain in the ABCEnchance-derived code; new code uses
  `static_cast` / `const_cast` / `reinterpret_cast`.
- Interface structure is orchestrated through the GameUI composition root.
  Page documents and their stylesheets live in the module packages; the
  configuration JSON and the shared theme stay under `assets/abc/abcui/`:
  `gameui/{rcss,config,img,scence}`, `hud/{rml,rcss,img,spr}`, plus the
  shared `lang`, `rcss` and `img`.
- RmlUi 6.3 RCSS constraints: `font-family` accepts a single family name (CJK
  falls back through the fallback face), and transition easing names use the
  `linear-in` family.
- Main-menu and settings structure is driven by
  `assets/abc/abcui/gameui/config/mainmenu.json` and
  `assets/abc/abcui/gameui/config/settings/*.json`; new menu/settings pages are
  added through JSON rather than hard-coded.
- `using namespace` is confined to `.cpp` implementation files: the subsystem
  directives are `engine`/`navigation`/`settings`/`detail`, plus a function-local
  `std::chrono` in `ServerQuery.cpp`. Never add one to a header or use it to
  reach an unrelated subsystem.
- CI pins the CMake download (4.4.4 + published SHA-256) and action commit
  SHAs, and uses the permission-scoped default token instead of a personal
  PAT. Keep those properties when editing the workflow.
- An empty `ABCUI_MILE_FFMPEG_ROOT` downloads the pinned, hash-verified SDK;
  accepting a local dev-tree copy requires
  `-DABCUI_ALLOW_LOCAL_MILE_FFMPEG=ON`. Do not restore the implicit local
  search.
- RML class tokens must have a rule in an RCSS file; an unstyled class is
  either given a rule or removed (ids used by C++ lookups stay).
- Commit messages use `type(scope): summary` (feat / fix / refactor / docs / build / chore) with motivation and verification in the body.
- Before handing off: no local paths or credentials; README matches actual behaviour; Debug and Release both build.
