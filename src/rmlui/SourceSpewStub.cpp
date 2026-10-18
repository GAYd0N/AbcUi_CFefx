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

void _SpewInfo(SpewType_t type, const tchar *pFile, int line) {
  (void)type;
  (void)pFile;
  (void)line;
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

SpewRetval_t _DSpewMessage(const tchar *pGroupName, int level,
                           const tchar *pMsg, ...) {
  (void)pGroupName;
  (void)level;
  (void)pMsg;
  return SPEW_CONTINUE;
}

SpewRetval_t ColorSpewMessage(SpewType_t type, const Color *pColor,
                              const tchar *pMsg, ...) {
  (void)type;
  (void)pColor;
  (void)pMsg;
  return SPEW_CONTINUE;
}

void _ExitOnFatalAssert(const tchar *pFile, int line) {
  abc::SysError("[SourceAssert] fatal assert at %s:%d", pFile, line);
}

bool ShouldUseNewAssertDialog() {
  return false;
}

bool DoNewAssertDialog(const tchar *pFile, int line, const tchar *pExpression) {
  (void)pFile;
  (void)line;
  (void)pExpression;
  return false;
}

#endif //_DEBUG
