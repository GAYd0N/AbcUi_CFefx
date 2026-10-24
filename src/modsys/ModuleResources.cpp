#include "ModuleResources.h"

#include <abc/Console.h>

namespace framework::module_resources {

void reportException(const char *id, const char *what) {
  abc::ConsoleError("[AbcUi] module \"%s\": exception escaped %s\n", id, what);
}

void reportInitializationFailure(const char *id, const char *reason) {
  abc::ConsoleError("[AbcUi] module \"%s\": initialization failed: %s\n", id,
                    reason && *reason ? reason : "unspecified");
}

} // namespace framework::module_resources
