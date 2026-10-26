// Mod resource loading extracted from GameUiModule.cpp (UTF-8).
#include "modules/gameui/settings/SettingsResources.h"
#include <abc/Paths.h>
#include <abc/Console.h>
#include <abc/Plugin.h>
#include <studio.h>
#include "rmlui/modelpreview/Options.h"
#include "rmlui/VGUI2ExtensionImport.h"
#include "services/settings/KeyBindModel.h"
#include "framework/FileSystemHandles.h"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <string_view>

namespace gameui::settings {
namespace {
constexpr std::size_t kMaxTextBytes = 4 * 1024 * 1024;

std::string readLocalText(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return {};
  const auto size = file.tellg();
  if (size <= 0 || size > static_cast<std::streamoff>(kMaxTextBytes))
    return {};
  std::string text(static_cast<std::size_t>(size), '\0');
  file.seekg(0);
  if (!file.read(text.data(), static_cast<std::streamsize>(text.size())))
    return {};
  return text;
}
} // namespace

std::string readModFile(const char *relativePath) {
  if (abc::g_pFileSystem) {
    const auto File =
        framework::openEngineFile(*abc::g_pFileSystem, relativePath, "rb");
    if (File) {
      const auto Size = abc::g_pFileSystem->Size(File.get());
      std::string Raw;
      if (Size > 0 && Size <= kMaxTextBytes) {
        Raw.resize(Size);
        if (abc::g_pFileSystem->Read(Raw.data(), Size, File.get()) !=
            static_cast<int>(Size))
          Raw.clear();
      }
      return services::settings::KeyBindModel::decodeResourceText(Raw.data(),
                                                                  Raw.size());
    }
  }
  const auto raw = readLocalText(abc::ModPath(relativePath));
  return services::settings::KeyBindModel::decodeResourceText(raw.data(),
                                                              raw.size());
}

namespace {
std::vector<std::string> scanMounted(const char *Directory, bool Directories) {
  std::vector<std::string> Names;
  if (abc::g_pFileSystem) {
    framework::EngineFileSearch search(*abc::g_pFileSystem);
    auto &Handle = search.handle;
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
  }
  std::sort(Names.begin(), Names.end());
  Names.erase(std::unique(Names.begin(), Names.end()), Names.end());
  if (!Directories && Names.size() > 512)
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
std::vector<services::settings::StringChoice> actionModels() {
  std::vector<services::settings::StringChoice> choices;
  if (!abc::g_pFileSystem)
    return choices;
  framework::EngineFileSearch search(*abc::g_pFileSystem);
  auto &handle = search.handle;
  const std::string mask = std::string(modelpreview::kActionModelDir) + "*.mdl";
  const char *name = abc::g_pFileSystem->FindFirst(mask.c_str(), &handle);
  while (name && choices.size() < 512) {
    const std::string path = std::string(modelpreview::kActionModelDir) + name;
    if (!abc::g_pFileSystem->FindIsDirectory(handle) &&
        modelpreview::Options::isModelPath(path)) {
      const std::string stem = std::filesystem::path(name).stem().string();
      choices.push_back({stem, stem});
    }
    name = abc::g_pFileSystem->FindNext(handle);
  }
  return choices;
}

std::vector<services::settings::StringChoice>
actionSequences(const std::string &path) {
  std::vector<services::settings::StringChoice> choices;
  if (!abc::g_pFileSystem || path.empty())
    return choices;
  const auto file =
      framework::openEngineFile(*abc::g_pFileSystem, path.c_str(), "rb");
  if (!file)
    return choices;
  studiohdr_t header{};
  const auto size = abc::g_pFileSystem->Size(file.get());
  const bool valid =
      abc::g_pFileSystem->Read(&header, sizeof(header), file.get()) ==
          sizeof(header) &&
      header.id == 0x54534449 && header.version == 10 &&
      header.length >= sizeof(header) &&
      static_cast<unsigned>(header.length) <= size && header.numseq > 0 &&
      header.numseq <= 512 && header.seqindex >= sizeof(header) &&
      static_cast<size_t>(header.seqindex) +
              static_cast<size_t>(header.numseq) * sizeof(mstudioseqdesc_t) <=
          static_cast<size_t>(header.length);
  if (valid) {
    abc::g_pFileSystem->Seek(file.get(), header.seqindex, FILESYSTEM_SEEK_HEAD);
    for (int i = 0; i < header.numseq; ++i) {
      mstudioseqdesc_t sequence{};
      if (abc::g_pFileSystem->Read(&sequence, sizeof(sequence), file.get()) !=
          sizeof(sequence)) {
        choices.clear();
        break;
      }
      std::string label(sequence.label,
                        strnlen(sequence.label, sizeof(sequence.label)));
      // Asset labels are untrusted display text, not markup or commands.
      std::replace_if(
          label.begin(), label.end(),
          [](unsigned char c) { return c < 32 || c == 127; }, ' ');
      const std::string value = std::to_string(i);
      choices.push_back({value, value + ": " + label});
    }
  }
  return choices;
}
} // namespace

void configureMenuActionChoices(services::settings::SettingsModel &model) {
  const auto action = model.find("abc_ui_menu_action_model");
  const auto sequence = model.find("abc_ui_menu_model_sequence");
  if (!action || !sequence || !action->row->StateName ||
      !sequence->row->StateName ||
      std::string_view(action->row->StateName) != "menu-action-models" ||
      std::string_view(sequence->row->StateName) != "menu-action-sequences")
    return;
  model.setStringChoices("abc_ui_menu_action_model", actionModels());
  model.setChoiceRefresh([source = std::string(), initialized = false](
                             services::settings::SettingsModel &rows) mutable {
    const auto action = rows.find("abc_ui_menu_action_model");
    const auto sequence = rows.find("abc_ui_menu_model_sequence");
    if (!action || !sequence || action->missing || sequence->missing)
      return;
    const std::string spelling =
        action->state == services::settings::RowState::kInvalid
            ? action->engineText
            : action->draftText;
    const std::string path = modelpreview::resolveActionModelPath(spelling, {});
    if (initialized && path == source)
      return;
    auto choices = actionSequences(path);
    const bool known = std::any_of(choices.begin(), choices.end(),
                                   [&sequence](const auto &choice) {
                                     return choice.Value == sequence->draftText;
                                   });
    rows.setStringChoices("abc_ui_menu_model_sequence", choices);
    // Repair only pending edits, including switching back to the live model
    // while a sequence draft still belongs to another model. Opening/cancelling
    // settings never silently changes the live value or creates a draft.
    const bool pending = spelling != action->engineText ||
                         sequence->draftText != sequence->engineText;
    if (!known && !choices.empty() && pending)
      rows.setDraftString("abc_ui_menu_model_sequence",
                          choices.front().Value.c_str());
    source = path;
    initialized = true;
  });
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
    if (const auto file = framework::openEngineFile(*abc::g_pFileSystem,
                                                    relativePath, "wb")) {
      const int written = abc::g_pFileSystem->Write(
          bytes.data(), static_cast<int>(bytes.size()), file.get());
      if (written == static_cast<int>(bytes.size()))
        return true;
      error = "short write";
      return false;
    }
  }
  const std::string absolute = abc::ModPath(relativePath);
  std::ofstream file(absolute, std::ios::binary);
  if (!file) {
    error = "cannot open " + absolute;
    return false;
  }
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  file.close();
  if (!file) {
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
    if (Language && *Language && std::string_view(Language) != "english") {
      std::string Path = abc::GameDir();
      Path += "_";
      Path += Language;
      Path += "/user.scr";
      Text = readLocalText(Path);
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
