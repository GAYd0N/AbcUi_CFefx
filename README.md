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
- UI module packages go to `<game>/svencoop/abc/abcui/modules/*.zip` (built by the `pack_modules` target; `deploy` installs them too when `ABC_GAME_DIR` is set).
- `FreeImage.dll` and `Mile.FFmpeg.dll` are provided by MetaHookSV/Renderer; AbcUi does not install or package a second copy.

## Packaging

```bash
cmake --build --preset win32-release --target dist
```

Produces `AbcUi-<version>.zip` under `build/<preset>/dist/` (plugin DLL,
`abc/abcui` assets, the packed UI modules under `abc/abcui/modules/`, README).
The module archives themselves are produced by `--target pack_modules`
(`build/<preset>/dist/modules/<id>-<version>.zip`) from the manifests and the
built module DLLs. `AbcCore.dll` and the shared gamedata catalog are
provided by the aggregate `ABCEnchance-<tag>.zip` / `AbcCore-<version>.zip`,
not by this module package. The archive contains `svencoop/` at its root: extract it directly into
the Sven Co-op root, with no extra version folder to copy.

## Data-driven menu

The main menu and settings categories are driven by JSON in `assets/abc/abcui/gameui/config/` (installed to `<mod>/abc/abcui/gameui/config/`): adding a file under `settings/` extends the pages and ordinary settings, loaded automatically at startup; labels may be literal text or `#token` lookups in `abcui/lang/*.txt`. Limits: 1 MiB per page, 64 JSON files, 512 items per page.

Settings sections may declare widget components alongside their rows; a card with no rows and no widget is not drawn at all. The multiplayer page's player section uses `model-preview` (the live model render) and `model-picker`: a list of every directory under `models/player` with a case-insensitive text filter, a favorites-only toggle and a per-row favorite star; double-clicking a row or picking it in the row's right-click menu also toggles the favorite. Clicking a row writes the `model` cvar immediately and the preview follows; the current engine value stays selected even across restarts. Favorites persist in `<mod>/abc/abcui/model_favorites.json` (`{"favorites": [...]}`, atomic writes); the picker never reads legacy files, and a damaged store loads read-only instead of being overwritten.

The ABC page's crosshair section uses `crosshair-preview` for the live shape and `crosshair-color` for two RGBA pickers: each picker edits one colour's red, green, blue and alpha channels (hue strip, saturation/value square and alpha strip) and commits the four drafts, replacing the eight per-channel sliders while the eight cvar rows stay declared as the apply/reset backbone. The section declares `"layout": "two-up"`: the preview and the pickers share a widget band above the rows, and the rows themselves are paired two per line, so the card stays compact and the preview is a fixed 220dp window instead of a full-height column. The options page is grouped into section cards; the advanced page's items are the game's own `user.scr` entries (the shipped `MultiplayerAdvancedDialog.res` only hosts the list), so a live script supplies the domains and defaults while the JSON keeps the card grouping and labels.

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

## UI modules

AbcUi is a UI-free framework plus loadable UI-module packages. Every HUD panel
that has been migrated lives in `modules/<id>/` in this repository and ships as
one zip package installed at `svencoop/abc/abcui/modules/<id>-<version>.zip`
(real files, never inside a VPK). Packages are mapped with
`CreateFileMapping`; entries are stored uncompressed so the manifest, RML,
RCSS, art and the entry DLL are served as zero-copy views (DEFLATED entries
from third-party packages are inflated through miniz).

A package contains:

```text
manifest.json            id, name, description, author, version, abi,
                         minAbcUi, entry, dependencies, conflicts
entry/<abcui_id>.dll     the module DLL (Win32 x86, /MD)
hud/...                  the module's rml/rcss/img/lang tree
```

The framework scans the module directory once per process during `HUD_Init`,
validates every manifest (ABI, `minAbcUi`, dependencies, conflicts), loads the
packages in deterministic dependency order (ties broken by id), maps each entry
DLL by hand (no `LoadLibrary`) and drives it through the same lifecycle points
the built-in modules used: `onLoad`, `onInit`, `onContextReady`, `onFrame`,
`onVidInit`, `onShutdown`, and `onUnload` in reverse order at process
teardown. A module that fails validation is skipped together with its
dependents; the rest keep working.

