#pragma once

// sdk/abcui/events.h - event bus channel constants and payload PODs.
//
// Channel ids are lowercase dotted strings owned by the publishing
// subsystem; every channel documents its payload struct here. Payloads are
// C PODs with a stable layout (fixed-size character arrays sized to the
// engine's limits) so they cross the pure-C module ABI. A sink receives a
// const pointer valid for the dispatch only - copy out anything kept.
//
// Naming:
//   netmsg.<message>  - hooked engine user messages (raw parse results)
//   textfeed.<feed>   - routed text feeds (kill feed, notices)
//   hud.<state>       - HUD state changes (health, battery, flashlight, ...)
//   input.<operation> - user operations (key events, mouse, commands)
//   client.<event>    - engine client lifecycle pumps (frame, redraw, ...)
//   menu.<event>      - native GameUI/BaseUI takeover events
//
// Veto channels (documented per constant) stop at the first sink returning
// nonzero - a higher-priority interception hides the event from everything
// below it.

#include "module_api.h"

#pragma pack(push, 8)

// hud.native.draw - veto, queried before the native HUD redraw and before the
// native viewport overlays paint. Nonzero hides only this native element for
// this redraw/frame. Modules must return zero while their replacement
// document is unavailable. No claim = native. These are whole client
// elements: AMMO includes its weapon-selection UI; HEALTH includes damage
// indicators; SPECTATOR is the viewport's observer bottom bar (mode, target,
// health and armor). TRAIN is intentionally not claimable.
#define ABCUI_CHANNEL_HUD_NATIVE_DRAW "hud.native.draw"
#define ABCUI_NATIVE_HUD_HEALTH 0
#define ABCUI_NATIVE_HUD_BATTERY 1
#define ABCUI_NATIVE_HUD_AMMO 2
#define ABCUI_NATIVE_HUD_FLASHLIGHT 4
#define ABCUI_NATIVE_HUD_CROSSHAIR 5
#define ABCUI_NATIVE_HUD_SPECTATOR 6
typedef struct AbcUiNativeHudEvent {
  int32_t element;
} AbcUiNativeHudEvent;

// module.<id>.<name> - immutable envelope; payload borrowed for dispatch only.
typedef struct AbcUiModuleEvent {
  uint32_t structSize;
  uint32_t schemaVersion;
  uint32_t payloadSize;
  const void *payload;
} AbcUiModuleEvent;

// netmsg.flashlight - flashlight toggled; battery 0..100.
#define ABCUI_CHANNEL_NETMSG_FLASHLIGHT "netmsg.flashlight"
typedef struct AbcUiFlashlightEvent {
  int32_t on;
  int32_t battery;
} AbcUiFlashlightEvent;

// netmsg.flashbat - flashlight battery update.
#define ABCUI_CHANNEL_NETMSG_FLASHBAT "netmsg.flashbat"
typedef struct AbcUiFlashBatEvent {
  int32_t battery;
} AbcUiFlashBatEvent;

// hud.suit - HEV suit possession changed (weapon bits bit 31).
#define ABCUI_CHANNEL_HUD_SUIT "hud.suit"
typedef struct AbcUiBoolEvent {
  int32_t value;
} AbcUiBoolEvent;

// hud.longjump - longjump module possession changed (physinfo "slj").
#define ABCUI_CHANNEL_HUD_LONG_JUMP "hud.longjump"

// hud.intermission - the client entered/left the end-of-map intermission
// (the scoreboard table shows for the duration).
#define ABCUI_CHANNEL_HUD_INTERMISSION "hud.intermission"

// hud.scorePointer - the scoreboard's pointer mode changed; nonzero = the
// cursor is unlocked and the table owns mouse input.
#define ABCUI_CHANNEL_HUD_SCORE_POINTER "hud.scorePointer"

// hud.spectate - the local player entered/left observer mode.
#define ABCUI_CHANNEL_HUD_SPECTATE "hud.spectate"

// netmsg.health - the server Health value (predicted health rides the hud
// state snapshot; this is the message-fed value).
#define ABCUI_CHANNEL_NETMSG_HEALTH "netmsg.health"

// netmsg.battery - the server Battery value (armor).
#define ABCUI_CHANNEL_NETMSG_BATTERY "netmsg.battery"

// hud.hideToken - raw HideWeapon/HideHUD token, last write wins. Bits:
#define ABCUI_CHANNEL_HUD_HIDE_TOKEN "hud.hideToken"
typedef struct AbcUiIntEvent {
  int32_t value;
} AbcUiIntEvent;

