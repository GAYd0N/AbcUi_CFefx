#pragma once
#include <RmlUi/Core/Element.h>
#include <string>
#include <unordered_map>
namespace framework {
// Per-panel cache; forget before removing nodes so recycled addresses cannot
// inherit another node's state. Never read back/serialize DOM to compare text.
class DomState {
  std::unordered_map<Rml::Element *,
                     std::unordered_map<std::string, std::string>>
      Values;

public:
  void clear() {
    Values.clear();
  }
  void forget(Rml::Element *e) {
    Values.erase(e);
  }
  void text(Rml::Element *e, const std::string &value) {
    if (changed(e, "#text", value))
      e->SetInnerRML(value);
  }
  void property(Rml::Element *e, const std::string &key,
                const std::string &value) {
    if (changed(e, key, value))
      e->SetProperty(key, value);
  }
  void attribute(Rml::Element *e, const std::string &key,
                 const std::string &value) {
    if (changed(e, "@" + key, value))
      e->SetAttribute(key, value);
  }

private:
  bool changed(Rml::Element *e, const std::string &key,
               const std::string &value) {
    if (!e)
      return false;
    auto &values = Values[e];
    auto it = values.find(key);
    if (it != values.end() && it->second == value)
      return false;
    values[key] = value;
    return true;
  }
};
} // namespace framework