Modules talk to the framework through the pure-C `AbcUiModuleApi` table
(`sdk/abcui/`): versioned, append-only sections for console/cvars, the
priority event bus, package files, HUD panels, interactive page documents,
the replacement menu's presentation state, the HUD data snapshot, Rml
documents and elements, chained commands, the weapon registry, the player
resource and the settings page's row/bind/display/microphone models. Modules include `<abcui/module_api.h>`, export exactly one symbol
(`AbcUiModule_Main`), must not include RmlUi headers or link the framework's
static libraries, and must avoid static TLS and delay-loaded imports.

First-party modules in this repository: flashlight, textmenu, vote, motd,
weaponchoose, killfeed, textfeed, sidetext, health, ammo, hudeffects,
crosshair, damage, cfefx, itemhighlight, playerboard, radar, scoreboard, chat and
`abcui.gameui` (every replacement game-menu surface: the shell, the console,
the server browser, the connection surfaces and the vote creation, player
list, command menu, inventory, create-game and settings pages, the
display-mode confirmation and the quit confirmation); `demo` is the minimal
example. `tools/pack.py` validates each manifest and writes the archives.
`AbcUi.dll` is UI-free: it ships no page, stub, HUD panel or panel catalog,
and every menu document, its stylesheet and the settings page tree live in
the module packages.

## Feature overview