#define ABCUI_HIDEBIT_WEAPONS (1 << 0)
#define ABCUI_HIDEBIT_FLASHLIGHT (1 << 1)
#define ABCUI_HIDEBIT_ALL (1 << 2)
#define ABCUI_HIDEBIT_HEALTH (1 << 3)
#define ABCUI_HIDEBIT_SELECTION (1 << 4)
#define ABCUI_HIDEBIT_BATTERY (1 << 5)
#define ABCUI_HIDEBIT_CUSTOM1 (1 << 6)
#define ABCUI_HIDEBIT_CUSTOM2 (1 << 7)

// hud.chooseWeapon - the weapon-selection highlight moved (slot keys /
// invnext / invprev). The payload is the registry identity (slot, slotPos);
// slot < 0 means "nothing selected" (the highlight walked off the owned
// list).
#define ABCUI_CHANNEL_HUD_CHOOSE_WEAPON "hud.chooseWeapon"
typedef struct AbcUiWeaponEvent {
  int32_t slot;
  int32_t slotPos;
} AbcUiWeaponEvent;

// hud.pickupWeapon / hud.dropWeapon - one registry weapon changed ownership.
// Same payload as hud.chooseWeapon.
#define ABCUI_CHANNEL_HUD_PICKUP_WEAPON "hud.pickupWeapon"
#define ABCUI_CHANNEL_HUD_DROP_WEAPON "hud.dropWeapon"

// hud.dropAllWeapons - every owned weapon went away (payload is null).
#define ABCUI_CHANNEL_HUD_DROP_ALL_WEAPONS "hud.dropAllWeapons"

// HUD_Frame pump payload; also the "module ran this frame" heartbeat.
#define ABCUI_CHANNEL_FRAME "client.frame"
typedef struct AbcUiFrameEvent {
  double frameTime; // seconds, engine frame time
} AbcUiFrameEvent;

// HUD_Redraw pump payload (view matrices live; projection phase).
#define ABCUI_CHANNEL_REDRAW "client.redraw"
typedef struct AbcUiRedrawEvent {
  double frameTime;
} AbcUiRedrawEvent;

// netmsg.showmenu - vanilla ShowMenu (number-key menu stream). `text` is a
// dispatch-scoped pointer: copy before the sink returns. Chunked menus
// arrive as several events (bits & 0x7f = more chunks pending, bit 7 =
// ammo-selection menu).
// Data subscribers observe only; subscribeVeto explicitly consumes the native
// message when a replacement can handle it. No veto = original handler.
#define ABCUI_CHANNEL_NETMSG_SHOW_MENU "netmsg.showmenu"
typedef struct AbcUiShowMenuEvent {
  int32_t validSlots;
  int32_t displaySeconds;
  int32_t bits;
  const char *text;
} AbcUiShowMenuEvent;

// netmsg.inithud - the client re-ran its HUD init (payload is null).
#define ABCUI_CHANNEL_NETMSG_INIT_HUD "netmsg.inithud"

// netmsg.votemenu - the server opened a yes/no vote; `type` is the vanilla
// vote enum (1 kill, 2 kick, 3 ban, 4 map, ...). The three strings are
// dispatch-scoped pointers: copy before the sink returns. The message is
// only routed while the hud_vote gate is on (console->setVoteGate).
// Data subscribers observe only; subscribeVeto explicitly consumes the native
// message when a replacement can handle it. No veto = original handler.
#define ABCUI_CHANNEL_NETMSG_VOTE_MENU "netmsg.votemenu"
typedef struct AbcUiVoteMenuEvent {
  int32_t type;
  const char *content; // server caption text
  const char *yes;     // server-provided yes caption, "" when absent
  const char *no;      // server-provided no caption, "" when absent
} AbcUiVoteMenuEvent;

// netmsg.endvote - the vote window closed (payload is null).
#define ABCUI_CHANNEL_NETMSG_END_VOTE "netmsg.endvote"

// netmsg.motd - one MOTD chunk from the server. `text` is a dispatch-scoped
// pointer; an empty text means "close the window" (old empty-chunk
// semantics) and code > 0 marks the final chunk of the message.
#define ABCUI_CHANNEL_NETMSG_MOTD "netmsg.motd"
typedef struct AbcUiMotdEvent {
  int32_t code;
  const char *text;
} AbcUiMotdEvent;

