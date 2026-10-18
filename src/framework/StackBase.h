#pragma once

// framework::StackBase - shared logic of the three pickup stack panels
// (AmmoStack/ItemStack/WeaponStack). Ported from ABCEnchance
// src/hud/vgui/basestackpanel.* - the vgui EditablePanel/CBaseSprStackItem
// pair becomes Rml child elements: newest appended at the container's bottom
// (flex column-reverse == the old bottom-up PerformLayout), each item
// expiring after keepTime with the old fade-out.
//
// Stack panels keep their document permanently visible: an empty transparent
// container draws nothing (the old panel only painted with items present).

#include <RmlUi/Core/ElementDocument.h>

#include <vector>

namespace framework {

class StackBase {
public:
  virtual ~StackBase();

protected:
  // Grabs the container element; call from onDocumentReady.
  void initStack(Rml::ElementDocument *document, const char *containerId);

  // Appends an item: a <div class=itemClass> wrapping <itemRml>. Drops the
  // oldest beyond maxItems (the old CalculateMaxItemCount capacity).
  Rml::Element *appendItem(const char *itemClass, const char *itemRml);

  // Expiry + fade bookkeeping; call from onData.
  void tickItems(double frameTime);

  void clearItems();

  // The scroll container (children are the live stack items).
  Rml::Element *stackContainer() const {
    return Container;
  }

  // Old CBaseStackPanel ApplySettings values; each panel pins its own from
  // its .res (keep/fade times, item box, capacity).
  struct ItemParams {
    float keepTime = 2.0f;
    float fadeIn = 0.1f;
    float fadeOut = 0.3f;
    int maxItems = 10;
  } StackParams;

private:
  struct Item {
    Rml::Element *element = nullptr;
    double expireTime = 0.0;     // client time the keep phase ends
    double fadeStartTime = -1.0; // client time the fade-out started
    bool fadeInPending = true;   // opacity flips to 1 on the next tick
  };
  double clientTimeNow = 0.0;
  Rml::Element *Container = nullptr;
  std::vector<Item> Items;
};

} // namespace framework