- HUD panels: Health / Flashlight / AmmoBar / AmmoStack / ItemStack / WeaponStack / Notice / DeadMsg / SideText / TextMenu / DamageTiles / PopNum / Cfefx / Indicator / player board / scoreboard / vote / MOTD / weapon choose / item highlight / Radar / chat and more; the HUD palette is the Half-Life amber (`#ffb000`, the old scheme's "Orange") defined once as `--abc-hud-*` tokens in `rcss/base.rcss`, matching the amber menu accent; the health/armor vitals follow the Half-Life 2 readout - an amber icon plus one large value drawn straight on the scene - while the bar meters under each value are kept as the at-a-glance indicator; the value ink and the meters are additive (`texadd://` glyph and meter textures composed by `framework::glyphs`, styled by the reusable `.hud-glyph` / `.hud-meter` classes in `base.rcss`), so they glow into the scene exactly like the HUD sprite icons and need no outline or black meter well; the ammo readout is the matching HL2 pool line - a large clip value, a stepped-down `/reserve`, the ammo sprite standing on the same baseline, and the pools packed into the bottom-right corner; data layer with NetworkMessages / WeaponResource / HudState / TextFeed, and the resource layer under `src/rmlui/texture/` with SpriteResource (GoldSrc .spr decode + hud.txt), animated images through the generic `<animimg>` element (`src/rmlui/animated/`: a format-backend table decodes the container into one dynamic texture per element - animated WebP today, APNG for Steam animated avatars later), which the water-exit splash uses (two additive animated WebP files at 10 fps, a fraction of the old .spr size); the original HUD list is resolved through gamedata and its visible flag is stripped in the chained HUD_Redraw thunk.
- Chat: the message-mode input and the player chat log are an RmlUi HUD module (`modules/chat/`), with the native chat input and `CHudSayText` both retired. The chained `messagemode`/`messagemode2` commands open the input line instead of the native one (the command bridge carries no argv, so an argumented `messagemode` also opens the line; send text through `say`/`say_team` directly when a script needs it), Enter submits through `say "<text>"` / `say_team "<text>"` with the payload stripped of quotes and control characters before assembly, Esc closes, and Up/Down walk a 32-entry send history; text, IME and the caret ride the shared input bridge, so a CJK composition commits into the same editor, and the composition/candidate panel is drawn by `src/rmlui/ImeOverlay.cpp` at the caret: RmlUi underlines the composing string inline while the overlay supplies the composition header and the candidate page the engine gives no window for, read from the IME's own list (`ImmGetCandidateListW`) as one horizontal row of at most nine entries, each prefixed with the number key that picks it and the selected row in amber; the open line also carries the active input method's short tag (`CN`/`TW`/`HK`/`JP`/`KR`) at its right edge and hides it when the layout has no IME. `SayText` is parsed in the data layer and swallowed so exactly one log is drawn: rows keep the server's colour control bytes (normal text, per-player name tint from the client's `GetClientColor`, location), split the client's `(TEAM)` / `*DEAD*` / `*SPEC*` decorations into their own run, honour positional `%s`/`%sN` substitution on HLSDK-shaped servers, and escape every server string before it reaches the DOM. The log sits bottom-left in the HUD palette (translucent row plates, amber channel ink), wraps long lines inside a 190dp column, grows upward from the input row, and shows a compact window of at most six rows (capped from `hud_saytext_lines`) over a 256-message scrollback: the thin scrollbar and the mouse wheel browse older lines while the input line is open (the wheel never switches weapons then), the tail follows new lines unless the reader scrolled up, and the whole block fades out one second after `hud_saytext_time` elapses; every line is also mirrored into the console the way the native handler did, and emoji render in colour through the Segoe UI Emoji fallback face (RmlUi asks FreeType for FT_LOAD_COLOR and uploads the RGBA glyph atlas); `hud_saytext 0` disables it and `hud_saytext_block_muted 1` drops lines from voice-muted players. Ownership is the input router's: the open input line holds an interactive-HUD token, and the engine is told the UI consumed the key at the CBaseUI key seam (`IVGUI2Extension_BaseUICallbacks::Key_Event`), which is the same route the native chat entry and CaptionMod's VGUI dialog use - the binding simply never runs, so WASD stops and no usercmd filtering is involved. Edges the router passes through for clean pairing (keys held when the line opened) are left to the engine. The token is released on Esc, focus loss, the pause menu or a map change.
- Resource files (sprites, images and RML documents) resolve through the engine `IFileSystem`, the same mounted view VGUI2Extension uses. Custom server content in `<mod>_downloads`, `<mod>_addon` and other engine search paths therefore participates without CWD-relative path guessing.
- Menu: main menu, settings (general/input/mouse/video/audio/voice/captions/multiplayer/advanced/menu/renderer), server browser, create game, player list, vote creation (ban/kick/kill/map with the server's map list), connect/loading/error pages, console, direct-connect/password dialogs, quit confirmation, and the nested in-game command menu; the loading page stays above the page shells and the console while a load is in progress, while the connection dialogs and the quit confirmation remain above it; a server `changelevel` also raises the loading page (map-load title/status, no connect address) instead of dropping to the no-map main menu and its backdrop movie. Text input goes through the IME-aware input bridge. The pause menu's **Vote Menu** row and the original `votemenu` command both open the RmlUi page, replacing the legacy GameUI vote dialog. The original `+commandmenu` VGUI window is replaced by a translucent, left-anchored RmlUi menu that leaves the game view visible. The original `inventory` / `+inventory` / `-inventory` viewport window is replaced by an RmlUi inventory window: the chained commands swallow the native `CInventoryPanel`, and the page projects the server's `InvAdd`/`InvRemove` item stream with the native captions and fallbacks (`You haven't got any inventory`, `No item selected`, `Nameless item`, `No item description`). The dialog is a centred panel over a dimmed scrim (clicking outside also closes it): the item list with its count on the left, a detail card on the right showing the item name, description and the server's can-use/can-drop flags as tags, and the action row at the bottom. A row click selects, Up/Down walk the list, Enter activates, Delete drops, and the three action buttons send the native `activateitem "#<id>"` / `dropitem "#<id>"` / `dropitem "*"` verbs; any action, Esc, the close button, the scrim, `-inventory` or the bound key closes the window. Actions are read-through: a row disappears only when the server's `InvRemove` arrives, so a refused action never rewrites the list optimistically. The console wraps output to the current window width, supports mouse selection with Ctrl+C copy or a right-click Copy menu (no selection copies the visible output), and the console hotkey returns straight to gameplay from the in-game pause page (the no-map main menu stays open). Tab completes the editor against the client's cvar/command catalog: a unique match completes in place, several matches extend to their common prefix and open a candidate list that further Tab presses cycle (Enter runs the highlighted candidate, Esc closes only that list); Up/Down keep walking the submission history.
- Menu backdrop: `abc_ui_menu_video` picks the looping movie behind the no-map menu (blurred by `abc_ui_menu_backdrop_blur` gaussian rounds, 0..4, default 2; 0 shows the unblurred capture); in a level the pause menu blurs the live frame instead.
- Voice mute: the player list's Mute button, the scoreboard's right-click **Mute Voice** entry and the per-row muted badge share one persisted mute list at `<mod>/abc/abcui/voice_ban.dt` (JSON `{"bans": ["<steamid64>"...]}`, atomic writes; the legacy files are never read). The list is mirrored to the server as a 32-slot `vban` mask with `VModEnable`, re-synced on the server's ReqState, on every toggle and once per second, so state survives restarts and map changes. Bots, the local player and SteamID-less rows are not mutable; every surface reads the state back from the service, so a refused toggle never shows success.
- Retained cvars: `hud_danger_health`, `hud_danger_armor`, `hud_deathnotice(+_time/_count)`, `hud_sideinfo`, `hud_cfefx(+_max/+_volume/+_time)`, `abc_ui_chat` (0 = native chat, 1 = AbcUi chat unless CaptionMod's `cap_newchat` owns it; set `cap_newchat 0` to hand chat to AbcUi); in-game diagnostics: `abc_ui_debugger` (RmlUi debugger) and `abc_ui_probe` (font/panel/element state dump).
- Cfefx damage feedback: every 1/10 of `hud_cfefx_max` damage queues one star, and the whole queue always plays - a single 5000-damage hit at the default max launches fifty stars in sequence, ten per full accumulator, instead of skipping to the pop. The score pop does not wait for that queue: as soon as the accumulator reaches one full `hud_cfefx_max` it shows immediately, and its image follows the damage reached (`hud_cfefx_max` 1000 shows `NanoDamege1`, 2000 shows `NanoDamege2`, 5000 shows `NanoDamege5`, capped at `NanoDamege10`). The old combo counter within `hud_cfefx_time` still raises the image so consecutive pops climb the ladder, and the side marks may be interrupted while the star queue keeps flowing.
- Cfefx debug drivers (Debug builds, `modules/cfefx`): `abc_ui_cfefx_hit` (one mark), `abc_ui_cfefx_tier` (clear, then nine marks), `abc_ui_cfefx_score` (one full accumulator = ten stars plus an immediate `NanoDamege1` pop), `abc_ui_cfefx_damage` (one injection of `abc_ui_cfefx_amount` damage, default 8000, playing the full star queue and popping the matching image immediately), `abc_ui_cfefx_sound`, `abc_ui_cfefx_reset` and `abc_ui_cfefx_status` (also echoes the current `abc_ui_cfefx_amount`). The module ABI carries no command arguments, so each action is its own command and the damage amount is a cvar.
- Avatars and real names come from the dynamically bound game steam_api.dll (Bridge/Native backend abstraction, no steam_api.lib import dependency); fonts are loaded from the system font directory through `Rml::LoadFontFace` (Verdana/Tahoma, Microsoft YaHei for CJK, Consolas, and Segoe UI Emoji/Symbol as the colour-emoji and symbol fallbacks, in that order so colour glyphs win over the monochrome symbols).

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
| `thirdparty/miniz` | https://github.com/richgel999/miniz | tag `3.1.2` (`77d0dce8627735138c51770d1799a1ef48f2117d`, read-only zip packages) |
| `thirdparty/SteamSDK` | https://github.com/MetaHookSv/SteamSDK | `3c1abaf6277f9f99fd16ef40557d6852820b848f` |

## Shared dependency

AbcCore is managed by the aggregate
[ABCEnchance](https://github.com/DrAbcOfficial/ABCEnchance) repository. Set
`ABC_CORE_DIR` to its source tree when building AbcUi standalone; the default is
`../AbcCore`. When building from the aggregate repository, point
`ABC_CORE_DIR` at its root `AbcCore/` checkout.

## License

Licensed under the [BSD 3-Clause License](LICENSE); each dependency keeps its own license.
