// framework::ApiTable implementation - see ApiTable.h.
#include "ApiTable.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
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

// The SDK's AbcUiFileView typedef sits at global scope (module_api.h), so its
// definition must too; it owns one package entry view.
struct AbcUiFileView {
  framework::PackageEntry entry;
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

const AbcUiCoreApi kCoreApi = {
    sizeof(AbcUiCoreApi),
    packedVersion(kFrameworkVersion),
    kFrameworkVersion,
    apiPrint,
    apiWarn,
    apiError,
};

// ------------------------------------------------------------- console ----

AbcUiCvarHandle apiRegisterCvar(const char *name, const char *value,
                                int flags) {
  if (!name || !value)
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
  if (it != table.end() && it->second.fn)
    it->second.fn(it->second.userdata);
}

void apiRegisterCommand(const char *name, AbcUiCommandFn fn, void *userdata) {
  if (!name || !fn)
    return;
  auto &table = moduleCommands();
  const bool fresh = table.find(name) == table.end();
  table[name] = ModuleCommand{fn, userdata};
  if (fresh)
    abc::ADD_COMMAND(name, &moduleCommandThunk);
}

void apiEngineCommand(const char *command) {
  if (command && *command)
    abc::EngineClientCmd(command);
}

void apiServerCommand(const char *command) {
  if (command && *command)
    abc::ServerCmd(command);
}

void apiSetVoteGate(AbcUiCvarHandle cvar) {
  netmsg::setVoteGate(static_cast<cvar_t *>(cvar));
}

// Engine cvar callbacks receive only the cvar pointer; the module's
// userdata hangs off this table (registered before the cvar can be set).
struct CvarWatch {
  AbcUiCommandFn fn = nullptr;
  void *userdata = nullptr;
};

std::map<cvar_t *, CvarWatch> &cvarWatches() {
  static std::map<cvar_t *, CvarWatch> instance;
  return instance;
}

void cvarWatchThunk(cvar_t *cvar) {
  const auto &table = cvarWatches();
  const auto it = table.find(cvar);
  if (it != table.end() && it->second.fn)
    it->second.fn(it->second.userdata);
}

AbcUiCvarHandle apiRegisterCvarWatch(const char *name, const char *value,
                                     int flags, AbcUiCommandFn fn,
                                     void *userdata) {
  if (!name || !value || !fn)
    return apiRegisterCvar(name, value, flags);
  cvar_t *cvar = abc::CREATE_CVAR(name, value, flags, &cvarWatchThunk);
  if (!cvar)
    return nullptr;
  cvarWatches()[cvar] = CvarWatch{fn, userdata};
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
  return eventBus().subscribe(channel, priority,
                              reinterpret_cast<EventSinkFn>(sink), userdata);
}

AbcUiSubscription apiSubscribeVeto(const char *channel, int priority,
                                   AbcUiEventVeto sink, void *userdata) {
  return eventBus().subscribeVeto(
      channel, priority, reinterpret_cast<EventVetoFn>(sink), userdata);
}

AbcUiSubscription apiSubscribeVetoAggregate(const char *channel, int priority,
                                            AbcUiEventVeto sink,
                                            void *userdata) {
  return eventBus().subscribeVetoAggregate(
      channel, priority, reinterpret_cast<EventVetoFn>(sink), userdata);
}

void apiUnsubscribe(AbcUiSubscription subscription) {
  eventBus().unsubscribe(subscription);
}

const AbcUiEventsApi kEventsApi = {
    sizeof(AbcUiEventsApi),    apiSubscribe, apiSubscribeVeto, apiUnsubscribe,
    apiSubscribeVetoAggregate,
};

// ----------------------------------------------------------------- vfs ----

AbcUiFileView *apiVfsOpen(const char *moduleId, const char *path) {
  auto *view = new (std::nothrow) AbcUiFileView();
  if (!view)
    return nullptr;
  if (!ModuleVfs::openPath(moduleId, path, view->entry, nullptr)) {
    delete view;
    return nullptr;
  }
  return view;
}

const void *apiViewData(AbcUiFileView *view) {
  return view ? view->entry.data : nullptr;
}

uint32_t apiViewSize(AbcUiFileView *view) {
  return view ? static_cast<uint32_t>(view->entry.size) : 0;
}

void apiViewFree(AbcUiFileView *view) {
  delete view;
}

int apiVfsExists(const char *moduleId, const char *path) {
  PackageEntry entry;
  return ModuleVfs::openPath(moduleId, path, entry, nullptr) ? 1 : 0;
}

int apiReadEngineFile(const char *path, void *out, uint32_t outSize,
                      uint32_t maxBytes) {
  if (!path || !*path)
    return -1;
  std::vector<unsigned char> bytes;
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
    sizeof(AbcUiVfsApi), apiVfsOpen,   apiViewData,       apiViewSize,
    apiViewFree,         apiVfsExists, apiReadEngineFile,
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

void apiRadarSetMaskRound(int32_t round) {
  radarrender::setMaskRound(round != 0);
}

void apiRadarSetZoom(float zoom) {
  radarrender::setZoom(zoom);
}

const AbcUiRadarApi kRadarApi = {
    sizeof(AbcUiRadarApi), apiRadarReady,        apiRadarGeneration,
    apiRadarSetActive,     apiRadarSetMaskRound, apiRadarSetZoom,
};

// ------------------------------------------------------------- commands ----

AbcUiSubscription apiSubscribeCommand(const char *name, int priority,
                                      AbcUiEventVeto sink, void *userdata) {
  if (!name)
    return 0;
  // The chained-command bus bridges every hooked command onto an aggregate
  // veto channel ("command.<name>"); see framework/Commands.cpp.
  const std::string channel = std::string("command.") + name;
  return eventBus().subscribeVetoAggregate(
      channel.c_str(), priority, reinterpret_cast<EventVetoFn>(sink), userdata);
}

int apiTextMenuOpen() {
  return ::commands::textMenuOpen() ? 1 : 0;
}

void apiSetTextMenuOpen(int open) {
  ::commands::setTextMenuOpen(open != 0);
}

const AbcUiCommandsApi kCommandsApi = {
    sizeof(AbcUiCommandsApi),
    apiSubscribeCommand,
    apiTextMenuOpen,
    apiSetTextMenuOpen,
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
         api.connection && api.settings;
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
      allMembersSet(kConsoleApi) && allMembersSet(kEventsApi) &&
      allMembersSet(kVfsApi) && allMembersSet(kPanelsApi) &&
      allMembersSet(kCommandsApi) && allMembersSet(kRadarApi) &&
      allMembersSet(*DataApi::get()) && allMembersSet(*UiApi::get()) &&
      allMembersSet(*WeaponsApi::get()) && allMembersSet(*PlayersApi::get()) &&
      allMembersSet(*ScoreApi::get()) && allMembersSet(*InputApi::get()) &&
      allMembersSet(*ShellApi::get()) && allMembersSet(*VoiceApi::get()) &&
      allMembersSet(*DocumentsApi::get()) && allMembersSet(*MenuApi::get()) &&
      allMembersSet(*VoteMenuApi::get()) &&
      allMembersSet(*PlayerListApi::get()) &&
      allMembersSet(*CommandMenuApi::get()) &&
      allMembersSet(*MenuConfigApi::get()) &&
      allMembersSet(*SocialApi::get()) && allMembersSet(*InventoryApi::get()) &&
      allMembersSet(*CreateGameApi::get()) &&
      allMembersSet(*ConsolePageApi::get()) &&
      allMembersSet(*ServerBrowserApi::get()) &&
      allMembersSet(*ConnectionApi::get()) &&
      allMembersSet(*SettingsApi::get());
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
  };
  static const bool complete = tableComplete(kApi);
  if (!complete)
    abc::ConsoleError("[AbcUi] module api table is incomplete (null section)");
  return &kApi;
}

} // namespace ApiTable

} // namespace framework
