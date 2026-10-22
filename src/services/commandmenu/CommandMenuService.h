#pragma once

// services::commandmenu::access - the process-lifetime command-menu model
// and the flow's engine reach.
//
// The model (the parsed commandmenu.json tree and its column navigation)
// and the open/dismiss intents belong to the framework; the command-menu
// page is a module and reads the model through the sdk commandmenu section.
// `submitCommand` is the page's command boundary (close first, then the
// engine send - the shared command path may synchronously raise another
// page) and `dismiss` is the flow's full close with the raised-menu
// discipline. The access point is null until the composition root has
// assembled (never a second copy of the model).

namespace services {
namespace commandmenu {

class Model;

struct Access {
  Model *model = nullptr;
  void (*submitCommand)(const char *command) = nullptr;
  void (*dismiss)() = nullptr;
};

void setAccess(const Access &access);
const Access &access();

} // namespace commandmenu
} // namespace services
