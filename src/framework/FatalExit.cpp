// framework::FatalExit implementation - see FatalExit.h.
#include "FatalExit.h"

#include <abc/Console.h>
#include <abc/Plugin.h>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>

namespace framework {

namespace {

// Unbuffered Win32 append, independent of the engine filesystem: this runs on
// the way to a fatal exit, where the engine may already be tearing its own
// file system down. Mirrors the debug assert trace writer in plugins.cpp.
void appendExternalLog(const char *line) {
  std::error_code pathError;
  const auto directory = std::filesystem::temp_directory_path(pathError);
  if (pathError)
    return;
  HANDLE file =
      CreateFileA((directory / "abcui-fatal.log").string().c_str(),
                  FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;
  SYSTEMTIME time{};
  GetLocalTime(&time);
  char stamped[384];
  const int written = std::snprintf(
      stamped, sizeof(stamped), "[%04u-%02u-%02u %02u:%02u:%02u] %s\n",
      time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
      time.wSecond, line);
  if (written > 0) {
    DWORD ignored = 0;
    const auto count =
        (std::min)(static_cast<std::size_t>(written), sizeof(stamped) - 1);
    WriteFile(file, stamped, static_cast<DWORD>(count), &ignored, nullptr);
  }
  CloseHandle(file);
}

} // namespace

void fatalExit(const char *reason) {
  const char *text = reason ? reason : "(no reason)";
  // Console channel first (also reaches every host console mirror), then the
  // external file, then the engine fatal path. abc::SysError prefixes the
  // plugin name itself and does not return in the shipped host.
  abc::ConsoleError("[AbcUi] fatal: %s\n", text);
  appendExternalLog(text);
  abc::SysError("%s", text);
}

} // namespace framework
