// framework::ApiTable implementation - see ApiTable.h.
#include "ApiTable.h"
#include "ApiTextures.h"
#include "ApiAudio.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include "../data/NetworkMessages.h"
#include "../framework/Commands.h"
#include "../render/RadarRender.h"
#include "../framework/ResourceFile.h"
#include "EventBus.h"
#include "FrameworkVersion.h"
#include "ModuleManager.h"
#include "ModulePanel.h"
#include "ModuleVfs.h"
#include "ModuleServices.h"

// The SDK's AbcUiFileView typedef sits at global scope (module_api.h), so its
// definition must too; it owns one package entry view.
struct AbcUiFileView {
  framework::PackageEntry entry;
  framework::module_resources::Registration lifetime;
};

namespace framework {
namespace {

// Parses "major.minor.patch" at compile time into ABCUI_VERSION_PACK form.
constexpr uint32_t packedVersion(const char *version) {
  uint32_t parts[3] = {0, 0, 0};
  int index = 0;
  for (const char *p = version; *p; ++p) {
    if (*p == '.') {
      if (++index > 2)
        break;
      continue;
    }
    if (*p < '0' || *p > '9')
      break;
    parts[index] = parts[index] * 10 + static_cast<uint32_t>(*p - '0');
  }
  return (parts[0] << 16) | (parts[1] << 8) | parts[2];
}

// ---------------------------------------------------------------- core ----

void apiPrint(const char *format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
  va_end(args);
  abc::ConsolePrint("[module] %s\n", buffer);
}

void apiWarn(const char *format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
  va_end(args);
  abc::ConsoleWarn("[module] %s\n", buffer);
}

void apiError(const char *format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
  va_end(args);
  abc::ConsoleError("[module] %s\n", buffer);
}

int32_t apiFailInitialization(const char *reason) {
  return module_resources::failInitialization(reason) ? 1 : 0;
}

const AbcUiCoreApi kCoreApi = {
    sizeof(AbcUiCoreApi),
    packedVersion(kFrameworkVersion),
    kFrameworkVersion,
    apiPrint,
    apiWarn,
    apiError,
    apiFailInitialization,
    module_services::registerService,
    module_services::unregisterService,
    module_services::available,
    module_services::call,
};

// ------------------------------------------------------------- console ----

AbcUiCvarHandle apiRegisterCvar(const char *name, const char *value,
                                int flags) {
  if (!name || !value || !module_resources::accepting())
    return nullptr;
  return static_cast<AbcUiCvarHandle>(
      abc::CREATE_CVAR(name, value, flags, nullptr));
}

float apiCvarValue(AbcUiCvarHandle cvar, float fallback) {
  const auto *c = static_cast<cvar_t *>(cvar);
  return c ? c->value : fallback;
}

void apiCvarString(AbcUiCvarHandle cvar, char *out, uint32_t outSize,
                   const char *fallback) {
  if (!out || outSize == 0)
    return;
  const auto *c = static_cast<cvar_t *>(cvar);
  const char *text = (c && c->string) ? c->string : fallback;
  if (!text)
    text = "";
  size_t i = 0;
  for (; i + 1 < outSize && text[i]; ++i)
    out[i] = text[i];
  out[i] = '\0';
}

struct ModuleCommand {
  AbcUiCommandFn fn = nullptr;
  void *userdata = nullptr;
  module_resources::Registration lifetime;
};

std::map<std::string, ModuleCommand> &moduleCommands() {
  static std::map<std::string, ModuleCommand> instance;
  return instance;
}

// One engine command per registered name; the engine calls back without a
// userdata slot, so the thunk recovers the name through argv(0).
void moduleCommandThunk() {
  const char *name = gEngfuncs.Cmd_Argv(0);
  const auto &table = moduleCommands();
  const auto it = table.find(name ? name : "");
  if (it != table.end() && it->second.fn) {
    const auto owner = it->second.lifetime.owner();
    const auto fn = it->second.fn;
    void *userdata = it->second.userdata;
    module_resources::invoke(owner, "console command", fn, userdata);
  }
}

void apiRegisterCommand(const char *name, AbcUiCommandFn fn, void *userdata) {
  if (!name || !fn || !module_resources::accepting())
    return;
  auto &table = moduleCommands();
  auto [it, fresh] = table.try_emplace(name);
  if (it->second.fn &&
      it->second.lifetime.owner() != module_resources::current()) {
    abc::ConsoleWarn("[AbcUi] command \"%s\" belongs to another module\n",
                     name);
    return;
  }
  it->second = ModuleCommand{fn, userdata};
  it->second.lifetime.track([key = it->first] {
    auto &record = moduleCommands().at(key);
    record.fn = nullptr;
    record.userdata = nullptr;
    record.lifetime.forget();
  });
  if (fresh)
    abc::ADD_COMMAND(it->first.c_str(), &moduleCommandThunk);
}

void apiEngineCommand(const char *command) {
  if (command && *command)
    abc::EngineClientCmd(command);
}

void apiServerCommand(const char *command) {
  if (command && *command)
    abc::ServerCmd(command);
}

module_resources::Registration &voteGateLifetime() {
  static module_resources::Registration lifetime;
  return lifetime;
}

void apiSetVoteGate(AbcUiCvarHandle cvar) {
  if (!module_resources::accepting())
    return;
  auto &lifetime = voteGateLifetime();
  if (cvar && lifetime.tracked() &&
      lifetime.owner() != module_resources::current()) {
    abc::ConsoleWarn("[AbcUi] vote gate belongs to another module\n");
    return;
  }
  lifetime.forget();
  lifetime = module_resources::Registration{};
  netmsg::setVoteGate(static_cast<cvar_t *>(cvar));
  if (cvar)
    lifetime.track([] {
      netmsg::setVoteGate(nullptr);
      voteGateLifetime().forget();
    });
}

// Engine cvar callbacks receive only the cvar pointer; the module's
// userdata hangs off this table (registered before the cvar can be set).
struct CvarWatch {
  AbcUiCommandFn fn = nullptr;
  void *userdata = nullptr;
  module_resources::Registration lifetime;
};

std::map<cvar_t *, CvarWatch> &cvarWatches() {
  static std::map<cvar_t *, CvarWatch> instance;
  return instance;
}

void cvarWatchThunk(cvar_t *cvar) {
  const auto &table = cvarWatches();
  const auto it = table.find(cvar);
  if (it != table.end() && it->second.fn) {
    const auto owner = it->second.lifetime.owner();
    const auto fn = it->second.fn;
    void *userdata = it->second.userdata;
    module_resources::invoke(owner, "cvar watch", fn, userdata);
  }
}

AbcUiCvarHandle apiRegisterCvarWatch(const char *name, const char *value,
                                     int flags, AbcUiCommandFn fn,
                                     void *userdata) {
  if (!module_resources::accepting())
    return nullptr;
  if (!name || !value || !fn)
    return apiRegisterCvar(name, value, flags);
  if (auto *existing = abc::CVAR_GET_POINTER(name)) {
    const auto it = cvarWatches().find(existing);
    if (it != cvarWatches().end() &&
        it->second.lifetime.owner() != module_resources::current()) {
      abc::ConsoleWarn("[AbcUi] cvar watch \"%s\" belongs to another module\n",
                       name);
      return nullptr;
    }
  }
  cvar_t *cvar = abc::CREATE_CVAR(name, value, flags, &cvarWatchThunk);
  if (!cvar)
    return nullptr;
  cvarWatches()[cvar] = CvarWatch{fn, userdata};
  cvarWatches()[cvar].lifetime.track([cvar] { cvarWatches().erase(cvar); });
  return static_cast<AbcUiCvarHandle>(cvar);
}

void apiPlaySound(const char *path, float volume) {
  if (path && *path)
    gEngfuncs.pfnPlaySoundByName(path, volume);
}

AbcUiCvarHandle apiFindCvar(const char *name) {
  return (name && *name)
             ? static_cast<AbcUiCvarHandle>(abc::CVAR_GET_POINTER(name))
             : nullptr;
}

void apiSetCvarValue(AbcUiCvarHandle cvar, float value) {
  auto *target = static_cast<cvar_t *>(cvar);
  if (target)
    target->value = value;
}

int32_t apiKeyBinding(const char *name, char *out, uint32_t outSize) {
  const char *binding =
      (name && *name) ? gEngfuncs.Key_LookupBinding(name) : nullptr;
  const std::string text = binding ? binding : "";
  if (out && outSize > 0) {
    uint32_t i = 0;
    for (; i + 1 < outSize && i < text.size(); ++i)
      out[i] = text[i];
    out[i] = 0;
  }
  return static_cast<int32_t>(text.size());
}

// Keep one initializer per member, in declaration order: a missing entry is
// not a compile error (remaining pointers are zero-initialized) and only
// shows up as a null call at runtime.
const AbcUiConsoleApi kConsoleApi = {
    sizeof(AbcUiConsoleApi), apiRegisterCvar,  apiCvarValue,     apiCvarString,
    apiRegisterCommand,      apiEngineCommand, apiServerCommand, apiSetVoteGate,
    apiRegisterCvarWatch,    apiPlaySound,     apiFindCvar,      apiKeyBinding,
    apiSetCvarValue,
};

// -------------------------------------------------------------- events ----

AbcUiSubscription apiSubscribe(const char *channel, int priority,
                               AbcUiEventSink sink, void *userdata) {
  return eventBus().subscribe(channel, priority, sink, userdata);
}

AbcUiSubscription apiSubscribeVeto(const char *channel, int priority,
                                   AbcUiEventVeto sink, void *userdata) {
  return eventBus().subscribeVeto(channel, priority, sink, userdata);
}

AbcUiSubscription apiSubscribeVetoAggregate(const char *channel, int priority,
                                            AbcUiEventVeto sink,
                                            void *userdata) {
  return eventBus().subscribeVetoAggregate(channel, priority, sink, userdata);
}

void apiUnsubscribe(AbcUiSubscription subscription) {
  eventBus().unsubscribe(subscription);
}

const AbcUiEventsApi kEventsApi = {
    sizeof(AbcUiEventsApi),    apiSubscribe,
    apiSubscribeVeto,          apiUnsubscribe,
    apiSubscribeVetoAggregate, module_services::publish,
};

// ----------------------------------------------------------------- vfs ----

std::map<AbcUiFileView *, std::unique_ptr<AbcUiFileView>> &fileViews() {
  static std::map<AbcUiFileView *, std::unique_ptr<AbcUiFileView>> views;
  return views;
}

void removeView(AbcUiFileView *view) {
  fileViews().erase(view);
}

bool ownsView(AbcUiFileView *view) {
  const auto it = fileViews().find(view);
  return it != fileViews().end() &&
         it->second->lifetime.owner() == module_resources::current();
}
void apiViewFree(AbcUiFileView *view) {
  if (ownsView(view))
    removeView(view);
}

int32_t apiVfsOpen(const char *uri, AbcUiFileView **out) {
  if (out)
    *out = nullptr;
  if (!uri || !out || !ModuleVfs::isModuleUri(uri))
    return ABCUI_ERROR_INVALID;
  const auto owner = module_resources::current();
  if (!owner || !module_resources::accepting() ||
      !ModuleVfs::canRead(owner->id, uri))
    return ABCUI_ERROR_FORBIDDEN;
  auto view = std::make_unique<AbcUiFileView>();
  if (!ModuleVfs::open(uri, view->entry, nullptr))
    return ABCUI_ERROR_NOT_FOUND;
  if (view->entry.size > UINT32_MAX)
    return ABCUI_ERROR_LIMIT;
  AbcUiFileView *raw = view.get();
  fileViews().emplace(raw, std::move(view));
  raw->lifetime.track([raw] { removeView(raw); });
  *out = raw;
  return ABCUI_OK;
}

const void *apiViewData(AbcUiFileView *view) {
  return ownsView(view) ? view->entry.data : nullptr;
}

uint32_t apiViewSize(AbcUiFileView *view) {
  return ownsView(view) ? static_cast<uint32_t>(view->entry.size) : 0;
}

int32_t apiVfsEnumerate(const char *directory, AbcUiFileSink sink,
                        void *userdata) {
  const auto owner = module_resources::current();
  if (!directory || !sink)
    return ABCUI_ERROR_INVALID;
  std::string probe = std::string(directory) + "/_";
  if (!owner || !ModuleVfs::canRead(owner->id, probe))
    return ABCUI_ERROR_FORBIDDEN;
  const auto files = ModuleVfs::list(directory);
  for (const auto &uri : files)
    module_resources::invoke(owner, "VFS enumeration", sink, userdata,
                             uri.c_str());
  return ABCUI_OK;
}

int32_t apiVfsResolve(const char *base, const char *path, char *out,
                      uint32_t outSize) {
  std::string uri;
  if (!path || !ModuleVfs::resolve(base ? base : "", path, uri))
    return ABCUI_ERROR_INVALID;
  const auto owner = module_resources::current();
  if (!owner || !ModuleVfs::canRead(owner->id, uri))
    return ABCUI_ERROR_FORBIDDEN;
  if (!out || outSize <= uri.size())
    return ABCUI_ERROR_LIMIT;
  std::memcpy(out, uri.c_str(), uri.size() + 1);
  return ABCUI_OK;
}

int apiReadEngineFile(const char *path, void *out, uint32_t outSize,
                      uint32_t maxBytes) {
  if (!path || !*path || std::string_view(path).starts_with("abc/abcui/"))
    return -1;
  std::vector<std::byte> bytes;
  if (!ResourceFile::read(path, bytes, maxBytes ? maxBytes : 65536u))
    return -1;
  if (out && outSize > 0) {
    const size_t copy =
        bytes.size() < outSize ? bytes.size() : static_cast<size_t>(outSize);
    if (copy)
      std::memcpy(out, bytes.data(), copy);
  }
  return static_cast<int>(bytes.size());
}

const AbcUiVfsApi kVfsApi = {
    sizeof(AbcUiVfsApi), apiVfsOpen,      apiViewData,   apiViewSize,
    apiViewFree,         apiVfsEnumerate, apiVfsResolve, apiReadEngineFile,
};

// -------------------------------------------------------------- panels ----

int apiRegisterPanel(const AbcUiPanelDesc *desc) {
  if (!desc || desc->structSize < sizeof(AbcUiPanelDesc) || !desc->name ||
      !desc->documentPath)
    return 0;
  return modmanager::acceptPanel(*desc) ? 1 : 0;
}

const AbcUiPanelsApi kPanelsApi = {
    sizeof(AbcUiPanelsApi),
    apiRegisterPanel,
};

// ---------------------------------------------------------------- radar ----

int32_t apiRadarReady() {
  return radarrender::ready() ? 1 : 0;
}

int32_t apiRadarGeneration() {
  return radarrender::generation();
}

void apiRadarSetActive(int32_t active) {
  radarrender::setActive(active != 0);
}

int32_t apiRadarSetMask(AbcUiTexture texture) {
  const int id = TexturesApi::textureId(texture);
  if (texture && !id)
    return ABCUI_ERROR_INVALID;
  radarrender::setMask(id);
  return ABCUI_OK;
}

void apiRadarSetZoom(float zoom) {
  radarrender::setZoom(zoom);
}

const AbcUiRadarApi kRadarApi = {
    sizeof(AbcUiRadarApi), apiRadarReady,   apiRadarGeneration,
    apiRadarSetActive,     apiRadarSetMask, apiRadarSetZoom,
};

// ------------------------------------------------------------- commands ----

AbcUiSubscription apiSubscribeCommand(const char *name, int priority,
                                      AbcUiEventVeto sink, void *userdata) {
  return ::commands::subscribe(name, priority, sink, userdata);
}

int apiTextMenuOpen() {
  return ::commands::textMenuOpen() ? 1 : 0;
}

void apiSetTextMenuOpen(int open) {
  ::commands::setTextMenuOpen(open != 0);
}

int32_t apiEnsureFirstCommand(const char *name) {
  return ::commands::ensureFirst(name);
}

const AbcUiCommandsApi kCommandsApi = {
    sizeof(AbcUiCommandsApi),
    apiSubscribeCommand,
    apiTextMenuOpen,
    apiSetTextMenuOpen,
    apiEnsureFirstCommand,
};

} // namespace

namespace ApiTable {

namespace {

// Every section except core is {uint32_t structSize; pointers...} on the
// x86 target (4-byte pointers, no padding), so a missing initializer shows up
// as a null pointer. That is not a compile error - catch it when the table is
// first handed to a module.
template <typename Table> bool allMembersSet(const Table &table) {
  constexpr size_t kPointers =
      (sizeof(Table) - sizeof(uint32_t)) / sizeof(void *);
  const void *const *members = reinterpret_cast<const void *const *>(&table);
  for (size_t i = 1; i <= kPointers; ++i) {
    if (!members[i])
      return false;
  }
  return true;
}

// The table aggregate itself needs a by-name audit: appending a section to the
// SDK header does not force appending its initializer here, C++ zero-fills the
// omission and every module then receives a silently null section. The
// per-section check below cannot see that, so check the wiring explicitly.
bool tableComplete(const AbcUiModuleApi &api) {
  return api.core && api.console && api.events && api.vfs && api.panels &&
         api.data && api.ui && api.commands && api.weapons && api.players &&
         api.radar && api.shell && api.voice && api.score && api.input &&
         api.documents && api.menu && api.votemenu && api.playerlist &&
         api.commandmenu && api.menuconfig && api.social && api.inventory &&
         api.creategame && api.consolepage && api.serverbrowser &&
         api.connection && api.settings && api.textures && api.audio;
}

void verifySections() {
  static bool checked = false;
  if (checked)
    return;
  checked = true;
  // Every section, wherever its implementation lives: the append-only rule
  // has already left three initializers short (console, data, players) after
  // later commits appended members. Such omissions only surface as null calls
  // at runtime, so audit every section here and report at boot.
  const bool complete =
      kCoreApi.failInitialization && allMembersSet(kConsoleApi) &&
      allMembersSet(kEventsApi) && allMembersSet(kVfsApi) &&
      allMembersSet(kPanelsApi) && allMembersSet(kCommandsApi) &&
      allMembersSet(kRadarApi) && allMembersSet(*DataApi::get()) &&
      allMembersSet(*UiApi::get()) && allMembersSet(*WeaponsApi::get()) &&
      allMembersSet(*PlayersApi::get()) && allMembersSet(*ScoreApi::get()) &&
      allMembersSet(*InputApi::get()) && allMembersSet(*ShellApi::get()) &&
      allMembersSet(*VoiceApi::get()) && allMembersSet(*DocumentsApi::get()) &&
      allMembersSet(*MenuApi::get()) && allMembersSet(*VoteMenuApi::get()) &&
      allMembersSet(*PlayerListApi::get()) &&
      allMembersSet(*CommandMenuApi::get()) &&
      allMembersSet(*MenuConfigApi::get()) &&
      allMembersSet(*SocialApi::get()) && allMembersSet(*InventoryApi::get()) &&
      allMembersSet(*CreateGameApi::get()) &&
      allMembersSet(*ConsolePageApi::get()) &&
      allMembersSet(*ServerBrowserApi::get()) &&
      allMembersSet(*ConnectionApi::get()) &&
      allMembersSet(*SettingsApi::get()) &&
      allMembersSet(*TexturesApi::get()) && allMembersSet(*AudioApi::get());
  if (!complete) {
    abc::ConsoleError("[AbcUi] module api table has null section members");
  }
}

} // namespace

const AbcUiModuleApi *get() {
  verifySections();
  static const AbcUiModuleApi kApi = {
      sizeof(AbcUiModuleApi),
      ABCUI_ABI_VERSION,
      &kCoreApi,
      &kConsoleApi,
      &kEventsApi,
      &kVfsApi,
      &kPanelsApi,
      DataApi::get(),
      UiApi::get(),
      &kCommandsApi,
      WeaponsApi::get(),
      PlayersApi::get(),
      &kRadarApi,
      ShellApi::get(),
      VoiceApi::get(),
      ScoreApi::get(),
      InputApi::get(),
      DocumentsApi::get(),
      MenuApi::get(),
      VoteMenuApi::get(),
      PlayerListApi::get(),
      CommandMenuApi::get(),
      MenuConfigApi::get(),
      SocialApi::get(),
      InventoryApi::get(),
      CreateGameApi::get(),
      ConsolePageApi::get(),
      ServerBrowserApi::get(),
      ConnectionApi::get(),
      SettingsApi::get(),
      TexturesApi::get(),
      AudioApi::get(),
  };
  static const bool complete = tableComplete(kApi);
  if (!complete)
    abc::ConsoleError("[AbcUi] module api table is incomplete (null section)");
  return &kApi;
}

} // namespace ApiTable

} // namespace framework
