#pragma once

// framework::menubackdrop - the engine-free seam between a menu document and
// the shared blurred backdrop.
//
// Interactive documents carry the "this page wants a backdrop" fact (their
// constructor), but they must not depend on the Rml element, the Renderer or
// any engine header: the offline page tests compile these translation units
// without a game. The rmlui layer installs the real implementation once at
// context assembly (rmlui::backdrop::registerElement); every other caller
// gets a no-op (or its own recorder, in tests).

namespace Rml {
class ElementDocument;
}

namespace framework::menubackdrop {

using Attacher = void (*)(Rml::ElementDocument *document);

// Installs/replaces the implementation; null restores the no-op.
void setAttacher(Attacher attacher);

// Inserts the backdrop into the document when an implementation is installed.
// Returns true when the document was handed to an attacher.
bool attach(Rml::ElementDocument *document);

} // namespace framework::menubackdrop
