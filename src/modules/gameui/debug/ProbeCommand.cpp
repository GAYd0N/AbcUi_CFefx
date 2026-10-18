// rmlui::ProbeCommand implementation - see ProbeCommand.h.
#include "ProbeCommand.h"

#ifndef NDEBUG

#include <metahook.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Format.h>

#include <keydefs.h>
#include <abc/Math.h>

#include "../../../data/HudState.h"
#include "../../../framework/PanelManager.h"
#include "../../../rmlui/texture/Avatars.h"
#include "../../../data/NetworkMessages.h"
#include "../../../data/PlayerResource.h"
#include "../../../rmlui/texture/SpriteResource.h"
#include "../../../data/WeaponResource.h"
#include "../../../panels/cfefx/CfefxPanel.h"
#include "../../../panels/itemhighlight/ItemHighlightPanel.h"
#include "../../../panels/radar/RadarPanel.h"
#include "../../../panels/scoreboard/ScoreboardPanel.h"

#include "../../../services/steam/SteamAPI.h"

#include "../../../rmlui/RmlUiModule.h"
#include "../../../rmlui/PaintGate.h"
#include "../../../rmlui/TextInput.h"
#include "../../../rmlui/VGUI2ExtensionImport.h"
#include "../../../rmlui/backdrop/ElementBackdrop.h"
#include "../../../rmlui/backdrop/Backdrop.h"
#include "../../../services/menumusic/MenuMusic.h"
#include "../../../services/friends/FriendsModel.h"
#include "../../../services/GameUiBridge.h"
#include "../../../services/console/ConsoleBackend.h"
#include "../../../services/console/ConsoleBuffer.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
namespace panels::scoreboard {}

namespace hooks {
int debugKeyEvent(int down, int keynum);
}

