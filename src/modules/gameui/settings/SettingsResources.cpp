// Mod resource loading extracted from GameUiModule.cpp (UTF-8).
#include "modules/gameui/settings/SettingsResources.h"
#include <abc/Paths.h>
#include <abc/Console.h>
#include <abc/Plugin.h>
#include "rmlui/VGUI2ExtensionImport.h"
#include "services/settings/KeyBindModel.h"
#include <io.h>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>
namespace gameui::settings {
std::string readModFile(const char *relativePath) {
  constexpr unsigned MaxBytes = 4 * 1024 * 1024;
  if (abc::g_pFileSystem) {
    const auto File = abc::g_pFileSystem->Open(relativePath, "rb");
    if (File) {
      const auto Size = abc::g_pFileSystem->Size(File);
      std::string Raw;
      if (Size > 0 && Size <= MaxBytes) {
        Raw.resize(Size);
        if (abc::g_pFileSystem->Read(Raw.data(), Size, File) !=
            static_cast<int>(Size))
          Raw.clear();
      }
      abc::g_pFileSystem->Close(File);
      return services::settings::KeyBindModel::decodeResourceText(Raw.data(),
                                                                  Raw.size());
    }
  }
  FILE *file = nullptr;
  if (fopen_s(&file, abc::ModPath(relativePath).c_str(), "rb") != 0 || !file)
    return {};
  std::string raw;
  char buffer[4096];
  size_t got = 0;
  while (raw.size() < MaxBytes &&
         (got = fread(buffer, 1, sizeof(buffer), file)) > 0)
    raw.append(buffer, got);
  fclose(file);
  return services::settings::KeyBindModel::decodeResourceText(raw.data(),
                                                              raw.size());
}

namespace {
std::vector<std::string> scanMounted(const char *Directory, bool Directories) {
  std::vector<std::string> Names;
  if (abc::g_pFileSystem) {
    FileFindHandle_t Handle = FILESYSTEM_INVALID_FIND_HANDLE;
    std::string Mask = std::string(Directory) + (Directories ? "/*" : "/*.bmp");
    const char *Name = abc::g_pFileSystem->FindFirst(Mask.c_str(), &Handle);
    while (Name) {
      std::string Entry = Name;
      if (!Entry.empty() && Entry[0] != '.' && Entry != "remapped.bmp" &&
          (!Directories || abc::g_pFileSystem->FindIsDirectory(Handle))) {
        if (!Directories)
          Entry = Entry.substr(0, Entry.find_last_of('.'));
        Names.push_back(std::move(Entry));
      }
      Name = abc::g_pFileSystem->FindNext(Handle);
    }
    if (Handle != FILESYSTEM_INVALID_FIND_HANDLE)
      abc::g_pFileSystem->FindClose(Handle);
  }
  std::sort(Names.begin(), Names.end());
  Names.erase(std::unique(Names.begin(), Names.end()), Names.end());
  if (Names.size() > 512)
    Names.resize(512);
  return Names;
}
} // namespace
std::vector<std::string> scanModDirectories(const char *Directory) {
  return scanMounted(Directory, true);
}
std::vector<std::string> scanModFileBasenames(const char *Directory) {
  return scanMounted(Directory, false);
}

namespace {

bool extensionMatches(const std::string &name, const char *const *Extensions,
                      int Count) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos)
    return false;
  std::string ext = name.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  for (int i = 0; i < Count; ++i) {
    std::string wanted = Extensions[i] ? Extensions[i] : "";
    std::transform(
        wanted.begin(), wanted.end(), wanted.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!wanted.empty() && ext == wanted)
      return true;
  }
  return false;
}

} // namespace

std::vector<services::settings::StringChoice>
scanModMediaFiles(const char *relativeDir, const char *const *extensions,
                  int extensionCount, const char *valuePrefix) {
  std::vector<services::settings::StringChoice> choices;
  if (!relativeDir || !*relativeDir || !valuePrefix)
    return choices;
  std::error_code error;
  const std::filesystem::path root = abc::ModPath(relativeDir);
  std::filesystem::directory_iterator itor(root, error);
  if (error)
    return choices;
  const std::string prefix = valuePrefix;
  for (const auto &entry : itor) {
    std::error_code typeError;
    if (!entry.is_regular_file(typeError) || typeError)
      continue;
    const std::string name = entry.path().filename().string();
    if (!extensionMatches(name, extensions, extensionCount))
      continue;
    services::settings::StringChoice choice;
    const size_t dot = name.find_last_of('.');
    choice.Label = dot == std::string::npos ? name : name.substr(0, dot);
    choice.Value = prefix + name;
    choices.push_back(std::move(choice));
  }
  std::sort(choices.begin(), choices.end(),
            [](const services::settings::StringChoice &A,
               const services::settings::StringChoice &B) {
              return A.Value < B.Value;
            });
  choices.erase(std::unique(choices.begin(), choices.end(),
                            [](const services::settings::StringChoice &A,
                               const services::settings::StringChoice &B) {
                              return A.Value == B.Value;
                            }),
                choices.end());
  if (choices.size() > 512)
    choices.resize(512);
  return choices;
}

bool writeModFile(const char *relativePath,
                  const std::vector<unsigned char> &bytes, std::string &error) {
  error.clear();
  if (bytes.empty()) {
    error = "nothing to write";
    return false;
  }
  // The engine filesystem first: it writes beside the mod's own files (the
  // same place the engine reads tempdecal.wad from).
  if (abc::g_pFileSystem) {
    if (auto *file = abc::g_pFileSystem->Open(relativePath, "wb")) {
      const int written = abc::g_pFileSystem->Write(
          bytes.data(), static_cast<int>(bytes.size()), file);
      abc::g_pFileSystem->Close(file);
      if (written == static_cast<int>(bytes.size()))
        return true;
      error = "short write";
      return false;
    }
  }
  const std::string absolute = abc::ModPath(relativePath);
  FILE *file = nullptr;
  if (fopen_s(&file, absolute.c_str(), "wb") != 0 || !file) {
    error = "cannot open " + absolute;
    return false;
  }
  const std::size_t written = fwrite(bytes.data(), 1, bytes.size(), file);
  fclose(file);
  if (written != bytes.size()) {
    error = "short write to " + absolute;
    return false;
  }
  return true;
}

services::settings::SettingsModel::UserScrTable loadAdvancedScript() {
  services::settings::SettingsModel::UserScrTable Table;
  // Localized variant first (svencoop_schinese ships translated prompts):
  // the Path is CWD-relative like abc::ModPath - the engine mounts the
  // Language directory next to the mod directory.
  std::string Text;
  {
    const char *Language = vgui2ext::currentLanguage();
    if (Language && *Language && std::strcmp(Language, "english") != 0) {
      std::string Path = abc::GameDir();
      Path += "_";
      Path += Language;
      Path += "/user.scr";
      FILE *File = nullptr;
      if (fopen_s(&File, Path.c_str(), "rb") == 0 && File) {
        char Buffer[4096];
        size_t Got = 0;
        while ((Got = fread(Buffer, 1, sizeof(Buffer), File)) > 0)
          Text.append(Buffer, Got);
        fclose(File);
      }
    }
  }
  if (Text.empty())
    Text = readModFile("user.scr");
  // Empty/invalid scripts leave the JSON page's fallback items intact.
  const auto Decoded = services::settings::KeyBindModel::decodeResourceText(
      Text.data(), Text.size());
  services::settings::SettingsModel::parseUserScr(Decoded.c_str(), Table);
  return Table;
}
} // namespace gameui::settings
