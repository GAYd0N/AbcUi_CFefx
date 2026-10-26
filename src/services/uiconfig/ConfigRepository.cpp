#include "ConfigRepository.h"

#include <algorithm>

#include <set>

namespace services::uiconfig {
namespace {
// Existing fixed spray-color provider; not a menu/settings row definition.
constexpr settings::SettingEnumOption kLogoColorOptions[] = {
    {0.0f, "Orange"}, {1.0f, "Yellow"}, {2.0f, "Blue"},
    {3.0f, "Ltblue"}, {4.0f, "Green"},  {5.0f, "Red"},
    {6.0f, "Brown"},  {7.0f, "Ltgray"}, {8.0f, "Dkgray"},
};

SettingDefinition copyRow(const settings::SettingRow &Row) {
  SettingDefinition Result;
  Result.Cvar = Row.Cvar;
  Result.Label = Row.LabelToken ? Row.LabelToken : Row.Cvar;
  Result.Section = Row.Section ? Row.Section : "";
  Result.Kind = Row.Kind;
  Result.Min = Row.Min;
  Result.Max = Row.Max;
  Result.Step = Row.Step;
  Result.DefaultValue = Row.DefaultValue;
  Result.DefaultString = Row.DefaultString ? Row.DefaultString : "";
  Result.Bounded = Row.Bounded;
  Result.HasDefault = Row.HasDefault;
  Result.ReadOnly = Row.ReadOnly;
  Result.HueSwatch = Row.HueSwatch;
  Result.RequireCvar = Row.RequireCvar ? Row.RequireCvar : "";
  Result.RequireEnabled = Row.RequireEnabled;
  for (int I = 0; I < Row.OptionCount; ++I)
    Result.Options.push_back({Row.Options[I].Value, Row.Options[I].Label});
  return Result;
}
} // namespace

bool ConfigRepository::read(const std::string &Path, std::string &Text) {
  if (!Read || !Read(Path, Text) || Text.size() > 1024 * 1024) {
    Errors.push_back({Path, "$", "cannot read package entry or exceeds 1 MiB"});
    return false;
  }
  if (Text.starts_with("\xef\xbb\xbf"))
    Text.erase(0, 3);
  return true;
}

void ConfigRepository::load(const std::string &Root, Reader reader,
                            Listing listing,
                            const std::vector<settings::SettingRow> &Advanced) {
  Rows.clear();
  Pages.clear();
  Errors.clear();
  Read = std::move(reader);
  Menu = {};
  std::string Text;
  const auto MenuPath = Root + "/mainmenu.json";
  if (read(MenuPath, Text)) {
    ConfigError Error{MenuPath};
    if (!DefinitionParser::parseMenu(Text, Menu, Error))
      Errors.push_back(std::move(Error));
  }
  auto Files =
      listing ? listing(Root + "/settings") : std::vector<std::string>{};
  std::erase_if(
      Files, [](const std::string &file) { return !file.ends_with(".json"); });
  std::sort(Files.begin(), Files.end());
  if (Files.size() > 64) {
    Errors.push_back({Root, "$", "settings directory exceeds 64 JSON files"});
    Files.resize(64);
  }
  std::set<std::string> PageIds, Cvars, Components;
  for (const auto &File : Files) {
    if (!read(File, Text))
      continue;
    ConfigError Error{File};
    SettingsPageDefinition Page;
    if (!DefinitionParser::parsePage(Text, Page, Error)) {
      Errors.push_back(std::move(Error));
      continue;
    }
    // A live user.scr replaces only the fallback items of its declared
    // component page; other JSON pages remain authoritative.
    if (Page.hasComponent("user-script") && !Advanced.empty()) {
      // Preserve JSON-only extensions while replacing matching script rows.
      for (const auto &Row : Advanced) {
        auto Existing = std::find_if(
            Page.Items.begin(), Page.Items.end(),
            [&](const auto &Item) { return Item.Cvar == Row.Cvar; });
        if (Existing != Page.Items.end()) {
          // The page definition owns the presentation: the script supplies
          // the registration data (kind, domain, default, options), never
          // the card a row lands in or its translated label. The shipped
          // user.scr rows carry the flat legacy section token and literal
          // prompts, so copying either used to move every overridden row
          // into an undeclared section and blank its card. The page's
          // behavioural flags (dependencies, read-only, hue swatch, file
          // choices) are not script concepts either and stay as declared.
          const SettingDefinition Presentation = *Existing;
          *Existing = copyRow(Row);
          Existing->Label = Presentation.Label;
          Existing->Section = Presentation.Section;
          Existing->RequireCvar = Presentation.RequireCvar;
          Existing->RequireEnabled = Presentation.RequireEnabled;
          Existing->ReadOnly = Presentation.ReadOnly;
          Existing->FileChoices = Presentation.FileChoices;
          Existing->Source = Presentation.Source;
          Existing->HueSwatch =
              Existing->Kind == settings::SettingKind::kFloat &&
              Existing->Bounded && Presentation.HueSwatch;
        } else {
          // Appended script rows land in the page's declared
          // script section, never in an undeclared group.
          SettingDefinition Appended = copyRow(Row);
          Appended.Section = Page.ScriptSection;
          Page.Items.push_back(std::move(Appended));
        }
      }
    }
    std::string Conflict;
    if (PageIds.count(Page.Id))
      Conflict = "duplicate page id: " + Page.Id;
    std::set<std::string> Local;
    for (const auto &Row : Page.Items) {
      if (Cvars.count(Row.Cvar) || !Local.insert(Row.Cvar).second)
        Conflict = "duplicate cvar: " + Row.Cvar;
    }
    // One component, one home: page-level behaviours plus every section
    // widget share one namespace across the whole catalog.
    std::set<std::string> PageComponents(Page.Components.begin(),
                                         Page.Components.end());
    for (const auto &Section : Page.Sections)
      PageComponents.insert(Section.Components.begin(),
                            Section.Components.end());
    for (const auto &Component : PageComponents) {
      if (Components.count(Component))
        Conflict = "component already assigned: " + Component;
    }
    if (!Conflict.empty()) {
      Errors.push_back({File, "$", Conflict});
      continue;
    }
    PageIds.insert(Page.Id);
    Cvars.insert(Local.begin(), Local.end());
    Components.insert(PageComponents.begin(), PageComponents.end());
    Pages.push_back(std::move(Page));
  }
  std::sort(Pages.begin(), Pages.end(), [](const auto &A, const auto &B) {
    return A.Order == B.Order ? A.Id < B.Id : A.Order < B.Order;
  });
  buildRows();
}

void ConfigRepository::buildRows() {
  for (auto &Page : Pages) {
    for (auto &Item : Page.Items) {
      Page.Cvars.push_back(Item.Cvar);
      for (const auto &Option : Item.Options)
        Item.OptionViews.push_back({Option.Value, Option.Label.c_str()});
      settings::SettingRow Row{Item.Cvar.c_str(),
                               Item.Kind,
                               Item.Min,
                               Item.Max,
                               Item.Step,
                               Item.DefaultValue,
                               Item.Label.c_str(),
                               Item.Section.c_str(),
                               Item.OptionViews.data(),
                               static_cast<int>(Item.OptionViews.size())};
      Row.DefaultString = Item.DefaultString.c_str();
      Row.Bounded = Item.Bounded;
      Row.ReadOnly = Item.ReadOnly;
      Row.RequireCvar =
          Item.RequireCvar.empty() ? nullptr : Item.RequireCvar.c_str();
      Row.RequireEnabled = Item.RequireEnabled;
      Row.HasDefault = Item.HasDefault;
      Row.HueSwatch = Item.HueSwatch;
      Row.FileChoices = Item.FileChoices;
      Row.AllowEmpty = Item.AllowEmpty;
      if (Item.Kind == settings::SettingKind::kCommandToggle) {
        Row.StateName = Item.Cvar.c_str();
        Row.OnCommand = Item.Cvar == "in_mlook" ? "+mlook" : "+jlook";
        Row.OffCommand = Item.Cvar == "in_mlook" ? "-mlook" : "-jlook";
      }
      if (Item.Kind == settings::SettingKind::kStringEnum) {
        Row.StateName = Item.Source.c_str();
        if (Item.Source == "logocolor") {
          Row.Options = kLogoColorOptions;
          Row.OptionCount = 9;
        }
      }
      Rows.push_back(Row);
    }
  }
}
} // namespace services::uiconfig
