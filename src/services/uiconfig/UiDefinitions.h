#pragma once

#include "services/settings/SettingsModel.h"

#include <string>
#include <vector>

namespace services::uiconfig {

struct ConfigError {
  std::string File;
  std::string Field;
  std::string Message;
};

struct MenuEntry {
  std::string Id;
  std::string Label;
  std::string Action;
  std::string Condition = "always";
};

struct MenuDefinition {
  std::string Title;
  std::vector<MenuEntry> Main;
  std::vector<MenuEntry> Pause;
};

struct OptionDefinition {
  float Value = 0;
  std::string Label;
};

// One declared section of a settings page: the big row the page projects as a
// card. Items attach to it through their `section` id; `components` name the
// special widgets rendered inside the card (model preview, crosshair preview,
// spray preview / maker, the bind list and the video/voice hardware blocks).
// Components are business models the plugin already implements - the schema
// only decides where they appear.
struct SectionDefinition {
  std::string Id;
  std::string Label;
  std::vector<std::string> Components;
  // Optional row layout: "two-up" pairs the section's rows in two columns
  // and puts the widget band above them (the compact crosshair card).
  // Empty is the default single-column row flow.
  std::string Layout;

  bool hasComponent(const char *Name) const;
};

// Own every string; materialize the legacy transaction model's pointer view
// only after loading/sorting has finished. Definitions outlive that view.
struct SettingDefinition {
  std::string Cvar;
  std::string Label;
  std::string Section;
  std::string DefaultString;
  std::string Source;
  std::string RequireCvar;
  bool RequireEnabled = false;
  bool ReadOnly = false;
  // The value is a player-palette hue (topcolor / bottomcolor): the page
  // shows the represented colour on the slider thumb.
  bool HueSwatch = false;
  // The string-enum scan yields FILE paths (menu video / music): the page
  // labels the dropdown with the file stem and the write validation accepts
  // a relative path.
  bool FileChoices = false;
  bool Bounded = true;
  bool HasDefault = true;
  settings::SettingKind Kind = settings::SettingKind::kBool;
  float Min = 0;
  float Max = 1;
  float Step = 1;
  float DefaultValue = 0;
  std::vector<OptionDefinition> Options;
  std::vector<settings::SettingEnumOption> OptionViews;
};

struct SettingsPageDefinition {
  std::string Id;
  std::string Label;
  std::string Description;
  int Order = 0;
  std::vector<std::string> Components;
  std::vector<SectionDefinition> Sections;
  // Section that appended user.scr rows land in (the page's declared
  // script target); empty when the page does not merge script rows.
  std::string ScriptSection;
  std::vector<SettingDefinition> Items;
  std::vector<std::string> Cvars;

  bool hasComponent(const char *Name) const;
  // Section carrying a component (the widget's home card), or null.
  const SectionDefinition *sectionHaving(const char *Name) const;
  // Section by id, or null for an undeclared/legacy grouping token.
  const SectionDefinition *section(const std::string &Id) const;
  // Display label for an item's `section` token: the declared section's
  // label, else the raw token (legacy label grouping). By value: the raw
  // fallback is the caller's token, never storage this page owns.
  std::string sectionLabel(const std::string &Id) const;
};

// Pure parsing/validation: no filesystem, engine, UI, or logging side effects.
class DefinitionParser {
public:
  static bool parseMenu(const std::string &Text, MenuDefinition &Output,
                        ConfigError &Error);
  static bool parsePage(const std::string &Text, SettingsPageDefinition &Output,
                        ConfigError &Error);
};

} // namespace services::uiconfig
