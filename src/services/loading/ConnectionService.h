#pragma once

// services::loading::connection access - the sdk connection section's
// access point: the process-lifetime attempt model plus the controller's
// typed edges. The model is fed by the engine feed drain (the composition
// root owns it); the error reason, the retry/return intents and the
// password dispatch belong to the controller layer, so they cross as
// function pointers the composition root wires once.

#include <string>

namespace services {
namespace loading {

class LoadingModel;

struct ConnectionAccess {
  LoadingModel *model = nullptr;
  // The error overlay's reason (the controller's owned copy).
  std::string (*errorReason)() = nullptr;
  // The model latch; false when already latched.
  bool (*cancelLoading)() = nullptr;
  void (*retryFailed)() = nullptr;
  void (*returnHome)() = nullptr;
  bool (*submitPassword)(const char *password) = nullptr;
  void (*passwordCancel)() = nullptr;
  // Validates and dispatches; false refused with the hint token.
  bool (*connectAddress)(const char *address, std::string &errorToken);
};

void setConnectionAccess(const ConnectionAccess &access);
const ConnectionAccess &connectionAccess();

} // namespace loading
} // namespace services