// menu.vguimenu - the client asked for a native viewport window
// (CTeamFortressViewport::ShowVGUIMenu ids: 4 mission brief, 5 MOTD, ...).
// Veto channel: nonzero suppresses the vanilla window.
#define ABCUI_CHANNEL_MENU_VGUI_MENU "menu.vguimenu"
typedef struct AbcUiVguiMenuEvent {
  int32_t menu;
} AbcUiVguiMenuEvent;

// menu.state - one replacement-menu presentation transition: the menu layer
// went up/down, the page stack top moved, the console overlay opened/closed,
// the loading attempt started/finished, or the backend phase changed.
// Payload AbcUiMenuState (module_api.h). Data channel; the framework publishes
// it after syncing the presentation, so a page module may show/hide its own
// document in response.
#define ABCUI_CHANNEL_MENU_STATE "menu.state"

// menu.paint - the presentation beat, publishing the same AbcUiMenuState:
// once after every sync and once per frame while the menu layer is up. A page
// module projects its DOM here (change-gated work only - this runs every
// frame).
#define ABCUI_CHANNEL_MENU_PAINT "menu.paint"

// Engine key numbers the input.key channel carries (HLSDK keydefs.h values;
// the ABI cannot include the engine headers).
#define ABCUI_KEY_ENTER 13
#define ABCUI_KEY_ESCAPE 27
#define ABCUI_KEY_BACKSPACE 127
#define ABCUI_KEY_UPARROW 128
#define ABCUI_KEY_DOWNARROW 129
#define ABCUI_KEY_LEFTARROW 130
#define ABCUI_KEY_RIGHTARROW 131
#define ABCUI_KEY_KP_ENTER 169
#define ABCUI_KEY_DELETE 148
#define ABCUI_KEY_MWHEELDOWN 239
#define ABCUI_KEY_MWHEELUP 240

// menu.commandkey - one key edge the menu layer claimed while a keyboard
// window page is on top (the command menu, the inventory window). Veto
// channel (aggregate): a sink returning nonzero consumed the edge (it
// performed the navigation). Payload is AbcUiKeyEvent with `down` always 1
// (the menu owner sees key-down edges only); `keynum` uses the engine
// numbering above plus the printable ASCII codes for letters/digits and
// the HLSDK numpad block (K_KP_HOME 160..K_KP_PLUS 174).
#define ABCUI_CHANNEL_MENU_COMMAND_KEY "menu.commandkey"

// settings.escape - one ESC press the menu layer routed while the settings
// page is on top. Veto channel: the page returns nonzero when it consumed
// the press on one of its own layers (the unsaved-changes prompt, the spray
// picker, the model picker's menu) and the menu owner then re-syncs instead
// of popping the page. Payload is AbcUiKeyEvent (down 1, keynum
// ABCUI_KEY_ESCAPE).
#define ABCUI_CHANNEL_SETTINGS_ESCAPE "settings.escape"

// input.key - one HUD key event. Veto channel: a sink returning nonzero
// consumes the press so the engine binding chain never sees it (the vote
// keys answer the vote instead of running their binding).
#define ABCUI_CHANNEL_INPUT_KEY "input.key"
typedef struct AbcUiKeyEvent {
  int32_t down;   // 1 key down, 0 key up
  int32_t keynum; // engine key number
} AbcUiKeyEvent;

// input.wheel - one mouse-wheel pulse (AbcUiKeyEvent payload). Veto channel
// with claim semantics: nonzero routes the pulse into the Rml context (the
// open document scrolls) without taking input ownership; zero leaves it to
// the engine bindings (weapon switch).
#define ABCUI_CHANNEL_INPUT_WHEEL "input.wheel"

// netmsg.curweapon - the client switched/holstered its weapon; `state`
// carries the old WEAPONSTATE bits (valid/onTarget/uzi).
#define ABCUI_CHANNEL_NETMSG_CUR_WEAPON "netmsg.curweapon"
typedef struct AbcUiCurWeaponEvent {
  int32_t state;
  int32_t id;
  int32_t clip;
  int32_t clip2;
} AbcUiCurWeaponEvent;

// netmsg.ammox - one ammo pool changed (index is the AmmoX byte domain).
#define ABCUI_CHANNEL_NETMSG_AMMO_X "netmsg.ammox"
typedef struct AbcUiAmmoEvent {
  int32_t index;
  int32_t count;
} AbcUiAmmoEvent;

// netmsg.weappickup - the player picked up weapon `id`.
#define ABCUI_CHANNEL_NETMSG_WEAP_PICKUP "netmsg.weappickup"
typedef struct AbcUiWeaponIdEvent {
  int32_t id;
} AbcUiWeaponIdEvent;

