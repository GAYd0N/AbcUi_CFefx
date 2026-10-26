#pragma once

// Process-owned engine command bridges, installed on explicit module request.
// Module subscriptions and callback ownership live in EventBus. Bridges remain
// inert pass-throughs after their handlers are revoked, so other plugins may
// safely retain a bridge thunk as their predecessor.

#include <abcui/module_api.h>

namespace commands {

// Main-thread, module-owned subscription to any existing callback-backed
// engine command. No name whitelist, fixed slot count or init-only restriction.
// Returns zero on refusal. Revoke through events->unsubscribe.
AbcUiSubscription subscribe(const char *name, int priority, AbcUiEventVeto sink,
                            void *userdata);

// abc_ui_mediaplayer diagnostics for one bridge: whether it exists, whether
// the engine table entry still points at it, and its dispatch counters
// (last: 1 consumed, 0 chained to the original, -1 never ran).
bool debugBridge(const char *name, unsigned &dispatches, unsigned &consumed,
                 int &last, bool &engineHeadOurs);

} // namespace commands

namespace commands {
// Main-thread interaction state; does not depend on a concrete panel header.
bool textMenuOpen();
void setTextMenuOpen(bool open);
} // namespace commands
