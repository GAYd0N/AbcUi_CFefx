// framework::MemoryDll implementation - see MemoryDll.h.
#include "MemoryDll.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include <Windows.h>

namespace framework {
namespace {

constexpr size_t kMaxImageBytes = 256u * 1024 * 1024;
constexpr size_t kMaxImportModules = 64;

void fail(std::string *error, const char *message) {
  if (error)
    *error = message;
}

bool within(size_t offset, size_t bytes, size_t size) {
  return offset <= size && bytes <= size - offset;
}

const IMAGE_SECTION_HEADER *sectionTable(const IMAGE_NT_HEADERS32 *nt) {
  return reinterpret_cast<const IMAGE_SECTION_HEADER *>(
      reinterpret_cast<const uint8_t *>(nt) +
      offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
      nt->FileHeader.SizeOfOptionalHeader);
}

DWORD sectionProtection(DWORD characteristics) {
  if (characteristics & IMAGE_SCN_MEM_EXECUTE)
    return (characteristics & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE
                                                   : PAGE_EXECUTE_READ;
  if (characteristics & IMAGE_SCN_MEM_READ)
    return (characteristics & IMAGE_SCN_MEM_WRITE) ? PAGE_READWRITE
                                                   : PAGE_READONLY;
  return PAGE_NOACCESS;
}

// Applies per-section page protections over the committed image.
bool protectSections(uint8_t *image, const IMAGE_NT_HEADERS32 *nt,
                     std::string *error) {
  const IMAGE_SECTION_HEADER *sections = sectionTable(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const IMAGE_SECTION_HEADER &section = sections[i];
    const size_t bytes =
        (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
    if (bytes == 0)
      continue;
    DWORD oldProtect = 0;
    if (!VirtualProtect(image + section.VirtualAddress, bytes,
                        sectionProtection(section.Characteristics),
                        &oldProtect)) {
      fail(error, "image section protection failed");
      return false;
    }
  }
  return true;
}

bool applyRelocations(uint8_t *image, const IMAGE_NT_HEADERS32 *nt,
                      ptrdiff_t delta, std::string *error) {
  if (delta == 0)
    return true;
  const IMAGE_DATA_DIRECTORY &directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  if (directory.VirtualAddress == 0 || directory.Size == 0)
    return true;

  size_t offset = 0;
  while (offset < directory.Size) {
    const IMAGE_BASE_RELOCATION *block =
        reinterpret_cast<const IMAGE_BASE_RELOCATION *>(
            image + directory.VirtualAddress + offset);
    if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) {
      fail(error, "image relocation block is truncated");
      return false;
    }
    const size_t entryCount =
        (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
    const WORD *entries = reinterpret_cast<const WORD *>(block + 1);
    for (size_t i = 0; i < entryCount; ++i) {
      const WORD type = entries[i] >> 12;
      const WORD offsetInPage = entries[i] & 0x0FFFu;
      uint32_t *patch = reinterpret_cast<uint32_t *>(
          image + block->VirtualAddress + offsetInPage);
      switch (type) {
      case IMAGE_REL_BASED_ABSOLUTE:
        break;
      case IMAGE_REL_BASED_HIGHLOW:
        *patch = static_cast<uint32_t>(static_cast<uint64_t>(*patch) + delta);
        break;
      default:
        fail(error, "image relocation type is not supported");
        return false;
      }
    }
    offset += block->SizeOfBlock;
  }
  return true;
}

bool bindImports(uint8_t *image, const IMAGE_NT_HEADERS32 *nt,
                 std::vector<void *> &imports, std::string *error) {
  const IMAGE_DATA_DIRECTORY &directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (directory.VirtualAddress == 0 || directory.Size == 0)
    return true;

  // Reserve before acquiring any library reference: recording a successful
  // LoadLibrary must not allocate and leak that reference on failure.
  imports.reserve(kMaxImportModules);
  const IMAGE_IMPORT_DESCRIPTOR *descriptor =
      reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(
          image + directory.VirtualAddress);
  for (; descriptor->Name != 0; ++descriptor) {
    if (imports.size() >= kMaxImportModules) {
      fail(error, "image imports too many modules");
      return false;
    }
    const char *moduleName =
        reinterpret_cast<const char *>(image + descriptor->Name);
    HMODULE module = LoadLibraryA(moduleName);
    if (!module) {
      fail(error, "image import module cannot be loaded");
      return false;
    }
    imports.push_back(module);

    // The IAT (FirstThunk) is what gets patched; the names come from the
    // INT (OriginalFirstThunk) when present.
    IMAGE_THUNK_DATA32 *iat =
        reinterpret_cast<IMAGE_THUNK_DATA32 *>(image + descriptor->FirstThunk);
    const IMAGE_THUNK_DATA32 *names =
        descriptor->OriginalFirstThunk != 0
            ? reinterpret_cast<const IMAGE_THUNK_DATA32 *>(
                  image + descriptor->OriginalFirstThunk)
            : iat;
    for (size_t i = 0; names[i].u1.AddressOfData != 0; ++i) {
      void *function = nullptr;
      if (names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG32) {
        function = GetProcAddress(
            module, reinterpret_cast<LPCSTR>(names[i].u1.Ordinal & 0xFFFFu));
      } else {
        const IMAGE_IMPORT_BY_NAME *byName =
            reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(
                image + names[i].u1.AddressOfData);
        function =
            GetProcAddress(module, reinterpret_cast<LPCSTR>(byName->Name));
      }
      if (!function) {
        fail(error, "image import symbol cannot be resolved");
        return false;
      }
      iat[i].u1.Function = reinterpret_cast<DWORD>(function);
    }
  }
  return true;
}

} // namespace

MemoryDll::~MemoryDll() {
  unload();
}

bool MemoryDll::load(const void *data, size_t size, std::string *error) {
  if (Image || !data || size < sizeof(IMAGE_DOS_HEADER)) {
    fail(error, "image load called on an active loader or a truncated image");
    return false;
  }
  const auto *dos = static_cast<const IMAGE_DOS_HEADER *>(data);
  const auto *source = static_cast<const uint8_t *>(data);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
      !within(static_cast<size_t>(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS32),
              size)) {
    fail(error, "image is not a PE executable");
    return false;
  }
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32 *>(source + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    fail(error, "image is not a PE executable");
    return false;
  }
  if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
      (nt->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0 ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    fail(error, "image is not a Win32 DLL");
    return false;
  }
  if (nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
      nt->OptionalHeader.NumberOfRvaAndSizes <
          IMAGE_NUMBEROF_DIRECTORY_ENTRIES) {
    fail(error, "image data directories are truncated");
    return false;
  }
  const IMAGE_DATA_DIRECTORY *directories = nt->OptionalHeader.DataDirectory;
  if (directories[IMAGE_DIRECTORY_ENTRY_TLS].Size != 0) {
    fail(error, "image uses static TLS, which is not supported");
    return false;
  }
  if (directories[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size != 0) {
    fail(error, "image uses delay-loaded imports, which are not supported");
    return false;
  }
  if (directories[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].Size != 0) {
    fail(error, "image is a .NET assembly");
    return false;
  }
  const size_t sizeOfHeaders = nt->OptionalHeader.SizeOfHeaders;
  if (nt->OptionalHeader.SizeOfImage == 0 ||
      nt->OptionalHeader.SizeOfImage > kMaxImageBytes || sizeOfHeaders == 0 ||
      sizeOfHeaders > nt->OptionalHeader.SizeOfImage || sizeOfHeaders > size) {
    fail(error, "image sizes are out of bounds");
    return false;
  }

  const size_t sectionOffset = static_cast<size_t>(dos->e_lfanew) +
                               offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
                               nt->FileHeader.SizeOfOptionalHeader;
  if (!within(sectionOffset,
              static_cast<size_t>(nt->FileHeader.NumberOfSections) *
                  sizeof(IMAGE_SECTION_HEADER),
              sizeOfHeaders)) {
    fail(error, "image section table is out of bounds");
    return false;
  }
  const IMAGE_SECTION_HEADER *sections = sectionTable(nt);
  const DWORD entryRva = nt->OptionalHeader.AddressOfEntryPoint;
  bool entryExecutable = entryRva == 0;
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const IMAGE_SECTION_HEADER &section = sections[i];
    const size_t bytes =
        (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
    if (!within(section.VirtualAddress, bytes,
                nt->OptionalHeader.SizeOfImage) ||
        (section.SizeOfRawData != 0 &&
         !within(section.PointerToRawData, section.SizeOfRawData, size))) {
      fail(error, "image section data is out of bounds");
      return false;
    }
    if (entryRva >= section.VirtualAddress &&
        entryRva - section.VirtualAddress < bytes &&
        (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
      entryExecutable = true;
  }
  if (!entryExecutable) {
    fail(error, "image entry point is outside executable sections");
    return false;
  }
  for (size_t i = 0; i < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; ++i) {
    // The certificate directory uses a file offset, not an image RVA.
    if (i == IMAGE_DIRECTORY_ENTRY_SECURITY)
      continue;
    const IMAGE_DATA_DIRECTORY &directory = directories[i];
    if (directory.Size != 0 &&
        (directory.VirtualAddress == 0 ||
         !within(directory.VirtualAddress, directory.Size,
                 nt->OptionalHeader.SizeOfImage))) {
      fail(error, "image data directory is out of bounds");
      return false;
    }
  }

  uint8_t *image = static_cast<uint8_t *>(
      VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
                   MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  if (!image) {
    fail(error, "image allocation failed");
    return false;
  }
  // Partial loads release imports and memory without calling module code.
  // AttachedEntry is armed only after a successful process attach.
  Image = image;
  ImageSize = nt->OptionalHeader.SizeOfImage;

  std::memcpy(image, source, (std::min)(sizeOfHeaders, size));
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const IMAGE_SECTION_HEADER &section = sections[i];
    if (section.SizeOfRawData == 0)
      continue;
    std::memcpy(image + section.VirtualAddress,
                source + section.PointerToRawData, section.SizeOfRawData);
  }

  const ptrdiff_t delta =
      image - reinterpret_cast<const uint8_t *>(
                  static_cast<uintptr_t>(nt->OptionalHeader.ImageBase));
  if (!applyRelocations(image, nt, delta, error) ||
      !bindImports(image, nt, Imports, error) ||
      !protectSections(image, nt, error)) {
    unload();
    return false;
  }
  if (!FlushInstructionCache(GetCurrentProcess(), image, ImageSize)) {
    fail(error, "image instruction cache flush failed");
    unload();
    return false;
  }

  if (entryRva != 0) {
    const auto entryPoint =
        reinterpret_cast<BOOL(WINAPI *)(HINSTANCE, DWORD, LPVOID)>(image +
                                                                   entryRva);
    if (!entryPoint(reinterpret_cast<HINSTANCE>(image), DLL_PROCESS_ATTACH,
                    nullptr)) {
      fail(error, "image DllMain(DLL_PROCESS_ATTACH) refused the load");
      unload();
      return false;
    }
    AttachedEntry = reinterpret_cast<void *>(entryPoint);
  }
  return true;
}

void MemoryDll::unload() {
  uint8_t *image = static_cast<uint8_t *>(Image);
  if (!image)
    return;
  Image = nullptr;
  ImageSize = 0;
  void *attachedEntry = AttachedEntry;
  AttachedEntry = nullptr;

  // Never run detach for an image whose imports/protection/attach failed.
  if (attachedEntry) {
    const auto entryPoint =
        reinterpret_cast<BOOL(WINAPI *)(HINSTANCE, DWORD, LPVOID)>(
            attachedEntry);
    entryPoint(reinterpret_cast<HINSTANCE>(image), DLL_PROCESS_DETACH, nullptr);
  }
  for (auto it = Imports.rbegin(); it != Imports.rend(); ++it)
    FreeLibrary(static_cast<HMODULE>(*it));
  Imports.clear();
  VirtualFree(image, 0, MEM_RELEASE);
}

void *MemoryDll::getExport(const char *name) const {
  if (!Image || !name)
    return nullptr;
  const auto *image = static_cast<const uint8_t *>(Image);
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32 *>(image + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY &directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (directory.VirtualAddress == 0 || directory.Size == 0)
    return nullptr;
  const auto *exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY *>(
      image + directory.VirtualAddress);
  const auto *names =
      reinterpret_cast<const DWORD *>(image + exports->AddressOfNames);
  for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
    const char *candidate = reinterpret_cast<const char *>(image + names[i]);
    if (std::string_view(candidate) != name)
      continue;
    const auto *ordinals =
        reinterpret_cast<const WORD *>(image + exports->AddressOfNameOrdinals);
    const DWORD ordinal = ordinals[i];
    if (ordinal >= exports->NumberOfFunctions)
      return nullptr;
    const auto *functions =
        reinterpret_cast<const DWORD *>(image + exports->AddressOfFunctions);
    const DWORD functionRva = functions[ordinal];
    // A forwarded export points back into the export directory; the
    // framework never forwards, so refuse rather than return a string.
    if (functionRva >= directory.VirtualAddress &&
        functionRva < directory.VirtualAddress + directory.Size)
      return nullptr;
    return const_cast<uint8_t *>(image) + functionRva;
  }
  return nullptr;
}

} // namespace framework
