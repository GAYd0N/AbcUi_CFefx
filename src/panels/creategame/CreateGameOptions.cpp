#include "CreateGamePage.h"
#include "framework/TextUtil.h"
#include "framework/Localization.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <cstdio>

namespace panels::creategame {
namespace {
std::string number(float Value) {
  char Text[48];
  std::snprintf(Text, sizeof(Text), "%.9g", static_cast<double>(Value));
  return Text;
}
std::string label(const std::string &Text) {
  std::string Result;
  if (!Text.empty() && Text[0] == '#' &&
      framework::Localization::translate(Result, Text.substr(1)))
    return Result;
  return Text;
}
} // namespace
void CreateGamePage::buildOptions() {
  if (!Model || !node("cg-options"))
    return;
  std::string Markup;
  for (const auto &Option : Model->options()) {
    // One row per option on the page's 96dp label grid (the typed rows
    // above use the same column), so every control lines up.
    Markup += "<div class=\"cg-row\"><div class=\"cg-row-label\">" +
              framework::TextUtil::escapeRml(label(Option.Label)) + "</div>";
    const std::string Id = "cg-option-" + Option.Name;
    if (Option.Toggle || !Option.Choices.empty())
      Markup += "<select class=\"cg-opt-select\" id=\"" + Id + "\"></select>";
    else {
      const bool Slider = Option.Bounded && Option.Min < Option.Max;
      Markup += "<input id=\"" + Id + "\" class=\"" +
                (Slider ? "cg-slider cg-opt-slider" : "cg-opt-field") +
                "\" type=\"" + (Slider ? "range" : "text") + "\" value=\"" +
                number(Option.Default) + "\"";
      if (Slider)
        Markup += " min=\"" + number(Option.Min) + "\" max=\"" +
                  number(Option.Max) + "\" step=\"1\"";
      Markup += "/><span class=\"row-value cg-opt-value\" id=\"" + Id +
                "-value\">" + number(Option.Default) + "</span>";
    }
    Markup += "</div>";
  }
  Rml::Element *Host = node("cg-options");
  Host->SetInnerRML(Markup);
  // An empty option set must not leave the block's divider hanging over
  // nothing (the shipped script always fills it, a stripped install may not).
  Host->SetProperty("display", Markup.empty() ? "none" : "block");
  for (const auto &Option : Model->options()) {
    auto *Select = rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(
        node(("cg-option-" + Option.Name).c_str()));
    if (!Select)
      continue;
    int Selected = 0;
    if (Option.Toggle) {
      Select->Add(framework::TextUtil::escapeRml(label("#Settings_Off")), "0");
      Select->Add(framework::TextUtil::escapeRml(label("#Settings_On")), "1");
      Selected = Option.Default > 0 ? 1 : 0;
    } else {
      for (size_t I = 0; I < Option.Choices.size(); ++I) {
        const auto &Choice = Option.Choices[I];
        Select->Add(framework::TextUtil::escapeRml(label(Choice.Label)),
                    number(Choice.Value));
        if (Choice.Value == Option.Default)
          Selected = static_cast<int>(I);
      }
    }
    Select->SetSelection(Selected);
  }
  if (auto *Button = node("cg-lan"))
    Button->SetInnerRML(label("#Settings_Off"));
}
std::vector<services::creategame::ServerOptionValue>
CreateGamePage::optionValues() const {
  std::vector<services::creategame::ServerOptionValue> Values;
  if (Model)
    for (const auto &Option : Model->options())
      if (auto *Control = rmlui_dynamic_cast<Rml::ElementFormControl *>(
              node(("cg-option-" + Option.Name).c_str())))
        Values.push_back({Option.Name, Control->GetValue()});
  return Values;
}
void CreateGamePage::handleChange(Rml::Event &Event) {
  auto *Target = Event.GetTargetElement();
  if (!Target)
    return;
  if (Target->GetId() == "cg-map-filter") {
    filterMaps(Event.GetParameter<Rml::String>("value", ""));
    return;
  }
  const auto Id = Target->GetId() + "-value";
  if (auto *Value = node(Id.c_str()))
    Value->SetInnerRML(framework::TextUtil::escapeRml(
        Event.GetParameter<Rml::String>("value", "")));
}
} // namespace panels::creategame
