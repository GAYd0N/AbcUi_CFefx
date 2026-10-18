#pragma once

// data::textfeed - TextMsg router: splits the vanilla text stream into the
// killfeed and the notice channels. Ported from the g_EventTextMsg handler
// in ABCEnchance src/hud/Viewport.cpp (batch-A half; ScoreInfo-style targets
// chain through untouched).
//
// Routing (old precedence, first match consumes the message):
//   1. target 1 (PRINTNOTIFY) kill patterns:
//      " committed suicide." / " was killed by a " / " : X : " -> KillFeed
//   2. target 1 or 4 (PRINTCENTER): localized "#token" resolution +
//      printf-style parameter substitution -> Notice
// Everything else chains to the original TextMsg handler.

#include <abc/Events.h>

namespace textfeed {

// TextMsg target values (old HUDNOTICE enum) - also the NoticePanel variant
// keys (PanelManager wires one panel per target).
constexpr int kPrintNotify = 1;
constexpr int kPrintCenter = 4;

// (victim, attacker, inflictor); attacker empty on suicide.
extern abc::CallbackList<void(const char *, const char *, const char *)>
    KillFeed;
// (target, formatted text) for HUDNOTICE targets 1/4.
extern abc::CallbackList<void(int, const char *)> Notice;

// HUD_Init-time: subscribes netmsg::TextMsg. Idempotent.
void init();

} // namespace textfeed
