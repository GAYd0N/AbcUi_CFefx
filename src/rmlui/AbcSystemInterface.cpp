// rmlui::AbcSystemInterface / AbcFileInterface implementation.
// Clipboard UTF-8 <-> UTF-16 conversion follows the Win32 backend pattern
// (RmlUi/Backends/RmlUi_Platform_Win32.cpp).

#include "AbcSystemInterface.h"

#include <abc/Console.h>
#include <abc/Plugin.h>

#include "TextInput.h"
#include "framework/ResourceFile.h"
#include "framework/DocumentLoadGuard.h"
#include "framework/Localization.h"
#include "modsys/ModuleVfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>

#include <IFileSystem.h>
#include <Windows.h>

namespace rmlui {

double AbcSystemInterface::GetElapsedTime() {
  static LARGE_INTEGER frequency = {};
  static bool initialized = false;
  if (!initialized) {
    QueryPerformanceFrequency(&frequency);
    initialized = true;
  }
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  return static_cast<double>(counter.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

int AbcSystemInterface::TranslateString(Rml::String &translated,
                                        const Rml::String &input) {
  // Localization tokens ("#token") resolve through the framework table;
  // non-tokens pass through untouched (returns -1 per Rml contract).
  if (!input.empty() && input[0] == '#') {
    if (framework::Localization::translate(translated, input.substr(1)))
      return 0;
  }
  translated = input;
  return -1;
}

bool AbcSystemInterface::LogMessage(Rml::Log::Type type,
                                    const Rml::String &message) {
  // T21: feed the document-load failure detector (see DocumentLoadGuard.h).
  framework::documentload::noteRmlLog(static_cast<int>(type), message.c_str());
  switch (type) {
  case Rml::Log::LT_ERROR:
  case Rml::Log::LT_ASSERT:
    abc::ConsoleError("[RmlUi] %s\n", message.c_str());
    break;
  case Rml::Log::LT_WARNING:
    abc::ConsoleWarn("[RmlUi] %s\n", message.c_str());
    break;
  default:
    abc::ConsolePrint("[RmlUi] %s\n", message.c_str());
    break;
  }
  return true;
}

void AbcSystemInterface::SetClipboardText(const Rml::String &text) {
  const int wideLength = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (wideLength <= 0 && !text.empty())
    return;
  std::wstring wide(static_cast<size_t>(wideLength), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      wide.data(), wideLength);

  if (!OpenClipboard(nullptr))
    return;
  EmptyClipboard();
  const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (void *data = GlobalLock(handle)) {
      memcpy(data, wide.c_str(), bytes);
      GlobalUnlock(handle);
      if (!SetClipboardData(CF_UNICODETEXT, handle))
        GlobalFree(handle);
    } else {
      GlobalFree(handle);
    }
  }
  CloseClipboard();
}

void AbcSystemInterface::GetClipboardText(Rml::String &text) {
  text.clear();
  if (!OpenClipboard(nullptr))
    return;
  if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
    if (const wchar_t *wide =
            static_cast<const wchar_t *>(GlobalLock(handle))) {
      const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                            nullptr, nullptr);
      if (bytes > 0) {
        // The conversion writes the terminating NUL too, so size the
        // buffer for it first and trim afterwards.
        text.resize(static_cast<size_t>(bytes));
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, text.data(), bytes, nullptr,
                            nullptr);
        text.resize(static_cast<size_t>(bytes - 1));
      }
      GlobalUnlock(handle);
    }
  }
  CloseClipboard();
}

// IME placement delegates to TextInput (the HWND resolution and the imm32
// calls live next to the composition state machine). Pin backend reference:
// Backends/RmlUi_Platform_Win32.cpp SystemInterface_Win32::ActivateKeyboard.
void AbcSystemInterface::ActivateKeyboard(Rml::Vector2f caretPosition,
                                          float lineHeight) {
  updateImePlacement(caretPosition, lineHeight);
}

// Widget blur: the composition was already cancelled through the handler's
// OnDeactivate; there is no platform state worth resetting here.
void AbcSystemInterface::DeactivateKeyboard() {}

namespace {

struct ResourceHandle {
  FileHandle_t EngineFile = nullptr;
  FILE *LocalFile = nullptr;
  // mod:// entries: a view into the mounted package. ModuleEntry keeps the
  // (possibly extracted) bytes alive for the handle's lifetime.
  const unsigned char *ModuleData = nullptr;
  size_t ModuleSize = 0;
  size_t ModulePos = 0;
  framework::PackageEntry ModuleEntry;
};

ResourceHandle *handleFor(Rml::FileHandle handle) {
  return reinterpret_cast<ResourceHandle *>(handle);
}

} // namespace

