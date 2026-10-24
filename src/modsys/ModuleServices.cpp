#include "ModuleServices.h"

#include <abcui/events.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>

#include "EventBus.h"
#include "ModuleResources.h"

namespace framework::module_services {
namespace {

constexpr uint32_t kMaxBuffer = 1024 * 1024;

bool validName(const char *name) {
  if (!name || !*name)
    return false;
  size_t size = 0;
  for (; *name && size <= 64; ++name, ++size)
    if (!(*name >= 'a' && *name <= 'z') && !(*name >= '0' && *name <= '9') &&
        *name != '_')
      return false;
  return size <= 64;
}

struct Service {
  std::string name;
  uint32_t version;
  AbcUiServiceHandler handler;
  void *userdata;
  module_resources::Registration lifetime;
};

struct Registry {
  uint64_t next = 1;
  uint32_t depth = 0;
  std::map<uint64_t, std::shared_ptr<Service>> services;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

class Dispatch {
public:
  Dispatch() {
    ++registry().depth;
  }
  ~Dispatch() {
    --registry().depth;
  }
};

bool authorized(const char *provider) {
  const auto owner = module_resources::current();
  return owner && provider &&
         (owner->id == provider ||
          std::find(owner->dependencies.begin(), owner->dependencies.end(),
                    provider) != owner->dependencies.end());
}

std::shared_ptr<Service> find(const char *provider, const char *name,
                              uint32_t version) {
  if (!provider || !validName(name) || version == 0)
    return {};
  for (const auto &[id, service] : registry().services) {
    const auto &owner = service->lifetime.owner();
    if (owner && owner->id == provider && service->name == name &&
        service->version == version && module_resources::enabled(owner) &&
        !owner->initializing)
      return service;
  }
  return {};
}

} // namespace

AbcUiServiceRegistration registerService(const char *name, uint32_t version,
                                         AbcUiServiceHandler handler,
                                         void *userdata) {
  const auto owner = module_resources::current();
  if (!owner || !owner->initializing || !module_resources::accepting() ||
      !validName(name) || !version || !handler)
    return 0;
  for (const auto &[id, service] : registry().services)
    if (service->lifetime.owner() == owner && service->name == name &&
        service->version == version)
      return 0;
  const uint64_t id = registry().next++;
  auto service = std::make_shared<Service>();
  service->name = name;
  service->version = version;
  service->handler = handler;
  service->userdata = userdata;
  service->lifetime.track([id] { registry().services.erase(id); });
  registry().services.emplace(id, std::move(service));
  return id;
}

void unregisterService(AbcUiServiceRegistration registration) {
  const auto it = registry().services.find(registration);
  if (it != registry().services.end() &&
      it->second->lifetime.owner() == module_resources::current())
    registry().services.erase(it);
}

int32_t available(const char *provider, const char *name, uint32_t version) {
  return module_resources::accepting() && authorized(provider) &&
                 find(provider, name, version)
             ? 1
             : 0;
}

int32_t call(const char *provider, const char *name, uint32_t version,
             const void *request, uint32_t requestSize, void *response,
             uint32_t responseCapacity, uint32_t *responseSize) {
  if (responseSize)
    *responseSize = 0;
  if (!responseSize || !provider || !validName(name) || !version ||
      requestSize > kMaxBuffer || responseCapacity > kMaxBuffer ||
      (requestSize && !request) || (responseCapacity && !response))
    return ABCUI_SERVICE_INVALID;
  if (!module_resources::accepting() || !authorized(provider))
    return ABCUI_SERVICE_FORBIDDEN;
  const auto service = find(provider, name, version);
  if (!service)
    return ABCUI_SERVICE_UNAVAILABLE;
  if (registry().depth >= 16)
    return ABCUI_SERVICE_DEPTH;
  Dispatch dispatch;
  int32_t result = ABCUI_SERVICE_FAILED;
  const bool returned = module_resources::attempt(
      service->lifetime.owner(), "module service", [&] {
        result = service->handler(service->userdata, request, requestSize,
                                  response, responseCapacity, responseSize);
      });
  if (!returned || result < 0 || *responseSize > kMaxBuffer) {
    *responseSize = 0;
    return ABCUI_SERVICE_FAILED;
  }
  return result;
}

int32_t publish(const char *name, uint32_t schemaVersion, const void *payload,
                uint32_t payloadSize) {
  const auto owner = module_resources::current();
  if (!owner || !module_resources::accepting() || !validName(name) ||
      !schemaVersion || payloadSize > kMaxBuffer || (payloadSize && !payload) ||
      registry().depth >= 16)
    return 0;
  Dispatch dispatch;
  const std::string channel = "module." + owner->id + "." + name;
  const AbcUiModuleEvent event = {sizeof(AbcUiModuleEvent), schemaVersion,
                                  payloadSize, payload};
  eventBus().publish(channel.c_str(), event);
  return 1;
}

} // namespace framework::module_services
