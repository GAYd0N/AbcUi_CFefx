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
  Page assets live under `assets/abc/abcui/`: `gameui/{rml,rcss,config,img,
  scence}`, `hud/{rml,rcss,img,spr}`, plus the shared `lang`, `rcss` and `img`.
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
- Commit messages use `type(scope): summary` (feat / fix / refactor / docs / build / chore) with motivation and verification in the body.
- Before handing off: no local paths or credentials; README matches actual behaviour; Debug and Release both build.