namespace rmlui {

namespace {

// Input-reach echo for the acceptance driver: the driver moves the engine
// cursor / traps an engine key and asserts these lines on the stdout mirror
// (same instrument role the retired DemoPanel played in Phase 3, but
// Debug-builds-only and attached to the probe document).
class ProbeInputEcho : public Rml::EventListener {
public:
  void ProcessEvent(Rml::Event &event) override {
    if (event.GetId() == Rml::EventId::Keydown) {
      abc::ConsolePrint("[AbcUi] probe key id=%d\n",
                        event.GetParameter<int>("key_identifier", 0));
      return;
    }
    abc::ConsolePrint("[AbcUi] probe mouse %d,%d\n",
                      event.GetParameter<int>("mouse_x", 0),
                      event.GetParameter<int>("mouse_y", 0));
  }
};
ProbeInputEcho InputEcho;
Rml::ElementDocument *ProbeDoc = nullptr;

// T15 diagnostics: the console cache/backend state without touching the
// text itself. `abc_ui_console_status` prints counters only;
// `abc_ui_console_dump <n>` prints the newest n cached lines through the
// engine console - the capture is detached around the print so the dump
// does not re-enter the cache or pollute it (the buffer itself stays the
// producer-owned mirror; the dump is a Debug instrument).
void consoleStatusCommand() {
  const auto &buffer = services::console::consoleBuffer();
  const auto &backend = services::console::consoleBackend();
  const auto bufferStats = buffer.stats();
  const auto backendCounters = backend.counters();
  abc::ConsolePrint(
      "[AbcUi] console: gen=%llu lines=%d seq=%llu fragments=%llu "
      "droppedBytes=%llu droppedLines=%llu rejectedAfterShutdown=%llu "
      "stopped=%d attached=%d\n",
      static_cast<unsigned long long>(buffer.generation()),
      static_cast<int>(buffer.lineCount()),
      static_cast<unsigned long long>(buffer.latestSequence()),
      static_cast<unsigned long long>(bufferStats.Fragments),
      static_cast<unsigned long long>(bufferStats.DroppedBytes),
      static_cast<unsigned long long>(bufferStats.DroppedLines),
      static_cast<unsigned long long>(bufferStats.RejectedAfterShutdown),
      buffer.stopped() ? 1 : 0,
      services::console::consoleCapture().attached() ? 1 : 0);
  abc::ConsolePrint(
      "[AbcUi] console: submitted=%llu rejectedEmpty=%llu rejectedInvalid=%llu "
      "unavailable=%llu sensitiveSkipped=%llu history=%d\n",
      static_cast<unsigned long long>(backendCounters.Executed),
      static_cast<unsigned long long>(backendCounters.RejectedEmpty),
      static_cast<unsigned long long>(backendCounters.RejectedInvalid),
      static_cast<unsigned long long>(backendCounters.Unavailable),
      static_cast<unsigned long long>(backendCounters.HistorySkippedSensitive),
      static_cast<int>(backend.history().size()));
}

void consoleDumpCommand() {
  int newest = 10;
  if (gEngfuncs.Cmd_Argc() >= 2)
    newest = atoi(gEngfuncs.Cmd_Argv(1));
  if (newest <= 0)
    newest = 10;
  if (newest > 64)
    newest = 64;
  const std::uint64_t latest =
      services::console::consoleBuffer().latestSequence();
  const std::uint64_t since = latest > static_cast<std::uint64_t>(newest)
                                  ? latest - static_cast<std::uint64_t>(newest)
                                  : 0;
  std::vector<services::console::ConsoleLine> lines;
  const std::size_t count = services::console::consoleBuffer().copySince(
      since, lines, static_cast<std::size_t>(newest));
  // The dump prints through the engine console; detach the capture for
  // the duration so the printed copy does not re-enter the cache.
  auto &capture = services::console::consoleCapture();
  const bool wasAttached = capture.attached();
  if (wasAttached)
    capture.detach();
  for (const auto &line : lines)
    abc::ConsolePrint("[AbcUi] console[%llu%s%s] %s\n",
                      static_cast<unsigned long long>(line.Sequence),
                      line.Developer ? " dev" : "",
                      line.Truncated ? " trunc" : "", line.Text.c_str());
  if (wasAttached)
    capture.attach();
  abc::ConsolePrint("[AbcUi] console dump: %d lines\n",
                    static_cast<int>(count));
}

// T15 diagnostics: drive the REAL submission path (validation, sensitive
// history rule, engine command channel) from the debug console.
void consoleSubmitCommand() {
  if (gEngfuncs.Cmd_Argc() < 2) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_console_submit <command...>\n");
    return;
  }
  std::string command;
  for (int i = 1; i < gEngfuncs.Cmd_Argc(); ++i) {
    if (i > 1)
      command += ' ';
    command += gEngfuncs.Cmd_Argv(i);
  }
  const auto result =
      services::console::consoleBackend().submit(command.c_str());
  abc::ConsolePrint("[AbcUi] console submit result=%d \"%s\"\n",
                    static_cast<int>(result), command.c_str());
}

void consoleCompleteCommand() {
  const char *prefix = gEngfuncs.Cmd_Argc() >= 2 ? gEngfuncs.Cmd_Argv(1) : "";
  const auto matches = services::console::consoleBackend().complete(prefix, 64);
  abc::ConsolePrint("[AbcUi] console complete \"%s\": %d matches\n", prefix,
                    static_cast<int>(matches.size()));
  for (const auto &match : matches)
    abc::ConsolePrint("[AbcUi]   %s\n", match.c_str());
}

// Server-provided sprite diagnostic (abc_ui_sprprobe): the WeaponStack
// icons come from the server's own hud txt + downloaded .spr atlases. A
// solid tinted quad means the texture handle stayed 0; this dump walks the
// live DOM sources and re-resolves each stage so read / decode / crop /
// upload failure is visible instead of silent. An explicit atlas argument
// dumps one file's bytes + header whether or not a panel uses it yet.
void spriteProbeCommand() {
  if (gEngfuncs.Cmd_Argc() >= 2) {
    sprites::debugDumpAtlas(gEngfuncs.Cmd_Argv(1));
    return;
  }
  Rml::Context *ctx = rmlui::context();
  if (!ctx)
    return;
  for (const char *docName : {"weaponstack", "ammostack", "itemstack"}) {
    Rml::ElementDocument *doc = ctx->GetDocument(docName);
    if (!doc)
      continue;
    Rml::Element *container = doc->GetElementById("stack");
    if (!container)
      continue;
    const int count = container->GetNumChildren();
    abc::ConsolePrint("[AbcUi] sprprobe %s items=%d visible=%d\n", docName,
                      count, doc->IsVisible() ? 1 : 0);
    for (int i = 0; i < count; i++) {
      Rml::Element *item = container->GetChild(i);
      Rml::Element *icon = item->GetFirstChild();
      const Rml::String src =
          icon ? icon->GetAttribute<Rml::String>("src", "") : "";
      if (std::string_view(docName) == "weaponstack") {
        const int weaponId = item->GetAttribute<int>("weaponid", 0);
        const weaponres::Weapon *weapon = weaponres::getWeapon(weaponId);
        abc::ConsolePrint("[AbcUi] sprprobe[%d] id=%d name='%s' valid=%d "
                          "atlas='%s' rect=%d,%d,%d,%d hasAmmo=%d src='%s'\n",
                          i, weaponId, weapon ? weapon->name.c_str() : "?",
                          weapon && weapon->inactive.valid ? 1 : 0,
                          weapon ? weapon->inactive.atlas.c_str() : "",
                          weapon ? weapon->inactive.rect.left : 0,
                          weapon ? weapon->inactive.rect.top : 0,
                          weapon ? weapon->inactive.rect.right : 0,
                          weapon ? weapon->inactive.rect.bottom : 0,
                          weapon && weaponres::hasAmmo(weapon) ? 1 : 0,
                          src.c_str());
      } else {
        abc::ConsolePrint("[AbcUi] sprprobe[%d] src='%s'\n", i, src.c_str());
      }
      if (!src.empty())
        sprites::debugResolveUri(src);
    }
  }
}

// Live font-pipeline probe (batch A acceptance aid): builds a document from
// an inline sheet - three explicit font families plus one line styled only
// through the file-based base.rcss. Which lines render localizes a
// missing-text report (face lookup vs base.rcss vs document markup).
void probeCommand() {
  if (ProbeDoc) {
    ProbeDoc->Close();
    ProbeDoc = nullptr;
    abc::ConsolePrint("[AbcUi] probe closed\n");
    return;
  }
  Rml::Context *context = rmlui::context();
  if (!context)
    return;
  // Also dump the live health panel's bar-wrap state: inline width value +
  // computed box - the decisive data for the "armor bar does not shrink"
  // report.
  if (Rml::ElementDocument *health = context->GetDocument("health")) {
    if (Rml::Element *wrap = health->GetElementById("armor-bar-wrap")) {
      const Rml::Box box = wrap->GetBox();
      const Rml::Vector2f content = box.GetSize();
      abc::ConsolePrint("[AbcUi] probe: armor-bar-wrap inline width='%s' "
                        "computed=%.2fx%.2f\n",
                        wrap->GetProperty("width")->ToString().c_str(),
                        content.x, content.y);
    } else {
      abc::ConsoleWarn("[AbcUi] probe: armor-bar-wrap not found\n");
    }
  }
  abc::ConsolePrint(
      "[AbcUi] probe: hudstate suit=%d hideFlash=%d tokens=%d spectate=%d\n",
      hudstate::hasSuit() ? 1 : 0,
      hudstate::isHudHidden(hudstate::kHideFlashlight) ? 1 : 0,
      hudstate::debugHideTokens(), hudstate::isSpectate() ? 1 : 0);
  // Steam/avatar chain ground truth (P6 avatar round): which backend won,
  // the id it serves, and a live fetch on the LOCAL id - that avatar is
  // always resident, so a failure here pins the broken link (connect /
  // handle / image decode) with no second client needed.
  {
    const steamapi::Backend steamBackend = steamapi::backend();
    const steamapi::SteamId64 local = steamapi::localSteamId();
    steamapi::AvatarRgba av;
    const bool fetched = steamapi::avatar(local, av);
    abc::ConsolePrint(
        "[AbcUi] probe: steam backend=%d local=%llu fetch=%d %dx%d gen=%d\n",
        static_cast<int>(steamBackend), static_cast<unsigned long long>(local),
        fetched ? 1 : 0, av.width, av.height, avatars::generation(local));
  }
  // Keydown follows the context focus; print who owns it so a stuck input
  // report localizes in one probe run.
  if (Rml::Element *focus = context->GetFocusElement()) {
    Rml::ElementDocument *owner = focus->GetOwnerDocument();
    abc::ConsolePrint("[AbcUi] probe: focus doc id='%s' tag='%s' (element "
                      "tag='%s' id='%s')\n",
                      owner ? owner->GetId().c_str() : "<none>",
                      owner ? owner->GetTagName().c_str() : "",
                      focus->GetTagName().c_str(), focus->GetId().c_str());
  } else {
    abc::ConsoleWarn("[AbcUi] probe: no focus element\n");
  }
  for (const char *name :
       {"health", "flashlight", "ammobar", "ammostack", "itemstack",
        "weaponstack", "deadmsg", "notice", "notice-center", "sidetext",
        "textmenu", "crosshair", "indicator", "gindicator", "dmgtiles",
        "effect", "popnum"}) {
    if (Rml::ElementDocument *doc = context->GetDocument(name))
      abc::ConsolePrint("[AbcUi] probe: doc %-14s visible=%d display='%s'\n",
                        name, doc->IsVisible() ? 1 : 0,
                        doc->GetProperty("display")->ToString().c_str());
  }
  if (Rml::ElementDocument *flashlight = context->GetDocument("flashlight")) {
    if (Rml::Element *off = flashlight->GetElementById("off-image")) {
      const Rml::Box box = off->GetBox();
      const Rml::Vector2f content = box.GetSize();
      abc::ConsolePrint("[AbcUi] probe: flashlight off-image src='%s' "
                        "visibility='%s' computed=%.2fx%.2f\n",
                        off->GetAttribute<Rml::String>("src", "<none>").c_str(),
                        off->GetProperty("visibility")->ToString().c_str(),
                        content.x, content.y);
    }
  }
  // Batch B ground truth: the popnum doc's live children with their inline
  // and absolute positions (projection chain: netmsg -> element -> layout).
  if (Rml::ElementDocument *popnum = context->GetDocument("popnum")) {
    if (Rml::Element *numbers = popnum->GetElementById("numbers")) {
      int count = numbers->GetNumChildren();
      abc::ConsolePrint("[AbcUi] probe: popnum children=%d doc-visible=%d\n",
                        count, popnum->IsVisible() ? 1 : 0);
      for (int i = 0; i < count && i < 4; i++) {
        Rml::Element *child = numbers->GetChild(i);
        abc::ConsolePrint("[AbcUi] probe: popnum[%d] text='%s' "
                          "inline-left='%s' inline-top='%s' abs=(%.1f %.1f)\n",
                          i, child->GetInnerRML().c_str(),
                          child->GetProperty("left")->ToString().c_str(),
                          child->GetProperty("top")->ToString().c_str(),
                          child->GetAbsoluteLeft(), child->GetAbsoluteTop());
      }
    }
  }
  // GIndicator ground truth: tracked-marker count + positions.
  if (Rml::ElementDocument *gind = context->GetDocument("gindicator")) {
    if (Rml::Element *markers = gind->GetElementById("markers")) {
      const int count = markers->GetNumChildren();
      abc::ConsolePrint("[AbcUi] probe: gindicator markers=%d\n", count);
      for (int i = 0; i < count && i < 4; i++) {
        Rml::Element *child = markers->GetChild(i);
        abc::ConsolePrint("[AbcUi] probe: gind[%d] display='%s' "
                          "inline-left='%s' inline-top='%s' abs=(%.1f %.1f)\n",
                          i, child->GetProperty("display")->ToString().c_str(),
                          child->GetProperty("left")->ToString().c_str(),
                          child->GetProperty("top")->ToString().c_str(),
                          child->GetAbsoluteLeft(), child->GetAbsoluteTop());
      }
    }
  }
  // Batch C ground truth: the scoreboard box/table display state (the
  // +showscores chain) and the new documents' visibility.
  if (Rml::ElementDocument *board = context->GetDocument("scoreboard")) {
    if (Rml::Element *box = board->GetElementById("box")) {
      Rml::Element *table = box->GetElementById("table");
      abc::ConsolePrint(
          "[AbcUi] probe: scoreboard box display='%s' opacity='%s' abs=(%.1f "
          "%.1f) table-children=%d doc-visible=%d\n",
          box->GetProperty("display")->ToString().c_str(),
          box->GetProperty("opacity")->ToString().c_str(),
          box->GetAbsoluteLeft(), box->GetAbsoluteTop(),
          table ? table->GetNumChildren() : -1, board->IsVisible() ? 1 : 0);
    }
  }
  // WeaponChoose ground truth: the bucket frame must ride the highlighted
  // item - inline left/top plus the projected absolute position localizes a
  // "frame does not follow the selection" report in one probe run.
  if (Rml::ElementDocument *choose = context->GetDocument("weaponchoose")) {
    if (Rml::Element *bucket = choose->GetElementById("bucket")) {
      abc::ConsolePrint(
          "[AbcUi] probe: weaponchoose bucket doc-visible=%d inline-left='%s' "
          "inline-top='%s' inline-size='%s x %s' abs=(%.1f %.1f)\n",
          choose->IsVisible() ? 1 : 0,
          bucket->GetProperty("left")->ToString().c_str(),
          bucket->GetProperty("top")->ToString().c_str(),
          bucket->GetProperty("width")->ToString().c_str(),
          bucket->GetProperty("height")->ToString().c_str(),
          bucket->GetAbsoluteLeft(), bucket->GetAbsoluteTop());
    } else {
      abc::ConsolePrint(
          "[AbcUi] probe: weaponchoose bucket absent doc-visible=%d\n",
          choose->IsVisible() ? 1 : 0);
    }
  }
  // PlayerBoard ground truth (P6 user-test round): updateClientInfo resolves
  // every per-tile element through GetElementById - a tile-2 lookup that
  // returns null means the RML ships the element as class-only and the
  // name/bars/icons updates silently no-op (the "tile shows no name and no
  // icons" report localizes here in one probe run).
  if (Rml::ElementDocument *pb = context->GetDocument("playerboard")) {
    if (Rml::Element *tile = pb->GetElementById("tile-2")) {
      // Subtree-scoped on purpose: the inner ids repeat per tile, and
      // Element::GetElementById would resolve against the whole document
      // (tile-1) - the exact bug this dump exists to catch.
      const auto resolved = [&](const char *id) {
        return Rml::ElementUtilities::GetElementById(tile, id) ? "ok"
                                                               : "MISSING";
      };
      abc::ConsolePrint(
          "[AbcUi] probe: playerboard tile-2 name=%s bar-health=%s "
          "bar-armor=%s icon-crouch=%s icon-medkit=%s icon-dead=%s\n",
          resolved("name"), resolved("bar-health"), resolved("bar-armor"),
          resolved("icon-crouch"), resolved("icon-medkit"),
          resolved("icon-dead"));
    }
  }
  for (const char *doc : {"playerboard", "vote", "motd", "weaponchoose"}) {
    if (Rml::ElementDocument *d = context->GetDocument(doc))
      abc::ConsolePrint("[AbcUi] probe: doc-batchc %s visible=%d\n", doc,
                        d->IsVisible() ? 1 : 0);
  }
  // ItemHighlight ground truth (batch D): tracked-entity count (the
  // HUD_AddEntity chain) cross-referenced with the live marker elements -
  // a mismatch between the two localizes the AddEntity gate vs the DOM path
  // in one probe run.
  if (Rml::ElementDocument *ih = context->GetDocument("itemhighlight")) {
    int markers = 0;
    if (Rml::Element *container = ih->GetElementById("markers")) {
      markers = container->GetNumChildren();
      for (int i = 0; i < markers && i < 4; i++) {
        Rml::Element *child = container->GetChild(i);
        abc::ConsolePrint("[AbcUi] probe: itemhighlight[%d] display='%s' "
                          "inline-left='%s' inline-top='%s' abs=(%.1f %.1f)\n",
                          i, child->GetProperty("display")->ToString().c_str(),
                          child->GetProperty("left")->ToString().c_str(),
                          child->GetProperty("top")->ToString().c_str(),
                          child->GetAbsoluteLeft(), child->GetAbsoluteTop());
      }
    }
    abc::ConsolePrint(
        "[AbcUi] probe: itemhighlight tracked=%d markers=%d doc-visible=%d\n",
        panels::itemhighlight::debugTrackedCount(), markers,
        ih->IsVisible() ? 1 : 0);
  }
  // Radar ground truth (batch D): FBO readiness + plate state + live
  // avatars, so a "no minimap" report splits cleanly between the FBO
  // producer and the DOM consumer.
  if (Rml::ElementDocument *radar = context->GetDocument("radar")) {
    int avatars = 0;
    if (Rml::Element *container = radar->GetElementById("avatars"))
      avatars = container->GetNumChildren();
    abc::ConsolePrint(
        "[AbcUi] probe: radar fbo-tex=%d doc-visible=%d avatars=%d\n",
        panels::radar::debugFboTexture(), radar->IsVisible() ? 1 : 0, avatars);
    if (Rml::Element *map = radar->GetElementById("map")) {
      abc::ConsolePrint("[AbcUi] probe: radar map src='%s' abs=(%.1f %.1f)\n",
                        map->GetAttribute<Rml::String>("src", "<none>").c_str(),
                        map->GetAbsoluteLeft(), map->GetAbsoluteTop());
    }
  }
  // Mouse-chain diagnostics (P6 audit round): cursor state, the R-4 WndProc
  // Menu backdrop (UI overhaul WS3): element presence, the shared service's
  // published image and its blur/decoder state.
  {
    const auto element = backdrop::probeInfo();
    const auto service = menubackdrop::menuBackdrop().diagnostics();
    abc::ConsolePrint("[AbcUi] probe: backdrop elements=%d visible=%d "
                      "published=%d uri='%s'\n",
                      element.Elements, element.Visible,
                      element.Published ? 1 : 0, element.Uri.c_str());
    abc::ConsolePrint("[AbcUi] probe: backdrop doc='%s' box=%.1fx%.1f "
                      "image=%.1fx%.1f src='%s' visibility='%s'\n",
                      element.Document.c_str(), element.Width, element.Height,
                      element.ImageWidth, element.ImageHeight,
                      element.ImageSrc.c_str(),
                      element.ImageVisibility.c_str());
    abc::ConsolePrint("[AbcUi] probe: backdrop mode=%d/%d rounds=%d "
                      "target=%dx%d video=%d/%.2f\n",
                      service.Requested, service.Effective, service.Rounds,
                      service.Width, service.Height, service.VideoOpen ? 1 : 0,
                      service.VideoSeconds);
  }
  // Menu score (UI overhaul WS4): the track the module hands to the engine's
  // mp3 player (looping and loudness are the engine's own; see MenuMusic).
  {
    const auto &music = menumusic::menuMusic();
    abc::ConsolePrint("[AbcUi] probe: music src='%s' on=%d playing=%d\n",
                      music.source().c_str(), music.enabled() ? 1 : 0,
                      music.playing() ? 1 : 0);
  }
  // Friends board (UI overhaul WS4): provider reach, the row set and each
  // row's joinability.
  {
    const auto &friends = services::friends::friendsModel();
    abc::ConsolePrint(
        "[AbcUi] probe: friends available=%d scanned=%d rows=%u gen=%u\n",
        friends.available() ? 1 : 0, friends.lastFriendCount(),
        static_cast<unsigned>(friends.rows().size()), friends.generation());
    for (std::size_t i = 0; i < friends.rows().size() && i < 6; ++i) {
      const auto &row = friends.rows()[i];
      abc::ConsolePrint(
          "[AbcUi] probe: friend[%u] '%s' inGame=%d connect='%s'\n",
          static_cast<unsigned>(i), row.Name.c_str(), row.InGame ? 1 : 0,
          row.Connect.c_str());
    }
  }
  // callback registration and the scoreboard pointer mode (wantsWheel =
  // visible && pointer-mode).
  {
    int cx = -1, cy = -1;
    const bool cursor = vgui2ext::getCursorPos(cx, cy);
    abc::ConsolePrint("[AbcUi] probe: mouse cursor=%d visible=%d pos=(%d,%d) "
                      "wndproc=%d owner=%d scoreboard-wheel=%d\n",
                      cursor ? 1 : 0, vgui2ext::isCursorVisible() ? 1 : 0, cx,
                      cy, vgui2ext::uiCallbacksRegistered() ? 1 : 0,
                      rmlui::uiMouseOwner() ? 1 : 0,
                      panels::scoreboard::wantsWheel() ? 1 : 0);
  }
  const char *rml =
      "<rml>"
      "<head>"
      "<link type=\"text/rcss\" href=\"abc/abcui/rcss/base.rcss\"/>"
      "<style>"
      "	body { margin: 0dp; width: 640dp; height: 640dp;"
      "		background-color: #101418e0; }"
      "	div { display: block; }"
      "	#a { font-family: \"Verdana\"; font-size: 40px; color: #ff0000; }"
      "	#b { font-family: \"Microsoft YaHei\"; font-size: 40px; color: "
      "#00ff00; }"
      "	#c { font-size: 40px; color: #ffff00; }"
      "</style>"
      "</head>"
      "<body>"
      "<div id=\"a\">PROBE-VERDANA-123</div>"
      "<div id=\"b\">PROBE-YAHEI-ABC abc \xe6\xb5\x8b\xe8\xaf\x95</div>"
      "<div id=\"c\">PROBE-UNSPECIFIED-456</div>"
      "<div id=\"d\" class=\"font-default\">PROBE-BASECSS-789</div>"
      "</body>"
      "</rml>";
  ProbeDoc = context->LoadDocumentFromMemory(rml);
  if (ProbeDoc) {
    ProbeDoc->Show();
    // Keydown follows the focus chain; the doc must own focus for the
    // echo to fire when the engine traps a key in-map.
    ProbeDoc->Focus();
    ProbeDoc->AddEventListener(Rml::EventId::Mousemove, &InputEcho);
    ProbeDoc->AddEventListener(Rml::EventId::Keydown, &InputEcho);
    if (Rml::Element *focus = context->GetFocusElement())
      abc::ConsolePrint(
          "[AbcUi] probe: post-open focus tag='%s' id='%s' doc='%s'\n",
          focus->GetTagName().c_str(), focus->GetId().c_str(),
          ProbeDoc->Contains(focus) ? "probe" : "other");
    else
      abc::ConsoleWarn("[AbcUi] probe: post-open focus is null\n");
    abc::ConsolePrint("[AbcUi] probe shown\n");
  } else {
    abc::ConsoleWarn("[AbcUi] probe document failed to load\n");
  }
}

// ShowMenu injector for the acceptance driver: fires the exact event the
// wire parser emits (netmsg::ShowMenu), so a session without a live server
// menu stream can still exercise the real downstream chain - TextMenu
// open/fade, the slot1..10 chained-command gate, the menuselect emission
// and the re-show-cancels-pending-hide path. The canned text carries the
// TextImageEx colour stream the panel must translate.
void menuCommand() {
  Rml::Context *context = rmlui::context();
  if (!context)
    return;
  netmsg::ShowMenu(0x7, 15, 0,
                   "\\r[AbcUi]\\w Menu probe\n"
                   "\n"
                   "\\y1. Alpha option\n"
                   "\\w2. Beta option\n"
                   "\\d3. Gamma disabled\n");
  abc::ConsolePrint(
      "[AbcUi] menu: injected ShowMenu (slots 1-3, 15s, plain menuselect)\n");
}

// Batch B feedback injector: fires the wire-parser events the feedback
// panels subscribe (Damage / Health / PopNum) with a source point 100u
// ahead of the view, so Indicator + DmgTiles + Effect(danger) + PopNum
// run their real downstream chains without a live attacker. GIndicator
// needs a live grenade entity and stays a real-game check.
void feedbackCommand() {
  Rml::Context *context = rmlui::context();
  if (!context)
    return;
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local) {
    abc::ConsoleWarn("[AbcUi] feedback: no local player\n");
    return;
  }
  vec3_t angles{}, forward{};
  gEngfuncs.GetViewAngles(angles);
  abc::math::AngleVectors(angles, forward, nullptr, nullptr);

