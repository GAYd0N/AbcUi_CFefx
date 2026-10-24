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

// Moves an already-subscribed bridge to the head of the engine's command
// chain and remembers the displaced handler as its upstream link. Returns 1
// when the bridge is at the head, 0 when there is no bridge/engine callback
// or the bridge is currently dispatching. The outermost link publishes once;
// when no sink consumed the command it forwards to the displaced handler.
// Main thread only.
int32_t ensureFirst(const char *name);

} // namespace commands

namespace commands {
// Main-thread interaction state; does not depend on a concrete panel header.
bool textMenuOpen();
void setTextMenuOpen(bool open);
} // namespace commands
