// Debug-build stubs for the SourceSDK tier0 spew symbols.
//
// The VGUI2Extension interface headers pull in SourceSDK mathlib/vector2d.h,
// whose inline ctors call Assert(). tier0/dbgflag.h enables DBGFLAG_ASSERT
// under _DEBUG, and the plugin links no tier0 implementation - so Debug
// needs these spew symbols. Routing spew to the engine console (instead of
// the default Win32 assert dialog) keeps Debug builds usable for automated
// acceptance runs. Recoverable spew continues; fatal assert calls SysError.
//
// Release builds never reference these (DBGFLAG_ASSERT stays off there).
#ifdef _DEBUG

#include <metahook.h>

#include <SourceSDK/tier0/dbg.h>

#include <abc/Console.h>
#include <abc/Plugin.h>

#include <cstdarg>
#include <cstdio>

void _SpewInfo([[maybe_unused]] SpewType_t type,
               [[maybe_unused]] const tchar *pFile, [[maybe_unused]] int line) {
}

SpewRetval_t _SpewMessage(const tchar *pMsg, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, pMsg);
  vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, pMsg, args);
  va_end(args);
  abc::ConsoleError("[SourceAssert] %s\n", buffer);
  return SPEW_CONTINUE;
}

SpewRetval_t _DSpewMessage([[maybe_unused]] const tchar *pGroupName,
                           [[maybe_unused]] int level,
                           [[maybe_unused]] const tchar *pMsg, ...) {
  return SPEW_CONTINUE;
}

SpewRetval_t ColorSpewMessage([[maybe_unused]] SpewType_t type,
                              [[maybe_unused]] const Color *pColor,
                              [[maybe_unused]] const tchar *pMsg, ...) {
  return SPEW_CONTINUE;
}

void _ExitOnFatalAssert(const tchar *pFile, int line) {
  abc::SysError("[SourceAssert] fatal assert at %s:%d", pFile, line);
}

bool ShouldUseNewAssertDialog() {
  return false;
}

bool DoNewAssertDialog([[maybe_unused]] const tchar *pFile,
                       [[maybe_unused]] int line,
                       [[maybe_unused]] const tchar *pExpression) {
  return false;
}

#endif //_DEBUG