  netmsg::DamageData damage;
  damage.armor = 25;
  damage.damage = 40;
  damage.tiles = (1 << 17) | (1 << 3) | (1 << 8); // POISON | BURN | SHOCK
  for (int i = 0; i < 3; i++)
    damage.from[i] = local->curstate.origin[i] + forward[i] * 100.0f;
  damage.from[2] += 8.0f;
  netmsg::Damage(damage);

  netmsg::Health(20);

  netmsg::PopNumData popnum;
  for (int i = 0; i < 3; i++)
    popnum.origin[i] = local->curstate.origin[i] + forward[i] * 80.0f;
  popnum.origin[2] += 16.0f;
  popnum.value = 12345;
  popnum.color[0] = 255;
  popnum.color[1] = 200;
  popnum.color[2] = 50;
  popnum.color[3] = 255;
  netmsg::PopNum(popnum);

  abc::ConsolePrint("[AbcUi] feedback: injected Damage(25/40 "
                    "poison+burn+shock) + Health(20) + PopNum(12345)\n");
}

// cfefx manual trigger: fires the wire event the panel subscribes to
// (netmsg::PopNum) instead of poking its private accumulator, so the whole
// chain - subscription, tier split, mark fade, star flight, score pop, the
// engine sound - runs exactly like a live server hit. The ids below are the
// document ids CfefxPanel queries; they are listed only here, in a Debug
// translation unit, so the production surface stays uncoupled (same rule as
// the probe fields).
constexpr const char *kCfefxMarkIds[9] = {
    "dmg-mark-one",   "dmg-mark-two",   "dmg-mark-three",
    "dmg-mark-four",  "dmg-mark-five",  "dmg-mark-six",
    "dmg-mark-seven", "dmg-mark-eight", "dmg-mark-nine",
};
constexpr const char *kCfefxStarIds[9] = {
    "star-one", "star-two",   "star-three", "star-four", "star-five",
    "star-six", "star-seven", "star-eight", "star-nine",
};

