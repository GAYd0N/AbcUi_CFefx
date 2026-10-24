// framework::DataApi / framework::UiApi - sdk data and ui sections. See
// ApiTable.h for how they join the module table.
#include "ApiTable.h"
#include "ApiSnapshot.h"
#include "ModuleResources.h"

#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementProgress.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>

#include <event_api.h>
#include <RmlUi/Core/ElementDocument.h>

#include "../data/HudState.h"
#include "../data/NetworkMessages.h"
#include "../data/PlayerTrace.h"
#include "../framework/ColorUtil.h"
#include "../framework/HudPanel.h"
#include "../framework/Localization.h"
#include "../framework/Projection.h"
#include "../framework/PanelManager.h"
#include "../framework/TextUtil.h"
#include "../framework/Theme.h"
#include "../rmlui/VGUI2ExtensionImport.h"
#include "../services/crosshair/Geometry.h"
#include "../rmlui/animated/ElementAnimatedImage.h"
#include "../rmlui/texture/SpriteResource.h"
#include "ModuleDocuments.h"

namespace framework {
namespace {

// Copies a string into a module-owned buffer, always terminating; returns the
// number of bytes written (never outSize or more).
int copyOut(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return 0;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
  return static_cast<int>(i);
}

} // namespace

namespace UiApi {
namespace {

// Documents reach the ui section as one opaque handle type; panel handles
// are owned by PanelManager, page handles by module_documents. Resolve by
// membership (never by casting) so a stale handle is a no-op instead of a
// wild pointer.
Rml::ElementDocument *documentOf(AbcUiDocument *doc) {
  if (!doc)
    return nullptr;
  if (module_documents::isDocument(doc))
    return module_documents::documentOf(doc);
  const HudPanel *panel = reinterpret_cast<HudPanel *>(doc);
  for (const auto &candidate : PanelManager::panels()) {
    if (candidate.get() == panel)
      return candidate->getDocument();
  }
  return nullptr;
}

HudPanel *panelOf(AbcUiDocument *doc) {
  if (!doc || module_documents::isDocument(doc))
    return nullptr;
  const HudPanel *panel = reinterpret_cast<HudPanel *>(doc);
  for (const auto &candidate : PanelManager::panels()) {
    if (candidate.get() == panel)
      return const_cast<HudPanel *>(candidate.get());
  }
  return nullptr;
}

Rml::Element *elementOf(AbcUiElement *element) {
  return reinterpret_cast<Rml::Element *>(element);
}

AbcUiDocument *apiPanelDocument(const char *panelName) {
  if (!panelName)
    return nullptr;
  return reinterpret_cast<AbcUiDocument *>(PanelManager::find(panelName));
}

void apiDocumentShow(AbcUiDocument *doc) {
  if (module_documents::isDocument(doc)) {
    module_documents::show(doc);
    return;
  }
  if (HudPanel *panel = panelOf(doc))
    panel->show();
}

void apiDocumentHide(AbcUiDocument *doc) {
  if (module_documents::isDocument(doc)) {
    module_documents::hide(doc);
    return;
  }
  if (HudPanel *panel = panelOf(doc))
    panel->hide();
}

void apiDocumentSetProperty(AbcUiDocument *doc, const char *name,
                            const char *value) {
  if (!name || !value)
    return;
  if (Rml::ElementDocument *document = documentOf(doc))
    document->SetProperty(name, value);
}

AbcUiElement *apiQuerySelector(AbcUiDocument *doc, const char *id) {
  if (!id || !*id)
    return nullptr;
  const char *key = (*id == '#') ? id + 1 : id;
  if (Rml::ElementDocument *document = documentOf(doc))
    return reinterpret_cast<AbcUiElement *>(document->GetElementById(key));
  return nullptr;
}

void apiDocumentFocus(AbcUiDocument *doc) {
  if (Rml::ElementDocument *document = documentOf(doc))
    document->Focus();
}

int apiElementAttribute(AbcUiElement *element, const char *name, char *out,
                        uint32_t outSize) {
  Rml::Element *target = elementOf(element);
  if (!target || !name || !*name)
    return 0;
  const Rml::String value = target->GetAttribute<Rml::String>(name, "");
  if (out && outSize > 0)
    copyOut(value, out, outSize);
  return static_cast<int>(value.size());
}

int apiEscapeRmlAttribute(const char *text, char *out, uint32_t outSize) {
  if (!text)
    return 0;
  const std::string escaped = TextUtil::escapeRmlAttribute(text);
  if (out && outSize > 0)
    copyOut(escaped, out, outSize);
  return static_cast<int>(escaped.size());
}

void apiDocumentBlur(AbcUiDocument *doc) {
  Rml::ElementDocument *document = documentOf(doc);
  if (!document)
    return;
  if (Rml::Context *context = document->GetContext())
    context->UnfocusDocument(document);
}

void apiElementSetProperty(AbcUiElement *element, const char *name,
                           const char *value) {
  if (element && name && value)
    elementOf(element)->SetProperty(name, value);
}

void apiElementSetInnerRML(AbcUiElement *element, const char *rml) {
  if (element && rml)
    elementOf(element)->SetInnerRML(rml);
}

int apiTranslate(const char *token, char *out, uint32_t outSize) {
  if (!token || !*token || !out || outSize == 0)
    return 0;
  std::string translated;
  if (!Localization::translate(translated, token))
    return 0;
  copyOut(translated, out, outSize);
  return 1;
}

int apiEscapeRml(const char *text, char *out, uint32_t outSize) {
  if (!text)
    return 0;
  const std::string escaped = TextUtil::escapeRml(text);
  if (out && outSize > 0)
    copyOut(escaped, out, outSize);
  return static_cast<int>(escaped.size());
}

int apiKeyNameForKey(int keynum, char *out, uint32_t outSize) {
  const char *name = vgui2ext::keyNameForKey(keynum);
  const std::string text = name ? name : "";
  if (out && outSize > 0)
    copyOut(text, out, outSize);
  return static_cast<int>(text.size());
}

void apiFormatColor(const float rgb[3], char *out, uint32_t outSize) {
  if (!rgb || !out || outSize == 0)
    return;
  copyOut(ColorUtil::format(rgb), out, outSize);
}

void apiDifferColor(const float base[3], const float danger[3], float ratio,
                    char *out, uint32_t outSize) {
  if (!base || !danger || !out || outSize == 0)
    return;
  float rgb[3];
  ColorUtil::differColor(base, danger, ratio, rgb);
  copyOut(ColorUtil::format(rgb), out, outSize);
}

void apiElementSetAttribute(AbcUiElement *element, const char *name,
                            const char *value) {
  if (!element || !name || !*name)
    return;
  Rml::Element *target = elementOf(element);
  if (!value)
    target->RemoveAttribute(name);
  else
    target->SetAttribute(name, value);
}

void apiElementSetClass(AbcUiElement *element, const char *name, int enabled) {
  if (!element || !name || !*name)
    return;
  elementOf(element)->SetClass(name, enabled != 0);
}

void apiElementRestartAnimation(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  if (!target)
    return;
  if (auto *animated =
          rmlui_dynamic_cast<rmlui::animated::ElementAnimatedImage *>(target))
    animated->Restart();
}

void apiFormatRgba(int r, int g, int b, int a, char *out, uint32_t outSize) {
  copyOut(ColorUtil::formatRgba(r, g, b, a), out, outSize);
}

int apiProject(const float world[3], float screen[2]) {
  if (!world || !screen)
    return 1;
  return projection::worldToScreen(world, screen);
}

int apiDocumentSize(AbcUiDocument *doc, int *width, int *height) {
  Rml::ElementDocument *document = documentOf(doc);
  Rml::Context *context = document ? document->GetContext() : nullptr;
  if (!context)
    return 0;
  const Rml::Vector2i dimensions = context->GetDimensions();
  if (width)
    *width = dimensions.x;
  if (height)
    *height = dimensions.y;
  return 1;
}

float apiScale() {
  return Theme::scale();
}

// The registry owns listeners through removal or context shutdown. Observer
// pointers make late removal safe after the target element has died.
// The ABI carries engine key numbers (the module cannot include the engine
// headers); this is KeyMap's mapping in reverse, for the keys a DOM
// keyboard handler acts on.
int engineKeynumOf(Rml::Input::KeyIdentifier identifier) {
  using Rml::Input::KeyIdentifier;
  if (identifier >= KeyIdentifier::KI_0 && identifier <= KeyIdentifier::KI_9)
    return '0' + static_cast<int>(identifier) -
           static_cast<int>(KeyIdentifier::KI_0);
  if (identifier >= KeyIdentifier::KI_A && identifier <= KeyIdentifier::KI_Z)
    return 'a' + static_cast<int>(identifier) -
           static_cast<int>(KeyIdentifier::KI_A);
  switch (identifier) {
  case KeyIdentifier::KI_SPACE:
    return 32;
  case KeyIdentifier::KI_TAB:
    return 9;
  case KeyIdentifier::KI_RETURN:
    return 13;
  case KeyIdentifier::KI_ESCAPE:
    return 27;
  case KeyIdentifier::KI_BACK:
    return 127;
  case KeyIdentifier::KI_UP:
    return 128;
  case KeyIdentifier::KI_DOWN:
    return 129;
  case KeyIdentifier::KI_LEFT:
    return 130;
  case KeyIdentifier::KI_RIGHT:
    return 131;
  case KeyIdentifier::KI_NUMPADENTER:
    return 169;
  default:
    return 0;
  }
}

struct ModuleElementListener final
    : Rml::EventListener,
      std::enable_shared_from_this<ModuleElementListener> {
  AbcUiElementListener callback = nullptr;
  void *userdata = nullptr;
  uint64_t id = 0;
  // Registered in the capture phase? The element's dispatcher keys entries by
  // (event id, phase), so removal must use the same phase or it silently
  // misses and leaves a dangling pointer behind.
  bool capture = false;
  Rml::ObserverPtr<Rml::Element> target;
  std::string type;
  module_resources::Registration lifetime;

  void ProcessEvent(Rml::Event &event) override {
    auto keepAlive = shared_from_this();
    if (!callback || !module_resources::enabled(lifetime.owner()))
      return;
    AbcUiElementEvent payload{};
    payload.structSize = sizeof(AbcUiElementEvent);
    const Rml::String type = event.GetType();
    payload.type = type.c_str();
    payload.button = event.GetParameter<int>("button", -1);
    payload.mouseX = event.GetParameter("mouse_x", 0.0f);
    payload.mouseY = event.GetParameter("mouse_y", 0.0f);
    if (type == "keydown" || type == "keyup") {
      payload.keynum = engineKeynumOf(static_cast<Rml::Input::KeyIdentifier>(
          event.GetParameter<int>("key_identifier", 0)));
      payload.modifiers =
          (event.GetParameter<int>("ctrl_key", 0) ? ABCUI_MOD_CTRL : 0) |
          (event.GetParameter<int>("shift_key", 0) ? ABCUI_MOD_SHIFT : 0);
    }
    if (module_resources::invoke(
            lifetime.owner(), "DOM event", callback, userdata,
            reinterpret_cast<AbcUiElement *>(event.GetTargetElement()),
            &payload))
      event.StopPropagation();
  }
};

std::map<uint64_t, std::shared_ptr<ModuleElementListener>> &elementListeners() {
  static std::map<uint64_t, std::shared_ptr<ModuleElementListener>> instance;
  return instance;
}

uint64_t NextListenerId = 1;

void apiElementRemoveListener(AbcUiElement *, const char *, uint64_t id) {
  const auto it = elementListeners().find(id);
  if (it == elementListeners().end())
    return;
  auto entry = it->second;
  elementListeners().erase(it); // detach may re-enter removal
  if (auto *target = entry->target.get())
    target->RemoveEventListener(entry->type, entry.get(), entry->capture);
}

uint64_t addElementListener(AbcUiElement *element, const char *type,
                            AbcUiElementListener listener, void *userdata,
                            bool capture) {
  Rml::Element *target = elementOf(element);
  if (!module_resources::accepting() ||
      !module_resources::contextRegistrationsOpen() || !target || !type ||
      !*type || !listener)
    return 0;
  auto entry = std::make_shared<ModuleElementListener>();
  const uint64_t id = NextListenerId++;
  entry->callback = listener;
  entry->userdata = userdata;
  entry->id = id;
  entry->capture = capture;
  entry->target = target->GetObserverPtr();
  entry->type = type;
  elementListeners()[id] = entry;
  entry->lifetime.track(
      [id] { apiElementRemoveListener(nullptr, nullptr, id); },
      module_resources::Lifetime::Context);
  target->AddEventListener(type, entry.get(), capture);
  return id;
}

uint64_t apiElementAddListener(AbcUiElement *element, const char *type,
                               AbcUiElementListener listener, void *userdata) {
  return addElementListener(element, type, listener, userdata, false);
}

uint64_t apiElementAddListenerCapture(AbcUiElement *element, const char *type,
                                      AbcUiElementListener listener,
                                      void *userdata) {
  return addElementListener(element, type, listener, userdata, true);
}

int32_t apiElementAttributeInt(AbcUiElement *element, const char *name,
                               int32_t fallback) {
  Rml::Element *target = elementOf(element);
  if (!target || !name || !*name)
    return fallback;
  const Rml::Variant *value = target->GetAttribute(name);
  return value ? value->Get<int>() : fallback;
}

AbcUiElement *apiElementParent(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  return target ? reinterpret_cast<AbcUiElement *>(target->GetParentNode())
                : nullptr;
}

int32_t apiElementClassSet(AbcUiElement *element, const char *name) {
  Rml::Element *target = elementOf(element);
  return target && name && *name && target->IsClassSet(name) ? 1 : 0;
}

void apiElementFocus(AbcUiElement *element) {
  if (Rml::Element *target = elementOf(element))
    target->Focus();
}

void apiElementBlur(AbcUiElement *element) {
  if (Rml::Element *target = elementOf(element))
    target->Blur();
}

int32_t apiElementValue(AbcUiElement *element, char *out, uint32_t outSize) {
  Rml::Element *target = elementOf(element);
  auto *control =
      target ? rmlui_dynamic_cast<Rml::ElementFormControl *>(target) : nullptr;
  const std::string value = control ? control->GetValue().c_str() : "";
  if (out && outSize > 0)
    copyOut(value, out, outSize);
  return static_cast<int32_t>(value.size());
}

void apiElementSetValue(AbcUiElement *element, const char *text) {
  Rml::Element *target = elementOf(element);
  auto *control =
      target ? rmlui_dynamic_cast<Rml::ElementFormControl *>(target) : nullptr;
  if (control)
    control->SetValue(text ? text : "");
}

float apiElementScrollTop(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  return target ? target->GetScrollTop() : 0.0f;
}

void apiElementSetScrollTop(AbcUiElement *element, float top) {
  if (Rml::Element *target = elementOf(element))
    target->SetScrollTop(top);
}

float apiElementScrollHeight(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  return target ? target->GetScrollHeight() : 0.0f;
}

float apiElementClientHeight(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  return target ? target->GetClientHeight() : 0.0f;
}

void apiElementSetSelection(AbcUiElement *element, int32_t start, int32_t end) {
  Rml::Element *target = elementOf(element);
  auto *input = target
                    ? rmlui_dynamic_cast<Rml::ElementFormControlInput *>(target)
                    : nullptr;
  if (input)
    input->SetSelectionRange(start, end);
}

const char *apiElementId(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  return target ? target->GetId().c_str() : "";
}

AbcUiElement *apiElementFind(AbcUiElement *element, const char *id) {
  Rml::Element *root = elementOf(element);
  if (!root || !id || !*id)
    return nullptr;
  return reinterpret_cast<AbcUiElement *>(
      Rml::ElementUtilities::GetElementById(root, id));
}

AbcUiElement *apiElementChild(AbcUiElement *element, int32_t index) {
  Rml::Element *target = elementOf(element);
  if (!target || index < 0 || index >= target->GetNumChildren())
    return nullptr;
  return reinterpret_cast<AbcUiElement *>(target->GetChild(index));
}

int32_t apiElementChildCount(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  return target ? static_cast<int32_t>(target->GetNumChildren()) : 0;
}

int32_t apiElementAbsoluteOffset(AbcUiElement *element, float *x, float *y) {
  Rml::Element *target = elementOf(element);
  if (!target)
    return 0;
  const Rml::Vector2f offset = target->GetAbsoluteOffset();
  if (x)
    *x = offset.x;
  if (y)
    *y = offset.y;
  return 1;
}

int32_t apiElementOffsetSize(AbcUiElement *element, float *width,
                             float *height) {
  Rml::Element *target = elementOf(element);
  if (!target)
    return 0;
  if (width)
    *width = target->GetOffsetWidth();
  if (height)
    *height = target->GetOffsetHeight();
  return 1;
}

// The row's laid-out text lives in its first text child; every layout
// query below resolves through it.
Rml::ElementText *textContentOf(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  if (!target || target->GetNumChildren() == 0)
    return nullptr;
  return rmlui_dynamic_cast<Rml::ElementText *>(target->GetChild(0));
}

int32_t apiElementTextLayout(AbcUiElement *element, AbcUiTextLine *out,
                             uint32_t max, float *lineHeight, float *originX,
                             float *originY) {
  Rml::ElementText *text = textContentOf(element);
  if (!text)
    return 0;
  const auto &lines = text->GetLines();
  if (lineHeight)
    *lineHeight = text->GetLineHeight();
  const Rml::Vector2f origin = text->GetAbsoluteOffset();
  if (originX)
    *originX = origin.x;
  if (originY)
    *originY = origin.y;
  if (!out || max == 0)
    return static_cast<int32_t>(lines.size());
  const uint32_t count =
      std::min<uint32_t>(max, static_cast<uint32_t>(lines.size()));
  for (uint32_t i = 0; i < count; ++i) {
    out[i].x = lines[i].position.x;
    out[i].y = lines[i].position.y;
    out[i].byteOffset = 0;
    out[i].bytes = static_cast<uint32_t>(lines[i].text.size());
    // Byte offsets accumulate in full-text order (the page slices its own
    // row-text copy with them).
    if (i > 0) {
      out[i].byteOffset = out[i - 1].byteOffset + out[i - 1].bytes;
    }
  }
  return static_cast<int32_t>(lines.size());
}

int32_t apiElementTextPrefixWidth(AbcUiElement *element, const char *value,
                                  uint32_t bytesLength) {
  Rml::ElementText *text = textContentOf(element);
  if (!text || !value)
    return 0;
  return Rml::ElementUtilities::GetStringWidth(
      text, Rml::StringView(value, value + bytesLength));
}

void apiElementProgressValue(AbcUiElement *element, float value) {
  Rml::Element *target = elementOf(element);
  if (auto *progress = rmlui_dynamic_cast<Rml::ElementProgress *>(target))
    progress->SetValue(value);
}

int32_t apiElementPseudoClassSet(AbcUiElement *element, const char *name) {
  Rml::Element *target = elementOf(element);
  return target && name && *name && target->IsPseudoClassSet(name) ? 1 : 0;
}

int32_t apiDocumentScale(AbcUiDocument *doc, float *scale) {
  Rml::ElementDocument *document = documentOf(doc);
  if (!document || !scale)
    return 0;
  *scale = Rml::ElementUtilities::GetDensityIndependentPixelRatio(document);
  return 1;
}

void apiElementSelectRemoveAll(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  if (auto *select =
          rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(target))
    select->RemoveAll();
}

void apiElementSelectAdd(AbcUiElement *element, const char *label,
                         const char *value) {
  Rml::Element *target = elementOf(element);
  if (!label || !value)
    return;
  if (auto *select =
          rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(target))
    select->Add(label, value);
}

int32_t apiElementSelectCount(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  if (auto *select =
          rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(target))
    return select->GetNumOptions();
  return 0;
}

void apiElementSelectSetSelection(AbcUiElement *element, int32_t index) {
  Rml::Element *target = elementOf(element);
  if (auto *select =
          rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(target))
    select->SetSelection(index);
}

int32_t apiElementSelectSelection(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  if (auto *select =
          rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(target))
    return select->GetSelection();
  return -1;
}

void apiElementScrollIntoView(AbcUiElement *element, int32_t alignTop) {
  Rml::Element *target = elementOf(element);
  if (target)
    target->ScrollIntoView(alignTop != 0);
}

void apiCrosshairLayout(const AbcUiCrosshairLayout *layout, float core[5][4],
                        float outline[5][4], int32_t visible[5]) {
  if (!layout)
    return;
  using services::crosshair::Part;
  services::crosshair::Layout request;
  request.Thickness = layout->thickness;
  request.Length = layout->length;
  request.Offset = layout->offset;
  request.OutlineWidth = layout->outlineWidth;
  request.Dot = layout->dot != 0;
  request.TShape = layout->tShape != 0;
  services::crosshair::Rect coreRects[services::crosshair::kPartCount];
  services::crosshair::Rect outlineRects[services::crosshair::kPartCount];
  services::crosshair::coreRects(request, coreRects);
  services::crosshair::outlineRects(request, outlineRects);
  static constexpr Part kParts[services::crosshair::kPartCount] = {
      Part::kCenter, Part::kTop, Part::kBottom, Part::kLeft, Part::kRight};
  for (int i = 0; i < services::crosshair::kPartCount; ++i) {
    if (core) {
      core[i][0] = coreRects[i].X;
      core[i][1] = coreRects[i].Y;
      core[i][2] = coreRects[i].Width;
      core[i][3] = coreRects[i].Height;
    }
    if (outline) {
      outline[i][0] = outlineRects[i].X;
      outline[i][1] = outlineRects[i].Y;
      outline[i][2] = outlineRects[i].Width;
      outline[i][3] = outlineRects[i].Height;
    }
    if (visible)
      visible[i] = services::crosshair::partVisible(request, kParts[i]) ? 1 : 0;
  }
}

int apiHudSprite(const char *name, char *atlas, uint32_t atlasSize,
                 int32_t rect[4]) {
  if (!name)
    return 0;
  std::string sprite;
  wrect_t crop{};
  if (!sprites::findHudSprite(name, sprite, crop))
    return 0;
  if (atlas && atlasSize > 0)
    copyOut(sprite, atlas, atlasSize);
  if (rect) {
    rect[0] = crop.left;
    rect[1] = crop.top;
    rect[2] = crop.right;
    rect[3] = crop.bottom;
  }
  return 1;
}

int apiDocumentVisible(AbcUiDocument *doc) {
  if (module_documents::isDocument(doc))
    return module_documents::visible(doc);
  const HudPanel *panel = panelOf(doc);
  return panel && panel->isVisible() ? 1 : 0;
}

AbcUiElement *apiElementAppendRml(AbcUiElement *parent, const char *rml,
                                  const char *className) {
  Rml::Element *element = elementOf(parent);
  if (!element || !rml)
    return nullptr;
  Rml::ElementDocument *document = element->GetOwnerDocument();
  if (!document)
    return nullptr;
  Rml::ElementPtr holder = document->CreateElement("div");
  if (!holder)
    return nullptr;
  Rml::Element *created = holder.get();
  if (className && *className)
    created->SetClass(className, true);
  created->SetInnerRML(rml);
  element->AppendChild(std::move(holder));
  return reinterpret_cast<AbcUiElement *>(created);
}

void apiElementRemove(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  if (!target)
    return;
  if (Rml::Element *parent = target->GetParentNode())
    parent->RemoveChild(target);
}

int apiElementInnerRml(AbcUiElement *element, char *out, uint32_t outSize) {
  Rml::Element *target = elementOf(element);
  if (!target)
    return 0;
  const std::string inner = target->GetInnerRML();
  if (out && outSize > 0)
    copyOut(inner, out, outSize);
  return static_cast<int>(inner.size());
}

AbcUiElement *apiElementFindTag(AbcUiElement *element, const char *tag) {
  Rml::Element *target = elementOf(element);
  if (!target || !tag || !*tag)
    return nullptr;
  // Widget parts (a range slider's thumb) are non-DOM children appended
  // after the DOM ones, so the search must include them.
  AbcUiElement *found = nullptr;
  const int count = target->GetNumChildren(true);
  for (int index = 0; index < count; ++index) {
    Rml::Element *child = target->GetChild(index);
    if (child && child->GetTagName() == tag)
      found = reinterpret_cast<AbcUiElement *>(child);
  }
  return found;
}

void apiElementSetDisabled(AbcUiElement *element, int32_t disabled) {
  Rml::Element *target = elementOf(element);
  auto *control =
      target ? rmlui_dynamic_cast<Rml::ElementFormControl *>(target) : nullptr;
  if (control)
    control->SetDisabled(disabled != 0);
}

void apiFormatHueColor(float value, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  float rgb[3];
  ColorUtil::playerHueColor(value, rgb);
  copyOut(ColorUtil::format(rgb), out, outSize);
}

void apiReleaseTexture(const char *source) {
  if (source && *source)
    Rml::ReleaseTexture(source);
}

int32_t apiElementAppendChild(AbcUiElement *parent, AbcUiElement *child) {
  Rml::Element *target = elementOf(parent);
  Rml::Element *node = elementOf(child);
  if (!target || !node || target == node)
    return 0;
  // Never let an element move under its own subtree.
  for (Rml::Element *ancestor = target; ancestor;
       ancestor = ancestor->GetParentNode()) {
    if (ancestor == node)
      return 0;
  }
  Rml::Element *previous = node->GetParentNode();
  if (!previous)
    return 0;
  if (previous == target)
    return 1;
  Rml::ElementPtr owned = previous->RemoveChild(node);
  if (!owned)
    return 0;
  target->AppendChild(std::move(owned));
  return 1;
}

const AbcUiUiApi kUiApi = {
    sizeof(AbcUiUiApi),
    apiPanelDocument,
    apiDocumentShow,
    apiDocumentHide,
    apiDocumentSetProperty,
    apiQuerySelector,
    apiElementSetProperty,
    apiElementSetInnerRML,

    apiTranslate,
    apiEscapeRml,
    apiKeyNameForKey,
    apiDocumentVisible,
    apiElementAppendRml,
    apiElementRemove,
    apiElementInnerRml,
    apiFormatColor,
    apiDifferColor,
    apiElementSetAttribute,
    apiElementSetClass,
    apiHudSprite,
    apiElementRestartAnimation,
    apiFormatRgba,
    apiProject,
    apiDocumentSize,
    apiCrosshairLayout,
    apiScale,
    apiElementChild,
    apiElementFind,
    apiElementAddListener,
    apiElementRemoveListener,
    apiElementAttributeInt,
    apiElementParent,
    apiElementClassSet,
    apiElementId,
    apiElementFocus,
    apiElementBlur,
    apiElementValue,
    apiElementSetValue,
    apiElementScrollTop,
    apiElementSetScrollTop,
    apiElementScrollHeight,
    apiElementClientHeight,
    apiElementSetSelection,
    apiDocumentFocus,
    apiDocumentBlur,
    apiElementAttribute,
    apiEscapeRmlAttribute,
    apiElementChildCount,
    apiElementSelectRemoveAll,
    apiElementSelectAdd,
    apiElementSelectCount,
    apiElementSelectSetSelection,
    apiElementSelectSelection,
    apiElementScrollIntoView,
    apiElementAddListenerCapture,
    apiElementAbsoluteOffset,
    apiElementOffsetSize,
    apiElementTextLayout,
    apiElementTextPrefixWidth,
    apiDocumentScale,
    apiElementPseudoClassSet,
    apiElementProgressValue,
    apiElementFindTag,
    apiElementSetDisabled,
    apiFormatHueColor,
    apiReleaseTexture,
    apiElementAppendChild,
};

} // namespace

const AbcUiUiApi *get() {
  return &kUiApi;
}

} // namespace UiApi

namespace DataApi {
namespace {

void apiHudState(AbcUiHudState *out) {
  if (!out)
    return;
  AbcUiHudState state{};
  AbcUiHudState *target = &state;
  state.structSize = sizeof(AbcUiHudState);
  target->suit = hudstate::hasSuit() ? 1 : 0;
  target->spectate = hudstate::isSpectate() ? 1 : 0;
  target->spectateMode = hudstate::spectateMode();
  target->spectateTarget = hudstate::spectateTarget();
  target->longjump = hudstate::hasLongJump() ? 1 : 0;
  target->hideBits = hudstate::debugHideTokens();
  const hudstate::MessageSnapshot &messages = hudstate::messages();
  target->flashOn = messages.FlashOn ? 1 : 0;
  target->flashBattery = messages.FlashBattery;
  target->health = static_cast<int32_t>(hudstate::health());
  target->armor = messages.Armor;
  target->waterLevel = hudstate::waterlevel();
  target->fov = hudstate::fov();
  const float *punch = hudstate::punchangle();
  if (punch) {
    target->punchAngle[0] = punch[0];
    target->punchAngle[1] = punch[1];
    target->punchAngle[2] = punch[2];
  }
  abi::copySnapshot(out, state);
}

// The ScoreInfo feed carries the scoreboard rows; the local row's frags is
// what the HUD score readouts show (old panel-local latch).
int &localFrags() {
  static int frags = 0;
  return frags;
}

void onScoreInfo(int clientIndex, float frags, int, float, float, int, int,
                 int) {
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (local && clientIndex == local->index)
    localFrags() = static_cast<int>(frags);
}

void watchScoreInfo() {
  static bool watched = false;
  if (watched)
    return;
  watched = true;
  netmsg::ScoreInfo.append(&onScoreInfo);
}

void apiPlayerState(AbcUiPlayerState *out) {
  if (!out)
    return;
  AbcUiPlayerState state{};
  state.structSize = sizeof(AbcUiPlayerState);
  watchScoreInfo();
  state.frags = localFrags();
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (local) {
    state.index = local->index;
    const float *velocity = local->curstate.velocity;
    state.speed =
        std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    state.origin[0] = local->curstate.origin[0];
    state.origin[1] = local->curstate.origin[1];
    state.origin[2] = local->curstate.origin[2];
    state.messageNum = local->curstate.messagenum;
    gEngfuncs.GetViewAngles(state.angles);
  }
  abi::copySnapshot(out, state);
}

int32_t apiModelIndex(const char *path) {
  if (!path || !*path || !gEngfuncs.pEventAPI)
    return 0;
  return gEngfuncs.pEventAPI->EV_FindModelIndex(path);
}

void apiViewHeight(float out[3]) {
  if (!out)
    return;
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  if (gEngfuncs.pEventAPI)
    gEngfuncs.pEventAPI->EV_LocalPlayerViewheight(out);
}

int32_t apiThirdPerson() {
  return gExportfuncs.CL_IsThirdPerson() ? 1 : 0;
}

int32_t apiTraceEndPos(int32_t type, float out[3]) {
  if (!out)
    return 0;
  playertrace::TraceType traceType = playertrace::TraceType::View;
  switch (type) {
  case ABCUI_TRACE_HEAD:
    traceType = playertrace::TraceType::Head;
    break;
  case ABCUI_TRACE_FOOT:
    traceType = playertrace::TraceType::Foot;
    break;
  default:
    traceType = playertrace::TraceType::View;
    break;
  }
  const pmtrace_t *trace = playertrace::get(traceType);
  if (!trace)
    return 0;
  out[0] = trace->endpos[0];
  out[1] = trace->endpos[1];
  out[2] = trace->endpos[2];
  return 1;
}

void apiServerInfo(char *serverName, uint32_t serverNameSize, char *nextMap,
                   uint32_t nextMapSize, int32_t *timeEnd) {
  const hudstate::MessageSnapshot &messages = hudstate::messages();
  if (serverName && serverNameSize > 0)
    copyOut(messages.ServerName, serverName, serverNameSize);
  if (nextMap && nextMapSize > 0)
    copyOut(messages.NextMap, nextMap, nextMapSize);
  if (timeEnd)
    *timeEnd = messages.TimeEnd;
}

int apiLevelName(char *out, uint32_t outSize) {
  const char *name = gEngfuncs.pfnGetLevelName();
  const std::string text = name ? name : "";
  if (out && outSize > 0)
    copyOut(text, out, outSize);
  return static_cast<int>(text.size());
}

int32_t apiEnsureSayTextFirst() {
  return netmsg::ensureSayTextFirst();
}

const AbcUiDataApi kDataApi = {
    sizeof(AbcUiDataApi), apiHudState,    apiLevelName,
    apiPlayerState,       apiThirdPerson, apiTraceEndPos,
    apiModelIndex,        apiViewHeight,  apiServerInfo,
    apiEnsureSayTextFirst,
};

} // namespace

const AbcUiDataApi *get() {
  return &kDataApi;
}

} // namespace DataApi
} // namespace framework
