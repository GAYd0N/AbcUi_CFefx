// framework::commands implementation - see Commands.h.
#include "Commands.h"

#include <functional>
#include <map>
#include <string>
#include <utility>

#include <metahook.h>

#include "../modsys/EventBus.h"

namespace commands {

namespace {

// MetaHook lookup is case-sensitive, while console execution is not. Retain
// exact engine spelling for HookCmd, but share one ASCII-folded event channel.
std::string commandKey(const char *name) {
  std::string key;
  if (!name)
    return key;
  for (const auto *p = reinterpret_cast<const unsigned char *>(name); *p; ++p) {
    // One token, never a command line. Other punctuation/non-ASCII names are
    // accepted when they exist in the engine catalog.
    if (*p <= ' ' || *p == 127 || *p == ';' || *p == '"')
      return {};
    key += static_cast<char>(*p >= 'A' && *p <= 'Z' ? *p + ('a' - 'A') : *p);
  }
  return key;
}

void commandThunk();

struct Entry {
  std::string name;
  std::string channel;
  xcommand_t original = nullptr;
  // The handler that was displaced from the head of the engine chain by
  // ensureFirst(). The outermost link forwards here when no sink consumed the
  // command; the nested (older) link always terminates at original.
  xcommand_t upstream = nullptr;
  int depth = 0;
};

struct Registry {
  ~Registry() {
    if (!g_pMetaHookAPI || !g_pMetaHookAPI->FindCmd || !g_pMetaHookAPI->HookCmd)
      return;
    for (const auto &[key, entry] : entries) {
      const auto *command = g_pMetaHookAPI->FindCmd(entry.name.c_str());
      // Never overwrite a later plugin's hook.
      if (command && command->function == &commandThunk)
        g_pMetaHookAPI->HookCmd(entry.name.c_str(), entry.original);
    }
  }
  std::map<std::string, Entry, std::less<>> entries;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

void commandThunk() {
  if (!gEngfuncs.Cmd_Argv)
    return;
  const auto key = commandKey(gEngfuncs.Cmd_Argv(0));
  auto &entries = registry().entries;
  const auto it = entries.find(key);
  if (it == entries.end())
    return;
  Entry &entry = it->second;
  // A bridge promoted by ensureFirst() leaves its old link in the chain
  // below; only the outermost link may publish. The nested link forwards
  // straight to the chain tail so a chained plugin cannot dispatch twice.
  ++entry.depth;
  struct DepthGuard {
    int &depth;
    ~DepthGuard() { --depth; }
  } guard{entry.depth};
  if (entry.depth > 1) {
    if (entry.original)
      entry.original();
    return;
  }
  // Process-owned entries stay valid when a module callback hooks another
  // command. Snapshot the chain before invoking any module code.
  const xcommand_t original = entry.original;
  const xcommand_t upstream = entry.upstream;
  const bool consumed = framework::eventBus().publishVetoAggregateRaw(
      entry.channel.c_str(), nullptr);
  if (consumed)
    return;
  if (upstream)
    upstream();
  else if (original)
    original();
}

cmd_function_t *findEngineCommand(const char *name, const std::string &key) {
  if (auto *command = g_pMetaHookAPI->FindCmd(name))
    return command;
  if (auto *command = g_pMetaHookAPI->FindCmd(key.c_str()))
    return command;
  if (!gEngfuncs.GetFirstCmdFunctionHandle ||
      !gEngfuncs.GetNextCmdFunctionHandle || !gEngfuncs.GetCmdFunctionName)
    return nullptr;
  for (auto handle = gEngfuncs.GetFirstCmdFunctionHandle(); handle;
       handle = gEngfuncs.GetNextCmdFunctionHandle(handle)) {
    const char *candidate = gEngfuncs.GetCmdFunctionName(handle);
    if (candidate && commandKey(candidate) == key)
      return g_pMetaHookAPI->FindCmd(candidate);
  }
  return nullptr;
}

} // namespace

AbcUiSubscription subscribe(const char *name, int priority, AbcUiEventVeto sink,
                            void *userdata) {
  if (!sink || !framework::module_resources::current() ||
      !framework::module_resources::accepting() || !g_pMetaHookAPI ||
      !g_pMetaHookAPI->FindCmd || !g_pMetaHookAPI->HookCmd ||
      !gEngfuncs.Cmd_Argv)
    return 0;
  const auto key = commandKey(name);
  if (key.empty())
    return 0;
  auto &entries = registry().entries;
  auto it = entries.find(key);
  if (it == entries.end()) {
    const auto *command = findEngineCommand(name, key);
    // A null callback is special engine forwarding, not a callable original.
    // Do not replace that execution path with an inert command.
    if (!command || !command->function || command->function == &commandThunk)
      return 0;
    Entry entry{command->name, "command." + key, command->function};
    it = entries.emplace(key, std::move(entry)).first;
    const xcommand_t previous =
        g_pMetaHookAPI->HookCmd(it->second.name.c_str(), &commandThunk);
    if (!previous) {
      entries.erase(it);
      return 0;
    }
    it->second.original = previous;
  }
  return framework::eventBus().subscribeVetoAggregate(
      it->second.channel.c_str(), priority, sink, userdata);
}

int32_t ensureFirst(const char *name) {
  if (!g_pMetaHookAPI || !g_pMetaHookAPI->FindCmd ||
      !g_pMetaHookAPI->HookCmd)
    return 0;
  const auto key = commandKey(name);
  if (key.empty())
    return 0;
  auto &entries = registry().entries;
  const auto it = entries.find(key);
  if (it == entries.end())
    return 0;
  Entry &entry = it->second;
  // Re-hooking from inside a dispatch would move the frame that is running.
  if (entry.depth > 0)
    return 0;
  const auto *command = g_pMetaHookAPI->FindCmd(entry.name.c_str());
  if (!command || !command->function)
    return 0;
  if (command->function == &commandThunk)
    return 1;
  const xcommand_t previous =
      g_pMetaHookAPI->HookCmd(entry.name.c_str(), &commandThunk);
  if (!previous || previous == &commandThunk)
    return 0;
  entry.upstream = previous;
  return 1;
}

namespace {
bool TextMenuOpen = false;
}
bool textMenuOpen() {
  return TextMenuOpen;
}
void setTextMenuOpen(bool open) {
  TextMenuOpen = open;
}
} // namespace commands
