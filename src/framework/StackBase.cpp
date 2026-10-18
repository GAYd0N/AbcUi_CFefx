// framework::StackBase implementation - see StackBase.h.
// from: ABCEnchance/src/hud/vgui/basestackpanel.* (value-semantic port).
#include "StackBase.h"
#include "../data/HudState.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

#include <abc/Console.h>

namespace framework {

StackBase::~StackBase() = default;

void StackBase::initStack(Rml::ElementDocument *document,
                          const char *containerId) {
  Container = document ? document->GetElementById(containerId) : nullptr;
  if (!Container)
    abc::ConsoleWarn("[AbcUi] stack container \"%s\" missing\n", containerId);
}

Rml::Element *StackBase::appendItem(const char *itemClass,
                                    const char *itemRml) {
  if (!Container)
    return nullptr;
  Rml::ElementDocument *document = Container->GetOwnerDocument();
  Rml::ElementPtr holder = document->CreateElement("div");
  if (!holder)
    return nullptr;
  Rml::Element *item = holder.get();
  item->SetClass(itemClass, true);
  item->SetInnerRML(itemRml);
  // Start the old Show() state: alpha 0, the fade-in runs on the next
  // style computation (transition to opacity 1 in tickItems).
  item->SetProperty("opacity", "0");
  Container->AppendChild(std::move(holder));

  Item entry;
  entry.element = item;
  entry.expireTime = clientTimeNow + StackParams.keepTime;
  Items.push_back(entry);

  while (Items.size() >
         static_cast<size_t>((std::max)(1, StackParams.maxItems))) {
    Container->RemoveChild(Items.front().element);
    Items.erase(Items.begin());
  }
  return item;
}

void StackBase::tickItems(double frameTime) {
  clientTimeNow += frameTime;
  if (Container && Container->GetOwnerDocument()) {
    if (hudstate::isHudHidden(hudstate::kHideAll))
      Container->GetOwnerDocument()->Hide();
    else if (!Items.empty() && !Container->GetOwnerDocument()->IsVisible())
      Container->GetOwnerDocument()->Show(Rml::ModalFlag::None,
                                          Rml::FocusFlag::None);
  }
  if (Items.empty())
    return;
  for (auto itor = Items.begin(); itor != Items.end();) {
    Item &item = *itor;
    if (item.fadeInPending) {
      item.fadeInPending = false;
      item.element->SetProperty(
          "transition",
          "opacity " + std::to_string(StackParams.fadeIn) + "s linear-in");
      item.element->SetProperty("opacity", "1");
    }
    if (item.fadeStartTime < 0.0 && clientTimeNow >= item.expireTime) {
      item.fadeStartTime = clientTimeNow;
      item.element->SetProperty(
          "transition",
          "opacity " + std::to_string(StackParams.fadeOut) + "s linear-in");
      item.element->SetProperty("opacity", "0");
    }
    if (item.fadeStartTime >= 0.0 &&
        clientTimeNow >= item.fadeStartTime + StackParams.fadeOut) {
      Container->RemoveChild(item.element);
      itor = Items.erase(itor);
    } else {
      ++itor;
    }
  }
}

void StackBase::clearItems() {
  for (Item &item : Items)
    Container->RemoveChild(item.element);
  Items.clear();
}

} // namespace framework
