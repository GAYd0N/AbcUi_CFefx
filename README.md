# AbcUi

HUD and menu plugin for Sven Co-op / MetaHookSv (`AbcUi.dll`): rebuilds the entire ABCEnchance interface on RmlUi, drawing exclusively through IMetaRenderer with zero direct OpenGL (textures upload through the VGUI2 surface channel). The shared [AbcCore](https://github.com/DrAbcOfficial/AbcCore) library is loaded as `AbcCore.dll` from an external source tree (`ABC_CORE_DIR`, sibling `../AbcCore` by default); AbcUi no longer vendors AbcCore as a submodule. The DLL exposes a Valve-style, versioned C ABI factory (`AbcCore_CreateInterface`). AbcUi owns the direct Renderer interface dependency and its render bridge, so AbcCore stays renderer-free.

## Build

Windows + MSVC + CMake >= 4.2, Win32 x86 only, C++20, dynamic CRT (/MD):

```bash
git clone --recursive https://github.com/DrAbcOfficial/AbcUi.git
git clone --recursive https://github.com/DrAbcOfficial/AbcCore.git ../AbcCore
cmake --preset win32-release
cmake --build --preset win32-release
```

The default `ABC_CORE_DIR` is the sibling `../AbcCore` checkout. If AbcCore is
elsewhere, pass `-DABC_CORE_DIR=<path>` when configuring. If AbcUi's own
submodules were not fetched by `--recursive`, run
`git submodule update --init --recursive` once at the repository root.

The pinned Renderer commit is currently published in the `DrAbcOfficial/Renderer` fork; when `MetaHookSv/Renderer` does not yet contain it, use the URL rewrite configured in `.github/workflows/build.yml` before fetching submodules.

## Deploy

Set the `ABC_GAME_DIR` environment variable to the game root before configuring (or pass `-DABC_GAME_DIR=...`); nothing is deployed when it is unset:

```bash
cmake --build --preset win32-release --target deploy
```

- UI assets go to `<game>/svencoop/abc/abcui/`, split by surface: GameUI documents, page styles, config and menu media live under `gameui/`; HUD documents, sprites and page art live under `hud/`; the cross-surface stylesheets (`rcss/base.rcss`, `rcss/theme.rcss`), language tables and shared art stay at the `abcui/` root.
- The Cfefx score sound is engine-played and therefore lives under the engine's sound root: `<game>/svencoop/sound/abc/cfefx/UI_SPECIALKILL2.wav`.
- `AbcUi.dll` goes to `<game>/svencoop/metahook/plugins/`.
- `FreeImage.dll` and `Mile.FFmpeg.dll` are provided by MetaHookSV/Renderer; AbcUi does not install or package a second copy.

## Packaging

```bash
cmake --build --preset win32-release --target dist
```

Produces `AbcUi-<version>.zip` under `build/<preset>/dist/` (plugin DLL,
`abc/abcui` assets, README). `AbcCore.dll` and the shared gamedata catalog are
provided by the aggregate `ABCEnchance-<tag>.zip` / `AbcCore-<version>.zip`,
not by this module package. The archive contains `svencoop/` at its root: extract it directly into
the Sven Co-op root, with no extra version folder to copy.

## Data-driven menu

The main menu and settings categories are driven by JSON in `assets/abc/abcui/gameui/config/` (installed to `<mod>/abc/abcui/gameui/config/`): adding a file under `settings/` extends the pages and ordinary settings, loaded automatically at startup; labels may be literal text or `#token` lookups in `abcui/lang/*.txt`. Limits: 1 MiB per page, 64 JSON files, 512 items per page.

The in-game `+commandmenu` binding is also data-driven by
`gameui/config/commandmenu.json`; the legacy `commandmenu.txt` is not read.
Each entry has a `label` and either a `command` leaf or nested `items`; array
order supplies the one-based number shown in the left gutter. Numeric keys
activate entries directly, mouse/arrow/Enter navigation is supported, and an
optional `maps` array limits an entry to named BSPs (case-insensitive, without
the path or `.bsp`). Commands use the engine's normal client-command path, so
existing client cvars and server verbs keep working. `+commandmenu` opens or
closes the translucent RmlUi page and `-commandmenu` is consumed; command
entries and Esc dismiss the page. Literal labels are looked up in the active
language table, so the shipped English labels have complete Simplified
Chinese and Spanish translations; explicit `#token` labels also work.
The page uses compact fixed-width columns, keeps only the needed one to three
columns on screen and caps column height so it does not cover the play area.
Limits: 1 MiB per file, 100 items per menu and 40 nested levels.

```json
{
  "version": 1,
  "title": "#CommandMenu_Title",
  "items": [
    { "label": "Mission Briefing", "command": "missionbriefing" },
    { "label": "Player Model", "items": [
      { "label": "Gordon", "command": "model gordon" }
    ] }
  ]
}
```

## Feature overview

- HUD panels: Health / Flashlight / AmmoBar / AmmoStack / ItemStack / WeaponStack / Notice / DeadMsg / SideText / TextMenu / DamageTiles / PopNum / Cfefx / Indicator / player board / scoreboard / vote / MOTD / weapon choose / item highlight / Radar and more; the HUD palette is the Half-Life amber (`#ffb000`, the old scheme's "Orange") defined once as `--abc-hud-*` tokens in `rcss/base.rcss`, matching the amber menu accent; the health/armor vitals follow the Half-Life 2 readout - an amber icon plus one large value drawn straight on the scene - while the bar meters under each value are kept as the at-a-glance indicator; the value ink and the meters are additive (`texadd://` glyph and meter textures composed by `framework::glyphs`, styled by the reusable `.hud-glyph` / `.hud-meter` classes in `base.rcss`), so they glow into the scene exactly like the HUD sprite icons and need no outline or black meter well; the ammo readout is the matching HL2 pool line - a large clip value, a stepped-down `/reserve`, the ammo sprite standing on the same baseline, and the pools packed into the bottom-right corner; data layer with NetworkMessages / WeaponResource / HudState / TextFeed, and the resource layer under `src/rmlui/texture/` with SpriteResource (GoldSrc .spr decode + hud.txt); the original HUD list is resolved through gamedata and its visible flag is stripped in the chained HUD_Redraw thunk.
- Resource files (sprites, images and RML documents) resolve through the engine `IFileSystem`, the same mounted view VGUI2Extension uses. Custom server content in `<mod>_downloads`, `<mod>_addon` and other engine search paths therefore participates without CWD-relative path guessing.
- Menu: main menu, settings (general/input/mouse/video/audio/voice/captions/multiplayer/advanced/menu/renderer), server browser, create game, player list, vote creation (ban/kick/kill/map with the server's map list), connect/loading/error pages, console, direct-connect/password dialogs, quit confirmation, and the nested in-game command menu; the loading page stays above the page shells and the console while a load is in progress, while the connection dialogs and the quit confirmation remain above it; text input goes through the IME-aware input bridge. The pause menu's **Vote Menu** row and the original `votemenu` command both open the RmlUi page, replacing the legacy GameUI vote dialog. The original `+commandmenu` VGUI window is replaced by a translucent, left-anchored RmlUi menu that leaves the game view visible. The console wraps output to the current window width, supports mouse selection with Ctrl+C copy or a right-click Copy menu (no selection copies the visible output), and the console hotkey returns straight to gameplay from the in-game pause page (the no-map main menu stays open).
- Menu backdrop: `abc_ui_menu_video` picks the looping movie behind the no-map menu (blurred by `abc_ui_menu_backdrop_blur` gaussian rounds, 0..4, default 2; 0 shows the unblurred capture); in a level the pause menu blurs the live frame instead.
- Retained cvars: `hud_danger_health`, `hud_danger_armor`, `hud_deathnotice(+_time/_count)`, `hud_sideinfo`, `hud_cfefx(+_max/+_volume/+_time)`; in-game diagnostics: `abc_ui_debugger` (RmlUi debugger) and `abc_ui_probe` (font/panel/element state dump).
- Avatars and real names come from the dynamically bound game steam_api.dll (Bridge/Native backend abstraction, no steam_api.lib import dependency); fonts are loaded from the system font directory through `Rml::LoadFontFace`.

## Runtime requirements

`plugins.lst` order (AbcUi must load after Renderer; it performs a fatal check on Renderer at startup):

```text
VGUI2Extension.dll
Renderer.dll
AbcEssential.dll
AbcUi.dll
```

`AbcCore.dll` is a shared dependency, not a plugin: place it at
`svencoop/metahook/dlls/AbcCore/AbcCore.dll` (the aggregate package does this).
The Windows loader resolves it when `AbcUi.dll` loads; the plugin-facing
contract is the versioned `AbcCore_CreateInterface` table, not exported C++
symbols.

## Submodules

| Path | Upstream | Pin |
|---|---|---|
| `thirdparty/Renderer` | https://github.com/MetaHookSv/Renderer | `ee7c1bd601f3cecd03fa0496db2cf0c4a919a452` (public interfaces and `gl_common.h` sync) |
| `thirdparty/RmlUi` | https://github.com/mikke89/RmlUi | tag `6.3` (`ba95ffe8bfb6370efb2cdcca927eaad4710c5413`) |
| `thirdparty/freetype` | https://github.com/freetype/freetype | tag `VER-2-13-3` (`42608f77f20749dd6ddc9e0536788eaad70ea4b5`) |
| `thirdparty/FreeImage` | https://github.com/hzqst/FreeImage_clone | `007c9e4c5d4198a1646b6c5274fd855be9cca7ef` |
| `thirdparty/VGUI2Extension` | https://github.com/MetaHookSv/VGUI2Extension | `01f9ad225aef5815eb1c02539667a9872cc2a511` (headers only) |
| `thirdparty/SteamSDK` | https://github.com/MetaHookSv/SteamSDK | `3c1abaf6277f9f99fd16ef40557d6852820b848f` |

## Shared dependency

AbcCore is managed by the aggregate
[ABCEnchance](https://github.com/DrAbcOfficial/ABCEnchance) repository. Set
`ABC_CORE_DIR` to its source tree when building AbcUi standalone; the default is
`../AbcCore`. When building from the aggregate repository, point
`ABC_CORE_DIR` at its root `AbcCore/` checkout.

## License

Licensed under the [BSD 3-Clause License](LICENSE); each dependency keeps its own license.
