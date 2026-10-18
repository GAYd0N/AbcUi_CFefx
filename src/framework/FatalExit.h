#pragma once

// framework::FatalExit - the strict-takeover core-unavailable exit path (T21).
//
// Contract (03 doc fault section, T21a step 2): when the takeover can no
// longer present ANY operable UI - Rml core/context unavailable, no font face
// loadable, the renderer missing, or the VGUI2Extension entry seam absent so
// the original GameUI can never be suppressed - the process must stop through
// the engine's own fatal path with an external diagnosis. It must never keep
// running: with the original UI still able to paint, a silent fallback would
// violate the strict takeover (D03), and a bare continue would leave a black
// locked screen.
//
// "External diagnosis" = console error (copied into the -condebug log and any
// host console mirror such as halflife-cli's sys_error capture) PLUS an
// unbuffered append to "<temp>/abcui-fatal.log" that survives the engine's
// own teardown (same rationale as the debug assert hook in plugins.cpp).
//
// The call never returns on the production path: abc::SysError routes through
// g_pMetaHookAPI->SysError into the engine's Sys_Error, which prints FATAL
// ERROR, notifies the host and exits. The return type is deliberately void -
// callers must not write code after it.

namespace framework {

// reason: plain ASCII description (no external input - call sites are all
// internal constant strings).
void fatalExit(const char *reason);

} // namespace framework