Rml::FileHandle AbcFileInterface::Open(const Rml::String &path) {
  // engine://<engine path> lets a package document reference a framework
  // shared asset (the base stylesheet, shared lang tables) by its absolute
  // engine path, which relative mod:// links cannot reach.
  Rml::String resolved = path;
  if (resolved.rfind("engine://", 0) == 0)
    resolved = resolved.substr(9);

  // Module packages: "mod://<module-id>/<path>" resolves through the module
  // VFS (zero-copy into the mapped zip for STORED entries). Rml keeps the
  // scheme when joining relative document references, so package-internal
  // links and images land here too.
  if (framework::ModuleVfs::isModuleUri(resolved)) {
    framework::PackageEntry entry;
    if (!framework::ModuleVfs::open(resolved.c_str(), entry, nullptr))
      return 0;
    auto *resource = new (std::nothrow) ResourceHandle{};
    if (!resource)
      return 0;
    resource->ModuleData = static_cast<const unsigned char *>(entry.data);
    resource->ModuleSize = entry.size;
    resource->ModulePos = 0;
    resource->ModuleEntry = std::move(entry);
    return reinterpret_cast<Rml::FileHandle>(resource);
  }

  // Rml documents and their resources follow the engine mount order, which
  // is the same IFileSystem VGUI2Extension uses. This also keeps paths such
  // as "abc/abcui/..." independent of the process CWD.
  const bool absolute = (resolved.size() >= 2 && resolved[1] == ':') ||
                        (!resolved.empty() &&
                         (resolved.front() == '/' || resolved.front() == '\\'));
  if (absolute) {
    FILE *local = nullptr;
    if (fopen_s(&local, resolved.c_str(), "rb") != 0 || !local)
      return 0;
    auto *resource = new (std::nothrow) ResourceHandle{nullptr, local};
    if (!resource) {
      fclose(local);
      return 0;
    }
    return reinterpret_cast<Rml::FileHandle>(resource);
  }

  std::string normalized;
  if (!abc::g_pFileSystem ||
      !framework::ResourceFile::normalize(resolved, normalized))
    return 0;
  FileHandle_t file = abc::g_pFileSystem->Open(normalized.c_str(), "rb");
  if (!file)
    return 0;
  auto *resource = new (std::nothrow) ResourceHandle{file, nullptr};
  if (!resource) {
    abc::g_pFileSystem->Close(file);
    return 0;
  }
  return reinterpret_cast<Rml::FileHandle>(resource);
}

void AbcFileInterface::Close(Rml::FileHandle file) {
  ResourceHandle *resource = handleFor(file);
  if (!resource)
    return;
  if (resource->LocalFile)
    fclose(resource->LocalFile);
  else if (abc::g_pFileSystem && resource->EngineFile)
    abc::g_pFileSystem->Close(resource->EngineFile);
  delete resource;
}

size_t AbcFileInterface::Read(void *buffer, size_t size, Rml::FileHandle file) {
  ResourceHandle *resource = handleFor(file);
  if (!resource || !buffer)
    return 0;
  if (resource->ModuleData) {
    if (resource->ModulePos >= resource->ModuleSize)
      return 0;
    const size_t remaining = resource->ModuleSize - resource->ModulePos;
    const size_t count = (std::min)(size, remaining);
    std::memcpy(buffer, resource->ModuleData + resource->ModulePos, count);
    resource->ModulePos += count;
    return count;
  }
  if (resource->LocalFile)
    return fread(buffer, 1, size, resource->LocalFile);
  if (!abc::g_pFileSystem || !resource->EngineFile)
    return 0;
  size_t total = 0;
  while (total < size) {
    const size_t remaining = size - total;
    const int chunk = static_cast<int>(
        (std::min)(remaining,
                   static_cast<size_t>((std::numeric_limits<int>::max)())));
    const int got =
        abc::g_pFileSystem->Read(static_cast<unsigned char *>(buffer) + total,
                                 chunk, resource->EngineFile);
    if (got <= 0)
      break;
    total += static_cast<size_t>(got);
  }
  return total;
}

bool AbcFileInterface::Seek(Rml::FileHandle file, long offset, int origin) {
  ResourceHandle *resource = handleFor(file);
  if (!resource)
    return false;
  if (resource->ModuleData) {
    long long base = 0;
    switch (origin) {
    case SEEK_SET:
      base = 0;
      break;
    case SEEK_CUR:
      base = static_cast<long long>(resource->ModulePos);
      break;
    case SEEK_END:
      base = static_cast<long long>(resource->ModuleSize);
      break;
    default:
      return false;
    }
    const long long target = base + offset;
    if (target < 0 || target > static_cast<long long>(resource->ModuleSize))
      return false;
    resource->ModulePos = static_cast<size_t>(target);
    return true;
  }
  if (resource->LocalFile)
    return _fseeki64(resource->LocalFile, offset, origin) == 0;
  if (!abc::g_pFileSystem || !resource->EngineFile)
    return false;
  if (offset < (std::numeric_limits<int>::min)() ||
      offset > (std::numeric_limits<int>::max)())
    return false;
  FileSystemSeek_t seek;
  switch (origin) {
  case SEEK_SET:
    seek = FILESYSTEM_SEEK_HEAD;
    break;
  case SEEK_CUR:
    seek = FILESYSTEM_SEEK_CURRENT;
    break;
  case SEEK_END:
    seek = FILESYSTEM_SEEK_TAIL;
    break;
  default:
    return false;
  }
  abc::g_pFileSystem->Seek(resource->EngineFile, static_cast<int>(offset),
                           seek);
  return true;
}

size_t AbcFileInterface::Tell(Rml::FileHandle file) {
  ResourceHandle *resource = handleFor(file);
  if (!resource)
    return 0;
  if (resource->ModuleData)
    return resource->ModulePos;
  if (resource->LocalFile)
    return static_cast<size_t>(_ftelli64(resource->LocalFile));
  if (!abc::g_pFileSystem || !resource->EngineFile)
    return 0;
  return abc::g_pFileSystem->Tell(resource->EngineFile);
}

} // namespace rmlui