panels::CfefxPanel *cfefxPanel() {
  for (const auto &panel : framework::PanelManager::panels()) {
    if (panel->getName() != "cfefx")
      continue;
    return dynamic_cast<panels::CfefxPanel *>(panel.get());
  }
  return nullptr;
}

// One POPNUMBER package 80u ahead of the view: the same point shape the
// server addon sends, so PopNum also reacts to it and the hud_popnumber
// decoupling can be watched side by side (`hud_popnumber 0` must leave the
// cfefx marks running).
bool cfefxInject(int value) {
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local) {
    abc::ConsoleWarn("[AbcUi] cfefx: no local player - nothing to inject\n");
    return false;
  }
  vec3_t angles{}, forward{};
  gEngfuncs.GetViewAngles(angles);
  abc::math::AngleVectors(angles, forward, nullptr, nullptr);

  netmsg::PopNumData popnum;
  for (int i = 0; i < 3; i++)
    popnum.origin[i] = local->curstate.origin[i] + forward[i] * 80.0f;
  popnum.origin[2] += 16.0f;
  popnum.value = value;
  popnum.color[0] = 255;
  popnum.color[1] = 210;
  popnum.color[2] = 80;
  popnum.color[3] = 255;
  netmsg::PopNum(popnum);
  return true;
}

