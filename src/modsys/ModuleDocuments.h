#pragma once

// framework::module_documents - interactive documents whose page lives in a
// module (the counterpart of the passive ModulePanel).
//
// A page module registers an AbcUiDocumentDesc (documents->create): the
// framework owns the C++/Rml side (lifetime, context load, focus, show/hide,
// z-order) and forwards the lifecycle callbacks to the descriptor. The
// descriptor strings live in the package image, which outlives every HUD
// cycle, but the document itself does not: every Rml context cycle reloads
// the pages (the same rule InteractiveDocument follows).
//
// The menu flag (ABCUI_DOC_FLAG_MENU) joins a document to the replacement
// menu layer: the framework hides those documents whenever the menu lowers
// and accepts focus inside them while they are visible (the per-frame guard
// in GameUiModule). The pages decide on their own when to show, driven by
// menu.state / menu.paint.

#include <abcui/module_api.h>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace framework {
namespace module_documents {

// Copies the descriptor (and its strings) and registers the page. A live
// context loads it immediately; otherwise the next contextReady does.
// Refused (null) for an incomplete descriptor or a duplicate name.
AbcUiDocument *create(const AbcUiDocumentDesc &desc);

// Closes and unregisters the page; onClose fires while the module is still
// loaded and the DOM still exists.
void destroy(AbcUiDocument *doc);

// True when the handle belongs to this registry (the ui section keeps panel
// and page handles on the same opaque type).
bool isDocument(AbcUiDocument *doc);
Rml::ElementDocument *documentOf(AbcUiDocument *doc);
int32_t visible(AbcUiDocument *doc);
int32_t degraded(AbcUiDocument *doc);
void show(AbcUiDocument *doc);
void hide(AbcUiDocument *doc);
int32_t retry(AbcUiDocument *doc);
AbcUiRecoveryRegistration registerRecovery(const AbcUiRecoveryDesc *desc);
void unregisterRecovery(AbcUiRecoveryRegistration registration);
void removeRecovery(AbcUiRecoveryRegistration registration);
int32_t requestRetry(AbcUiDocument *doc);

// Rml context cycle: load every registered page (and any page registered
// later loads on creation). shutdown() closes them while the context is
// still alive; unload() additionally forgets the descriptors (process
// teardown, before the module images go away).
void contextReady(Rml::Context *context);
// Per-frame beat: a degraded page's Retry button is acted on here (never from
// inside its own event dispatch).
void pump();
void shutdown();
void unload();

// Menu-layer integration: hide every visible menu page, and report whether a
// visible registered page owns a DOM node (the focus guard's legitimacy
// test).
void hideMenu();
bool ownsVisible(Rml::ElementDocument *document);

} // namespace module_documents
} // namespace framework