// netmsg.ammopickup - the player picked up ammo (AbcUiAmmoEvent).
#define ABCUI_CHANNEL_NETMSG_AMMO_PICKUP "netmsg.ammopickup"

// netmsg.itempickup - the player picked up the hud.txt item `name`
// (dispatch-scoped pointer).
#define ABCUI_CHANNEL_NETMSG_ITEM_PICKUP "netmsg.itempickup"
typedef struct AbcUiItemPickupEvent {
  const char *name;
} AbcUiItemPickupEvent;

// netmsg.damage - the Damage usermsg (who hit us, how much, which damage
// types). `from` is the attacker's world origin.
#define ABCUI_CHANNEL_NETMSG_DAMAGE "netmsg.damage"
typedef struct AbcUiDamageEvent {
  int32_t armor;
  int32_t damage;
  int32_t tiles; // DMG_* bit mask (old bitsDamage)
  float from[3];
} AbcUiDamageEvent;

// netmsg.popnum - one floating damage/heal number from the addon carrier
// (world origin, value, RGBA tint).
#define ABCUI_CHANNEL_NETMSG_POP_NUM "netmsg.popnum"
typedef struct AbcUiPopNumEvent {
  float origin[3];
  int32_t value;
  int32_t color[4];
} AbcUiPopNumEvent;

// Engine modtype_t values carried by hud.addentity (com_model.h order).
#define ABCUI_MODEL_BRUSH 0
#define ABCUI_MODEL_SPRITE 1
#define ABCUI_MODEL_ALIAS 2
#define ABCUI_MODEL_STUDIO 3
// Entity effects bit that hides the entity from the HUD (EF_NODRAW).
#define ABCUI_EF_NODRAW 128
// movetype_t value of an attachment that follows another entity
// (baseline.movetype; never a highlight target).
#define ABCUI_MOVETYPE_FOLLOW 12

// hud.addentity - one HUD_AddEntity pass, published before the engine's own
// handler (models/origins are dispatch-scoped copies; nothing borrows the
// entity pointer).
#define ABCUI_CHANNEL_HUD_ADD_ENTITY "hud.addentity"
typedef struct AbcUiAddEntityEvent {
  int32_t type;
  int32_t index;     // entity index
  int32_t modelType; // mod_studio / mod_sprite / ... (engine model_t type)
  int32_t messageNum;
  int32_t effects;    // entity effects bits (ABCUI_EF_NODRAW)
  int32_t modelIndex; // curstate.modelindex (engine model registry index)
  int32_t moveType;   // baseline.movetype (ABCUI_MOVETYPE_*)
  char model[96];     // model name as passed to HUD_AddEntity
  float origin[3];
} AbcUiAddEntityEvent;

// textfeed.saytext - one chat line (SayText usermsg, plus the server
// console's own TextMsg/HUD_PRINTTALK lines re-published with client 0).
// Veto channel: a sink returning nonzero swallows the native draw, which is
// how exactly one chat log stays on screen (every sink passes = the native
// handler chains, keeping chat alive while the Rml log is disabled).
// `text` and the four parameters are dispatch-scoped pointers.
#define ABCUI_CHANNEL_TEXTFEED_SAY_TEXT "textfeed.saytext"
typedef struct AbcUiSayTextEvent {
  int32_t client; // sender entity index
  const char *text;
  const char *params[4];
} AbcUiSayTextEvent;

// textfeed.killfeed - one routed kill line. All three strings are
// dispatch-scoped pointers; `attacker` is empty for a suicide. `inflictor`
// is the localized weapon phrase (the old kill feed's bracket text).
// Data subscribers observe only; subscribeVeto explicitly consumes the native
// message when a replacement can handle it. No veto = original handler.
#define ABCUI_CHANNEL_TEXTFEED_KILL_FEED "textfeed.killfeed"
typedef struct AbcUiKillFeedEvent {
  const char *victim;
  const char *attacker;
  const char *inflictor;
} AbcUiKillFeedEvent;

// textfeed.notice - one routed centerprint/notify line. `text` is a
// dispatch-scoped pointer; `target` is the old HUDNOTICE value (1 notify,
// 4 center).
// Data subscribers observe only; subscribeVeto explicitly consumes the native
// message when a replacement can handle it. No veto = original handler.
#define ABCUI_CHANNEL_TEXTFEED_NOTICE "textfeed.notice"
typedef struct AbcUiNoticeEvent {
  int32_t target;
  const char *text;
} AbcUiNoticeEvent;

#pragma pack(pop)