void cfefxDump(const char *label) {
  panels::CfefxPanel *panel = cfefxPanel();
  if (!panel) {
    abc::ConsoleWarn("[AbcUi] cfefx: panel not registered\n");
    return;
  }
  const panels::CfefxPanel::DebugState state = panel->debugState();
  abc::ConsolePrint("[AbcUi] cfefx: %s enabled=%d active=%d visible=%d "
                    "damage=%llu step=%d tier=%d score=%d "
                    "lastScore=%.2fs scoreAge=%.2fs volume=%.2f popNum=%.0f\n",
                    label, state.enabled ? 1 : 0, state.active ? 1 : 0,
                    state.visible ? 1 : 0,
                    static_cast<unsigned long long>(state.damage), state.step,
                    static_cast<int>(state.tier), static_cast<int>(state.score),
                    state.lastScoreAge, state.scoreAge, state.volume,
                    gEngfuncs.pfnGetCvarFloat("hud_popnumber"));
  if (!state.active)
    abc::ConsoleWarn("[AbcUi] cfefx: inactive (no live player, health 0 or HUD "
                     "hidden) - the "
                     "injection ran but nothing draws\n");

  Rml::ElementDocument *doc = panel->getDocument();
  if (!doc) {
    abc::ConsoleWarn("[AbcUi] cfefx: document not loaded\n");
    return;
  }
  std::string marks;
  std::string stars;
  for (int i = 0; i < 9; i++) {
    if (Rml::Element *mark = doc->GetElementById(kCfefxMarkIds[i]))
      marks +=
          abc::Format("{0}:{1} ", i, mark->GetProperty("opacity")->ToString());
    if (Rml::Element *star = doc->GetElementById(kCfefxStarIds[i]))
      stars +=
          abc::Format("{0}:{1} ", i, star->GetProperty("opacity")->ToString());
  }
  abc::ConsolePrint("[AbcUi] cfefx: mark-alpha %s| star-alpha %s\n",
                    marks.c_str(), stars.c_str());
  if (Rml::Element *score = doc->GetElementById("score-mark"))
    abc::ConsolePrint(
        "[AbcUi] cfefx: score src='%s' opacity='%s' | document display='%s'\n",
        score->GetAttribute<Rml::String>("src", "<none>").c_str(),
        score->GetProperty("opacity")->ToString().c_str(),
        doc->GetProperty("display")->ToString().c_str());
}

