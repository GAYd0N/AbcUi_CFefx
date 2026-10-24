#pragma once

#include <abcui/module_api.h>

namespace framework::module_services {

AbcUiServiceRegistration registerService(const char *name, uint32_t version,
                                         AbcUiServiceHandler handler,
                                         void *userdata);
void unregisterService(AbcUiServiceRegistration registration);
int32_t available(const char *provider, const char *name, uint32_t version);
int32_t call(const char *provider, const char *name, uint32_t version,
             const void *request, uint32_t requestSize, void *response,
             uint32_t responseCapacity, uint32_t *responseSize);
int32_t publish(const char *name, uint32_t schemaVersion, const void *payload,
                uint32_t payloadSize);

} // namespace framework::module_services
