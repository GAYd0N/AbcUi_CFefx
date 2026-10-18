// rmlui::backdrop::ElementBackdrop implementation - see ElementBackdrop.h.
#include "ElementBackdrop.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "Backdrop.h"
#include "../../framework/AssetPaths.h"
#include "../../framework/MenuBackdrop.h"

namespace rmlui::backdrop {

namespace {

std::vector<ElementBackdrop *> &elements() {
  static std::vector<ElementBackdrop *> Elements;
  return Elements;
}

Rml::ElementInstancerGeneric<ElementBackdrop> Instancer;

// The framework seam speaks void(*)(ElementDocument*); the concrete helper
// returns the element for direct callers.
void attachForSeam(Rml::ElementDocument *document) {
  attachToDocument(document);
}

void setPixel(Rml::Element *element, const char *property, float value) {
  char text[32];
  _snprintf_s(text, sizeof(text), _TRUNCATE, "%.1fpx", value);
  element->SetProperty(property, text);
}

} // namespace

ElementBackdrop::ElementBackdrop(const Rml::String &Tag) : Element(Tag) {
  SetProperty("position", "absolute");
  SetProperty("left", "0px");
  SetProperty("top", "0px");
  SetProperty("overflow", "hidden");
  SetProperty("pointer-events", "none");

  auto image = Rml::Factory::InstanceElement(this, "img", "img", {});
  Image = image.get();
  if (Image) {
    Image->SetProperty("position", "absolute");
    Image->SetProperty("left", "0px");
    Image->SetProperty("top", "0px");
    Image->SetProperty("display", "block");
    Image->SetProperty("pointer-events", "none");
    Image->SetProperty("visibility", "hidden");
    AppendChild(std::move(image));
  }
  auto dimmer = Rml::Factory::InstanceElement(this, "div", "div", {});
  Dimmer = dimmer.get();
  if (Dimmer) {
    Dimmer->SetProperty("position", "absolute");
    Dimmer->SetProperty("left", "0px");
    Dimmer->SetProperty("top", "0px");
    Dimmer->SetProperty("background-color", "black");
    Dimmer->SetProperty("pointer-events", "none");
    AppendChild(std::move(dimmer));
  }
  elements().push_back(this);
}

ElementBackdrop::~ElementBackdrop() {
  std::erase(elements(), this);
}

void ElementBackdrop::OnAttributeChange(const Rml::ElementAttributes &Changed) {
  Element::OnAttributeChange(Changed);
  menubackdrop::Mode mode = menubackdrop::Mode::kAuto;
  // -1 mirrors "no explicit blur attribute": the cvar then owns the value
  // (and keeps owning it while the user drags the settings slider).
  int rounds = -1;
  float dim = 0.35f;
  bool hasMode = false;
  bool hasBlur = false;
  for (const auto &[Name, Value] : GetAttributes()) {
    const Rml::String text = Value.Get<Rml::String>();
    if (Name == "mode") {
      hasMode = true;
      if (text == "live")
        mode = menubackdrop::Mode::kLive;
      else if (text == "video")
        mode = menubackdrop::Mode::kVideo;
      else if (text == "solid")
        mode = menubackdrop::Mode::kSolid;
      else if (text != "auto")
        abc::ConsoleWarn("[AbcUi backdrop] unknown mode '%s' on #%s\n",
                         text.c_str(), GetId().c_str());
    } else if (Name == "blur") {
      hasBlur = true;
      const int value = std::clamp(atoi(text.c_str()), 0, 4);
      rounds = value;
    } else if (Name == "dim") {
      dim = std::clamp(static_cast<float>(atof(text.c_str())), 0.0f, 1.0f);
    }
  }
  // Only explicit attributes steer the shared backdrop (the cvars own the
  // defaults); a document without them never resets another page's choice.
  if (hasMode)
    menubackdrop::menuBackdrop().setMode(mode);
  if (hasBlur)
    menubackdrop::menuBackdrop().setBlurRounds(rounds);
  Rounds = rounds;
  Dim = dim;
}

bool ElementBackdrop::sync() {
  if (!Image || !Dimmer)
    return false;
  // Operator overrides: the shipped menu movie and the blur strength are
  // cvars (abc_ui_menu_video / abc_ui_menu_backdrop_blur); an explicit
  // attribute still wins per document.
  if (cvar_t *source = abc::CVAR_GET_POINTER("abc_ui_menu_video")) {
    if (source->string && AppliedVideoCvar != source->string) {
      AppliedVideoCvar =
          framework::assets::remapLegacyMenuMedia(source->string);
      menubackdrop::menuBackdrop().setVideoSource(AppliedVideoCvar);
    }
  }
  if (cvar_t *cvarRounds = abc::CVAR_GET_POINTER("abc_ui_menu_backdrop_blur")) {
    const int rounds = std::clamp(static_cast<int>(cvarRounds->value), 0, 4);
    if (Rounds < 0 && AppliedCvarRounds != rounds) {
      AppliedCvarRounds = rounds;
      menubackdrop::menuBackdrop().setBlurRounds(rounds);
    }
  }
  // Size from the context, not the parent: the backdrop must cover the window
  // whatever the document's own box turned out to be.
  Rml::Context *context = GetContext();
  if (!context)
    return false;
  const Rml::Vector2i dimensions = context->GetDimensions();
  if (dimensions.x <= 0 || dimensions.y <= 0)
    return false;
  if (static_cast<int>(GetOffsetWidth()) != dimensions.x ||
      static_cast<int>(GetOffsetHeight()) != dimensions.y) {
    setPixel(this, "width", static_cast<float>(dimensions.x));
    setPixel(this, "height", static_cast<float>(dimensions.y));
  }
  // The image is stretched to the window: the blur removed the detail that
  // an aspect-correct fit would preserve, and stretch keeps the shader's
  // screen-space orientation (the capture was already screen shaped).
  if (static_cast<int>(Image->GetOffsetWidth()) != dimensions.x ||
      static_cast<int>(Image->GetOffsetHeight()) != dimensions.y) {
    setPixel(Image, "width", static_cast<float>(dimensions.x));
    setPixel(Image, "height", static_cast<float>(dimensions.y));
  }
  if (static_cast<int>(Dimmer->GetOffsetWidth()) != dimensions.x ||
      static_cast<int>(Dimmer->GetOffsetHeight()) != dimensions.y) {
    setPixel(Dimmer, "width", static_cast<float>(dimensions.x));
    setPixel(Dimmer, "height", static_cast<float>(dimensions.y));
  }
  if (AppliedDim != Dim) {
    AppliedDim = Dim;
    char text[32];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%.2f", Dim);
    Dimmer->SetProperty("opacity", text);
  }

  const std::string &uri = menubackdrop::menuBackdrop().uri();
  bool changed = false;
  if (uri.empty()) {
    if (Image->GetAttribute<Rml::String>("src", "") != "") {
      Image->SetAttribute("src", "");
      changed = true;
    }
    if (Image->GetProperty<Rml::String>("visibility") != "hidden") {
      Image->SetProperty("visibility", "hidden");
      changed = true;
    }
    return changed;
  }
  if (Image->GetAttribute<Rml::String>("src", "") != uri) {
    Image->SetAttribute("src", uri);
    changed = true;
  }
  if (Image->GetProperty<Rml::String>("visibility") != "visible") {
    Image->SetProperty("visibility", "visible");
    changed = true;
  }
  return changed;
}

void ElementBackdrop::dropImageSource() {
  if (!Image)
    return;
  Image->SetAttribute("src", "");
  Image->SetProperty("visibility", "hidden");
}

void registerElement() {
  Rml::Factory::RegisterElementInstancer("backdrop", &Instancer);
  // Menu documents declare "this page wants a backdrop" through the
  // engine-free framework seam; this layer is the only one that knows how
  // to build one.
  framework::menubackdrop::setAttacher(&attachForSeam);
}

bool anyVisible() {
  for (auto *element : elements())
    if (element->IsVisible(true))
      return true;
  return false;
}

ProbeInfo probeInfo() {
  ProbeInfo info;
  info.Elements = static_cast<int>(elements().size());
  for (auto *element : elements()) {
    if (element->IsVisible(true)) {
      info.Visible++;
      if (info.Visible == 1)
        element->fillProbeInfo(info);
    }
  }
  info.Published = !menubackdrop::menuBackdrop().uri().empty();
  info.Uri = menubackdrop::menuBackdrop().uri().c_str();
  return info;
}

void ElementBackdrop::fillProbeInfo(ProbeInfo &info) {
  Rml::ElementDocument *document = GetOwnerDocument();
  info.Document = document ? document->GetId() : Rml::String();
  info.Width = GetOffsetWidth();
  info.Height = GetOffsetHeight();
  if (!Image)
    return;
  info.ImageWidth = Image->GetOffsetWidth();
  info.ImageHeight = Image->GetOffsetHeight();
  info.ImageSrc = Image->GetAttribute<Rml::String>("src", "");
  info.ImageVisibility = Image->GetProperty<Rml::String>("visibility");
}

bool syncAll() {
  bool changed = false;
  for (auto *element : elements())
    if (element->sync())
      changed = true;
  return changed;
}

void invalidateTargets() {
  menubackdrop::menuBackdrop().invalidateTargets();
  for (auto *element : elements())
    element->dropImageSource();
}

Rml::Element *attachToDocument(Rml::ElementDocument *document) {
  if (!document)
    return nullptr;
  if (Rml::Element *existing = document->GetElementById("menu-backdrop"))
    return existing;
  Rml::ElementPtr element = document->CreateElement("backdrop");
  if (!element)
    return nullptr;
  Rml::Element *node = element.get();
  node->SetId("menu-backdrop");
  Rml::Element *first =
      document->GetNumChildren() > 0 ? document->GetChild(0) : nullptr;
  // Insert as the first child: paint order follows DOM order, so the backdrop
  // lands behind every page node whatever their stacking settings are.
  if (first)
    document->InsertBefore(std::move(element), first);
  else
    document->AppendChild(std::move(element));
  return node;
}

} // namespace rmlui::backdrop