void cfefxUsage() {
  abc::ConsolePrint("[AbcUi] cfefx: abc_ui_cfefx hit [value] | tier <1..9> | "
                    "score [count] | sound | "
                    "reset | status\n"
                    "[AbcUi] cfefx: hit defaults to one mark of hud_cfefx_max; "
                    "ten marks = one score pop\n");
}

void cfefxCommand() {
  panels::CfefxPanel *panel = cfefxPanel();
  if (!panel) {
    abc::ConsoleWarn("[AbcUi] cfefx: panel not registered\n");
    return;
  }
  if (!panel->getDocument()) {
    abc::ConsoleWarn("[AbcUi] cfefx: document not loaded (in a map yet?)\n");
    return;
  }
  const panels::CfefxPanel::DebugState state = panel->debugState();
  if (state.step <= 0) {
    abc::ConsoleWarn("[AbcUi] cfefx: hud_cfefx_max not registered yet\n");
    return;
  }
  const char *sub = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv(1) : "status";

  if (!strcmp(sub, "hit")) {
    const int requested =
        gEngfuncs.Cmd_Argc() > 2 ? atoi(gEngfuncs.Cmd_Argv(2)) : state.step;
    if (!cfefxInject(requested))
      return;
    cfefxDump("hit");
  } else if (!strcmp(sub, "tier")) {
    const int target = std::clamp(
        gEngfuncs.Cmd_Argc() > 2 ? atoi(gEngfuncs.Cmd_Argv(2)) : 1, 1, 9);
    panel->reset();
    if (!cfefxInject(target * state.step))
      return;
    cfefxDump("tier");
  } else if (!strcmp(sub, "score")) {
    const int count = std::clamp(
        gEngfuncs.Cmd_Argc() > 2 ? atoi(gEngfuncs.Cmd_Argv(2)) : 1, 1, 10);
    for (int i = 0; i < count; i++) {
      // A full accumulator: the tenth mark triggers the score pop and
      // clears the marks, the same edge a live 1000-damage run hits.
      if (!cfefxInject(state.step * 10))
        return;
    }
    cfefxDump("score");
  } else if (!strcmp(sub, "sound")) {
    panel->debugPlayScoreSound();
    abc::ConsolePrint("[AbcUi] cfefx: sound requested at volume %.2f "
                      "(abc/cfefx/UI_SPECIALKILL2.wav)\n",
                      state.volume);
  } else if (!strcmp(sub, "reset")) {
    panel->reset();
    cfefxDump("reset");
  } else if (!strcmp(sub, "status")) {
    cfefxDump("status");
  } else {
    cfefxUsage();
  }
}

// Batch C full-occupancy board instrument: pins 31 fake playerres slots
// (mixed teams/health/donor/admin) so a local session can drive the
// scoreboard table/scroll/context-menu at 32 occupancy. The wire-parse
// layer stays untouched - this feeds the same store the real ScoreInfo
// events write.
// Phase 8 benchmark instrument: prints the accumulated per-frame phase
// timings (Context::Update / Context::Render) and restarts the window.
void perfCommand() {
  const rmlui::PerfSnapshot snapshot = rmlui::perfSnapshot();
  abc::ConsolePrint("[AbcUi] perf: frames=%d update avg=%.3fms max=%.3fms | "
                    "render avg=%.3fms max=%.3fms\n",
                    snapshot.Frames, snapshot.UpdateAvgMs, snapshot.UpdateMaxMs,
                    snapshot.RenderAvgMs, snapshot.RenderMaxMs);
}

void boardCommand() {
  playerres::debugPopulateBoard();
  abc::ConsolePrint("[AbcUi] board: 31 fake players pinned; +showscores opens, "
                    "MOUSE2 pointer, right-click rows\n");
}

// Injected mouse keys (MOUSE2/wheel) never reach the client key chain
// (P6 acceptance finding - real hardware does), so the driver flips the
// pointer mode and pumps the wheel directly.
void pointerOnCommand() {
  panels::scoreboard::setPointerMode(true);
  abc::ConsolePrint("[AbcUi] pointer: on\n");
}

void pointerOffCommand() {
  panels::scoreboard::setPointerMode(false);
  abc::ConsolePrint("[AbcUi] pointer: off\n");
}

void wheelUpCommand() {
  rmlui::onWheel(1, K_MWHEELUP);
  abc::ConsolePrint("[AbcUi] wheel: up\n");
}

void wheelDownCommand() {
  rmlui::onWheel(1, K_MWHEELDOWN);
  abc::ConsolePrint("[AbcUi] wheel: down\n");
}

// Deterministic client-key seam; does not pretend to be hardware input.
void mouseKeyCommand() {
  const int key =
      gEngfuncs.Cmd_Argc() > 1 ? atoi(gEngfuncs.Cmd_Argv(1)) : K_MOUSE2;
  const int down = gEngfuncs.Cmd_Argc() > 2 ? atoi(gEngfuncs.Cmd_Argv(2)) : 1;
  const int result = hooks::debugKeyEvent(down, key);
  abc::ConsolePrint("[AbcUi] input-test key=%d down=%d result=%d owner=%d "
                    "cursor=%d wheel=%d\n",
                    key, down, result, uiMouseOwner(),
                    vgui2ext::isCursorVisible(),
                    panels::scoreboard::wantsWheel());
}

// T07 text seams: the same entry points the engine AppProc events
// drive, from a client command. Does not pretend to be an IME - the
// composition feed is the SDL_EVENT_TEXT_EDITING payload, the text
// feed is SDL_EVENT_TEXT_INPUT. Values print as raw bytes so a
// driver can assert byte-exact UTF-8.
void printFocusedValue(const char *tag) {
  Rml::Context *ctx = rmlui::context();
  Rml::Element *focus = ctx ? ctx->GetFocusElement() : nullptr;
  auto *control = dynamic_cast<Rml::ElementFormControl *>(focus);
  if (!control) {
    abc::ConsolePrint("[AbcUi] text-test %s focus=<none>\n", tag);
    return;
  }
  const Rml::String value = control->GetValue();
  abc::ConsolePrint(
      "[AbcUi] text-test %s tag=%s bytes=%zu value=%s composing=%d\n", tag,
      focus->GetTagName().c_str(), value.size(), value.c_str(),
      rmlui::textComposing() ? 1 : 0);
}

void inputTextCommand() {
  const char *text = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv(1) : "";
  rmlui::feedTextInput(text);
  printFocusedValue("commit");
}

void inputComposeCommand() {
  const char *text = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv(1) : "";
  const int start = gEngfuncs.Cmd_Argc() > 2 ? atoi(gEngfuncs.Cmd_Argv(2)) : 0;
  rmlui::feedTextEditing(text, start);
  printFocusedValue("compose");
}

void inputCancelCommand() {
  rmlui::cancelComposition();
  printFocusedValue("cancel");
}

// T07 event-source counters (V07 on-machine evidence): which channel
// actually delivers ('appproc' = SDL text events, 'wndproc' =
// WM_KEY*/WM_CHAR the surface observes, 'feed' = commits into an
// editor vs dropped for lack of focus).
void inputEventsCommand() {
  unsigned appProcTotal = 0, textInput = 0, textEditing = 0, candidates = 0;
  unsigned wndProcTotal = 0, wndChar = 0, wndKey = 0, commits = 0, drops = 0;
  rmlui::eventCounters(appProcTotal, textInput, textEditing, candidates,
                       wndProcTotal, wndChar, wndKey, commits, drops);
  abc::ConsolePrint(
      "[AbcUi] input-events appproc-total=%u text=%u editing=%u candidates=%u "
      "wndproc-total=%u char=%u key=%u feed-commit=%u drop=%u composing=%d\n",
      appProcTotal, textInput, textEditing, candidates, wndProcTotal, wndChar,
      wndKey, commits, drops, rmlui::textComposing() ? 1 : 0);
  unsigned wndTypes[256] = {}, appTypes[256] = {};
  rmlui::messageHistogram(wndTypes, appTypes);
  for (int i = 0; i < 256; ++i) {
    if (wndTypes[i])
      abc::ConsolePrint("[AbcUi] input-events wnd-msg=0x%02x count=%u\n", i,
                        wndTypes[i]);
    if (appTypes[i])
      abc::ConsolePrint("[AbcUi] input-events app-type=0x%02x count=%u\n", i,
                        appTypes[i]);
  }
}

// T08 acceptance (V01/V08 evidence): the native-UI suppression state
// and the before-filter/after-filter counts. Allowed for a native
// bucket = attempts - blocked and must be 0 while armed.
void nativeCommand() {
  const auto &bridge = services::nativeUiBridge();
  const auto c = bridge.counters();
  abc::ConsolePrint("[AbcUi] native armed=%d gate=%d menuUp=%d page=%d "
                    "baseUiKeys=%u baseUiToggles=%u\n",
                    bridge.armed() ? 1 : 0, rmlui::paintGateInstalled() ? 1 : 0,
                    bridge.menuUp() ? 1 : 0, bridge.pageShown() ? 1 : 0,
                    c.BaseUiKeyEvents, c.BaseUiToggles);
  const char *buckets[] = {"gameui", "serverbrowser", "other"};
  for (int i = 0; i < (int)services::PaintBucket::kCount; i++) {
    abc::ConsolePrint(
        "[AbcUi] native paint %s attempts=%u blocked=%u allowed=%u\n",
        buckets[i], c.PaintAttempts[i], c.PaintBlocked[i],
        c.PaintAttempts[i] - c.PaintBlocked[i]);
    abc::ConsolePrint(
        "[AbcUi] native hittest %s attempts=%u blocked=%u allowed=%u\n",
        buckets[i], c.HitTestAttempts[i], c.HitTestBlocked[i],
        c.HitTestAttempts[i] - c.HitTestBlocked[i]);
  }
  abc::ConsolePrint("[AbcUi] native input-flags-cleared=%u up=%u down=%u "
                    "escRouted=%u escRepeats=%u escUps=%u\n",
                    c.InputFlagsCleared, c.MenuUpTransitions,
                    c.MenuDownTransitions, c.EscRouted, c.EscRepeatsSuppressed,
                    c.PhysicalEscUps);
  abc::ConsolePrint("[AbcUi] native queries exclusive=%u active=%u console=%u "
                    "runFrames=%u loading=%u/%u disc=%u\n",
                    c.ExclusiveQueries, c.ActiveQueries,
                    c.ConsoleVisibleQueries, c.RunFrames, c.LoadingStarts,
                    c.LoadingFinishes, c.Disconnects);
  abc::ConsolePrint("[AbcUi] native engineAnswers active=%d exclusive=%d "
                    "level=%d console=%d\n",
                    bridge.isGameUIActive() ? 1 : 0,
                    bridge.hasExclusiveInput() ? 1 : 0,
                    bridge.hasLevel() ? 1 : 0, bridge.consoleVisible() ? 1 : 0);
}

// Element value by id (window focus loss hides the page and clears
// the context focus, but the document keeps its value - this reads
// the evidence without depending on either).
void valueCommand() {
  Rml::Context *ctx = rmlui::context();
  const char *id = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv(1) : "";
  const char *docName =
      gEngfuncs.Cmd_Argc() > 2 ? gEngfuncs.Cmd_Argv(2) : "mainmenu";
  Rml::ElementDocument *doc = ctx ? ctx->GetDocument(docName) : nullptr;
  auto *control =
      doc ? dynamic_cast<Rml::ElementFormControl *>(doc->GetElementById(id))
          : nullptr;
  if (!control) {
    abc::ConsolePrint("[AbcUi] value id=%s doc=%s <none>\n", id, docName);
    return;
  }
  const Rml::String value = control->GetValue();
  abc::ConsolePrint("[AbcUi] value id=%s bytes=%zu value=%s composing=%d\n", id,
                    value.size(), value.c_str(),
                    rmlui::textComposing() ? 1 : 0);
}

// T21 fault-matrix instrument: list every VISIBLE element whose absolute
// box covers a screen point (x=arg1, y=arg2), across all context
// documents. Used to attribute an unexplained painted square to its
// owning document/element during the strict-takeover evidence runs.
void domAtCommand() {
  Rml::Context *ctx = rmlui::context();
  if (!ctx)
    return;
  const int px = gEngfuncs.Cmd_Argc() > 1 ? atoi(gEngfuncs.Cmd_Argv(1)) : -1;
  const int py = gEngfuncs.Cmd_Argc() > 2 ? atoi(gEngfuncs.Cmd_Argv(2)) : -1;
  abc::ConsolePrint("[AbcUi] dom-at point=(%d,%d) documents=%d\n", px, py,
                    ctx->GetNumDocuments());
  for (int i = 0; i < ctx->GetNumDocuments(); ++i) {
    Rml::ElementDocument *doc = ctx->GetDocument(i);
    if (!doc)
      continue;
    // Depth-first walk; every visible element whose border box
    // contains the point (or, with no point given, every element in
    // the top-right 200x200 corner) is printed with its geometry.
    std::vector<Rml::Element *> stack;
    stack.push_back(doc);
    while (!stack.empty()) {
      Rml::Element *el = stack.back();
      stack.pop_back();
      if (el->IsVisible()) {
        const Rml::Vector2f offset =
            el->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = el->GetBox().GetSize(Rml::BoxArea::Border);
        const bool contains = px >= 0 && py >= 0
                                  ? (offset.x <= px && px < offset.x + size.x &&
                                     offset.y <= py && py < offset.y + size.y)
                                  : (offset.x + size.x > 800 && offset.y < 200);
        if (contains) {
          abc::ConsolePrint("[AbcUi] dom-at doc='%s' tag='%s' id='%s' "
                            "abs=(%.1f,%.1f) size=%.1fx%.1f visible=1\n",
                            doc->GetId().c_str(), el->GetTagName().c_str(),
                            el->GetId().c_str(), offset.x, offset.y, size.x,
                            size.y);
        }
      }
      for (int c = 0; c < el->GetNumChildren(); ++c)
        stack.push_back(el->GetChild(c));
    }
  }
}

void inputRegressionCommand() {
  auto *ctx = rmlui::context();
  auto *panel = static_cast<panels::ScoreboardPanel *>(
      framework::PanelManager::find("scoreboard"));
  if (!ctx || !panel)
    return;
  int failures = 0;
  auto check = [&](bool ok, const char *label) {
    abc::ConsolePrint("[AbcUi] input-regression %s %s\n", ok ? "PASS" : "FAIL",
                      label);
    if (!ok)
      ++failures;
  };
  panel->reset();
  check(hooks::debugKeyEvent(1, K_MOUSE2) == 1 && !uiMouseOwner(),
        "hidden right passes");
  panels::scoreboard::setIntermission(true);
  check(hooks::debugKeyEvent(1, K_MOUSE2) == 0 && uiMouseOwner(),
        "first right enters");
  hooks::debugKeyEvent(0, K_MOUSE2);
  check(vgui2ext::isCursorVisible(), "native cursor visible");
  check(hooks::debugKeyEvent(1, K_MOUSE2) == 0 && uiMouseOwner(),
        "second right stays");
  hooks::debugKeyEvent(0, K_MOUSE2);
  check(hooks::debugKeyEvent(1, K_MOUSE1) == 0, "left attack consumed");
  hooks::debugKeyEvent(0, K_MOUSE1);
  panels::scoreboard::setIntermission(false);
  check(!uiMouseOwner() && !panels::scoreboard::wantsWheel(),
        "intermission hide releases");
  check(hooks::debugKeyEvent(1, K_MOUSE2) == 1, "closed right passes again");
  panels::scoreboard::setIntermission(true);
  hooks::debugKeyEvent(1, K_MOUSE2);
  hooks::debugKeyEvent(0, K_MOUSE2);
  panel->reset();
  check(!uiMouseOwner(), "map reset releases");
  abc::ConsolePrint(
      "[AbcUi] input-regression failures=%d (client seam, not hardware)\n",
      failures);
}

} // namespace

void closeProbeDocument() {
#ifndef NDEBUG
  if (ProbeDoc) {
    ProbeDoc->Close();
    ProbeDoc = nullptr;
  }
#endif
}

void registerProbeCommand() {
  abc::ADD_COMMAND("abc_ui_input_regression", inputRegressionCommand);
  abc::ADD_COMMAND("abc_ui_input_key", mouseKeyCommand);
  abc::ADD_COMMAND("abc_ui_input_text", inputTextCommand);
  abc::ADD_COMMAND("abc_ui_input_compose", inputComposeCommand);
  abc::ADD_COMMAND("abc_ui_input_cancel", inputCancelCommand);
  abc::ADD_COMMAND("abc_ui_input_events", inputEventsCommand);
  abc::ADD_COMMAND("abc_ui_value", valueCommand);
  abc::ADD_COMMAND("abc_ui_native", nativeCommand);
  abc::ADD_COMMAND("abc_ui_dom_at", domAtCommand);
  abc::ADD_COMMAND("abc_ui_probe", probeCommand);
  abc::ADD_COMMAND("abc_ui_menu", menuCommand);
  abc::ADD_COMMAND("abc_ui_feedback", feedbackCommand);
  abc::ADD_COMMAND("abc_ui_cfefx", cfefxCommand);
  abc::ADD_COMMAND("abc_ui_board", boardCommand);
  abc::ADD_COMMAND("abc_ui_perf", perfCommand);
  abc::ADD_COMMAND("abc_ui_pointer_on", pointerOnCommand);
  abc::ADD_COMMAND("abc_ui_pointer_off", pointerOffCommand);
  abc::ADD_COMMAND("abc_ui_wheelup", wheelUpCommand);
  abc::ADD_COMMAND("abc_ui_wheeldown", wheelDownCommand);
  // T15 console backend diagnostics (cache counters, dump, real submission
  // path, read-only completion).
  abc::ADD_COMMAND("abc_ui_console_status", consoleStatusCommand);
  abc::ADD_COMMAND("abc_ui_console_dump", consoleDumpCommand);
  abc::ADD_COMMAND("abc_ui_console_submit", consoleSubmitCommand);
  abc::ADD_COMMAND("abc_ui_console_complete", consoleCompleteCommand);
  abc::ADD_COMMAND("abc_ui_sprprobe", spriteProbeCommand);
}

} // namespace rmlui

#else // NDEBUG

namespace rmlui {

void closeProbeDocument() {
#ifndef NDEBUG
  if (ProbeDoc) {
    ProbeDoc->Close();
    ProbeDoc = nullptr;
  }
#endif
}

void registerProbeCommand() {}

} // namespace rmlui

#endif // NDEBUG
