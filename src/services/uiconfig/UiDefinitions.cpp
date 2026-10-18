#include "UiDefinitions.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace services::uiconfig {
namespace {
using Value = rapidjson::Value;

struct ValidationError {
  std::string Field;
  std::string Message;
};

void require(bool Valid, const std::string &Field, const char *Message) {
  if (!Valid)
    throw ValidationError{Field, Message};
}

bool identifier(const std::string &Text) {
  return !Text.empty() && Text.size() <= 96 &&
         std::all_of(Text.begin(), Text.end(), [](unsigned char C) {
           return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
                  (C >= '0' && C <= '9') || C == '_' || C == '-';
         });
}

void fields(const Value &Object, std::initializer_list<const char *> Allowed,
            const std::string &Path) {
  require(Object.IsObject(), Path, "expected object");
  std::set<std::string> Seen;
  for (auto It = Object.MemberBegin(); It != Object.MemberEnd(); ++It) {
    std::string Key(It->name.GetString(), It->name.GetStringLength());
    require(Seen.insert(Key).second, Path + "." + Key, "duplicate field");
    require(std::any_of(Allowed.begin(), Allowed.end(),
                        [&](const char *Name) { return Key == Name; }),
            Path + "." + Key, "unknown field");
  }
}

const Value &member(const Value &Object, const char *Key,
                    const std::string &Path) {
  require(Object.HasMember(Key), Path + "." + Key, "required field missing");
  return Object[Key];
}

std::string text(const Value &Object, const char *Key, const std::string &Path,
                 bool Empty = false) {
  const auto &V = member(Object, Key, Path);
  require(V.IsString(), Path + "." + Key, "expected string");
  std::string Result(V.GetString(), V.GetStringLength());
  require((Empty || !Result.empty()) && Result.size() <= 1024 &&
              Result.find('\0') == std::string::npos,
          Path + "." + Key, "invalid string length or NUL");
  return Result;
}

float number(const Value &Object, const char *Key, const std::string &Path) {
  const auto &V = member(Object, Key, Path);
  require(V.IsNumber(), Path + "." + Key, "expected finite number");
  float Result = static_cast<float>(V.GetDouble());
  require(std::isfinite(Result), Path + "." + Key,
          "number exceeds float range");
  return Result;
}

void array(const Value &V, const std::string &Path, unsigned Limit) {
  require(V.IsArray(), Path, "expected array");
  require(V.Size() <= Limit, Path, "too many entries");
}

rapidjson::Document document(const std::string &Text) {
  require(Text.size() <= 1024 * 1024, "$", "file exceeds 1 MiB");
  require(Text.find('\0') == std::string::npos, "$", "unexpected NUL byte");
  rapidjson::Document Doc;
  Doc.Parse<rapidjson::kParseValidateEncodingFlag |
            rapidjson::kParseIterativeFlag>(Text.data(), Text.size());
  if (Doc.HasParseError())
    throw ValidationError{"$@" + std::to_string(Doc.GetErrorOffset()),
                          rapidjson::GetParseError_En(Doc.GetParseError())};
  require(Doc.IsObject(), "$", "expected object");
  const auto &Version = member(Doc, "version", "$");
  require(Version.IsInt() && Version.GetInt() == 1, "$.version",
          "unsupported version");
  return Doc;
}

std::vector<MenuEntry> menuEntries(const Value &V, const std::string &Path,
                                   std::set<std::string> &Ids) {
  array(V, Path, 64);
  std::vector<MenuEntry> Entries;
  for (unsigned I = 0; I < V.Size(); ++I) {
    const std::string P = Path + "[" + std::to_string(I) + "]";
    const auto &Item = V[I];
    fields(Item, {"id", "label", "action", "condition"}, P);
    MenuEntry Entry;
    Entry.Id = text(Item, "id", P);
    Entry.Label = text(Item, "label", P);
    Entry.Action = text(Item, "action", P);
    require(identifier(Entry.Id) && Ids.insert(Entry.Id).second, P + ".id",
            "invalid or duplicate id");
    // Only home-menu intents, never dialog confirmations or commands.
    const std::set<std::string> Actions = {
        "resume",     "disconnect", "quit",     "settings", "servers",
        "creategame", "playerlist", "votemenu", "console",  "directconnect"};
    require(Actions.count(Entry.Action) != 0, P + ".action",
            "unregistered home-menu action");
    if (Item.HasMember("condition"))
      Entry.Condition = text(Item, "condition", P);
    require(Entry.Condition == "always" || Entry.Condition == "multiplayer" ||
                Entry.Condition == "singleplayer",
            P + ".condition", "unknown visibility condition");
    Entries.push_back(std::move(Entry));
  }
  require(!Entries.empty(), Path, "section must not be empty");
  return Entries;
}

SettingDefinition setting(const Value &Item, const std::string &P) {
  fields(Item,
         {"cvar", "label", "section", "kind", "min", "max", "step", "default",
          "options", "source", "requires", "requiresEnabled", "readOnly",
          "hue"},
         P);
  SettingDefinition Row;
  Row.Cvar = text(Item, "cvar", P);
  require(identifier(Row.Cvar), P + ".cvar", "invalid cvar identifier");
  if (Item.HasMember("requires")) {
    Row.RequireCvar = text(Item, "requires", P);
    require(identifier(Row.RequireCvar), P + ".requires",
            "invalid cvar identifier");
  }
  for (const auto *Key : {"readOnly", "requiresEnabled"})
    if (Item.HasMember(Key))
      require(Item[Key].IsBool(), P + "." + Key, "expected boolean");
  Row.ReadOnly = Item.HasMember("readOnly") && Item["readOnly"].GetBool();
  if (Item.HasMember("hue")) {
    require(Item["hue"].IsBool(), P + ".hue", "expected boolean");
    Row.HueSwatch = Item["hue"].GetBool();
  }
  Row.RequireEnabled =
      Item.HasMember("requiresEnabled") && Item["requiresEnabled"].GetBool();
  require(!Row.RequireEnabled || !Row.RequireCvar.empty(),
          P + ".requiresEnabled", "requires a dependency");
  Row.Label = text(Item, "label", P);
  Row.Section = Item.HasMember("section") ? text(Item, "section", P) : "";
  const auto Kind = text(Item, "kind", P);
  using K = settings::SettingKind;
  if (Kind == "bool")
    Row.Kind = K::kBool;
  else if (Kind == "float")
    Row.Kind = K::kFloat;
  else if (Kind == "enum")
    Row.Kind = K::kEnum;
  else if (Kind == "negate")
    Row.Kind = K::kNegate;
  else if (Kind == "text")
    Row.Kind = K::kText;
  else if (Kind == "string-enum")
    Row.Kind = K::kStringEnum;
  else if (Kind == "command-toggle")
    Row.Kind = K::kCommandToggle;
  else
    require(false, P + ".kind", "unknown setting kind");
  const bool String = Row.Kind == K::kText || Row.Kind == K::kStringEnum;
  Row.HasDefault = Item.HasMember("default");
  if (String || Row.Kind == K::kBool || Row.Kind == K::kCommandToggle)
    require(!Item.HasMember("min") && !Item.HasMember("max") &&
                !Item.HasMember("step"),
            P, "min/max/step are only valid for float, enum and negate");
  if (String) {
    Row.DefaultString = Row.HasDefault ? text(Item, "default", P, true) : "";
    require(std::all_of(Row.DefaultString.begin(), Row.DefaultString.end(),
                        [](unsigned char C) {
                          return C >= 0x20 && C != 0x7f && C != '"' &&
                                 C != '\\' && C != '`' && C != ';';
                        }),
            P + ".default", "unsafe string default");
    if (Row.Kind == K::kText && Row.HasDefault)
      require(!Row.DefaultString.empty() &&
                  Row.DefaultString.size() <= settings::kNameMaxBytes,
              P + ".default", "text default must be 1..63 bytes");
    Row.Min = Row.Max = Row.Step = 0;
  } else {
    Row.DefaultValue = Row.HasDefault ? number(Item, "default", P) : 0;
    Row.Bounded =
        Row.Kind != K::kFloat || Item.HasMember("min") || Item.HasMember("max");
    if (!Row.Bounded) {
      Row.Min = Row.Max = Row.Step = 0;
    } else if (Row.Kind != K::kBool && Row.Kind != K::kCommandToggle) {
      Row.Min = number(Item, "min", P);
      Row.Max = number(Item, "max", P);
      Row.Step = Item.HasMember("step") ? number(Item, "step", P) : 0;
    }
    require(Row.Min <= Row.Max && Row.Step >= 0, P, "invalid numeric domain");
    if (!Row.HasDefault)
      Row.DefaultValue = Row.Min;
    require(!Row.Bounded ||
                (Row.DefaultValue >= Row.Min && Row.DefaultValue <= Row.Max),
            P + ".default", "default outside domain");
    if (Row.Kind == K::kBool || Row.Kind == K::kCommandToggle)
      require(Row.DefaultValue == 0 || Row.DefaultValue == 1, P + ".default",
              "expected 0 or 1");
  }
  // The hue thumb swatch reads the dragged value directly, so it only
  // exists on the bounded float control the page actually draws a slider
  // for.
  require(!Row.HueSwatch || (Row.Kind == K::kFloat && Row.Bounded), P + ".hue",
          "hue requires a bounded float row");
  if (Row.Kind == K::kCommandToggle)
    require(Row.Cvar == "in_mlook" || Row.Cvar == "in_jlook", P + ".cvar",
            "unregistered command-toggle adapter");
  if (Item.HasMember("source")) {
    Row.Source = text(Item, "source", P);
    require(Row.Kind == K::kStringEnum &&
                (Row.Source == "models/player" || Row.Source == "logos" ||
                 Row.Source == "logocolor" || Row.Source == "scence/video" ||
                 Row.Source == "scence/audio"),
            P + ".source", "unknown string option provider");
    // File providers scan a directory for media files; their choices are
    // paths labelled by file stem (see SettingDefinition::FileChoices).
    Row.FileChoices =
        Row.Source == "scence/video" || Row.Source == "scence/audio";
  }
  if (Item.HasMember("options")) {
    const auto &Options = Item["options"];
    array(Options, P + ".options", 256);
    require(Row.Kind == K::kEnum, P + ".options",
            "options require numeric enum");
    std::set<float> Values;
    for (unsigned I = 0; I < Options.Size(); ++I) {
      const std::string OP = P + ".options[" + std::to_string(I) + "]";
      fields(Options[I], {"value", "label"}, OP);
      float V = number(Options[I], "value", OP);
      require(V >= Row.Min && V <= Row.Max && Values.insert(V).second,
              OP + ".value", "duplicate or out-of-domain option");
      Row.Options.push_back({V, text(Options[I], "label", OP)});
    }
    require(!Row.HasDefault || Values.count(Row.DefaultValue) != 0,
            P + ".default", "default is not an option");
  }
  require(Row.Kind != K::kEnum || !Row.Options.empty(), P + ".options",
          "enum requires options");
  require(Row.Kind != K::kStringEnum || !Row.Source.empty(), P + ".source",
          "string-enum requires option provider");
  return Row;
}

std::string componentName(const Value &V, const std::string &Path,
                          bool PageLevel) {
  require(V.IsString(), Path, "expected component name");
  std::string Name(V.GetString(), V.GetStringLength());
  // Page-level components are whole-page behaviours; everything visual
  // belongs to the section that shows it (page -> sections -> widgets).
  const std::set<std::string> PageComponents = {"user-script"};
  const std::set<std::string> SectionComponents = {
      "bindings",          "video",         "voice",      "model-preview",
      "crosshair-preview", "spray-preview", "spray-maker"};
  const auto &Allowed = PageLevel ? PageComponents : SectionComponents;
  require(Allowed.count(Name) != 0, Path,
          PageLevel
              ? "unknown page component (visual components belong to a section)"
              : "unknown section component");
  return Name;
}

SectionDefinition section(const Value &Item, const std::string &P) {
  fields(Item, {"id", "label", "components"}, P);
  SectionDefinition Result;
  Result.Id = text(Item, "id", P);
  require(identifier(Result.Id), P + ".id", "invalid section identifier");
  Result.Label = text(Item, "label", P);
  if (Item.HasMember("components")) {
    array(Item["components"], P + ".components", 8);
    for (const auto &C : Item["components"].GetArray()) {
      std::string Name = componentName(C, P + ".components", false);
      require(std::find(Result.Components.begin(), Result.Components.end(),
                        Name) == Result.Components.end(),
              P + ".components", "duplicate component");
      Result.Components.push_back(std::move(Name));
    }
  }
  return Result;
}
} // namespace

bool SectionDefinition::hasComponent(const char *Name) const {
  return std::find(Components.begin(), Components.end(), Name) !=
         Components.end();
}

bool SettingsPageDefinition::hasComponent(const char *Name) const {
  return std::find(Components.begin(), Components.end(), Name) !=
         Components.end();
}

const SectionDefinition *
SettingsPageDefinition::sectionHaving(const char *Name) const {
  for (const auto &Entry : Sections)
    if (Entry.hasComponent(Name))
      return &Entry;
  return nullptr;
}

const SectionDefinition *
SettingsPageDefinition::section(const std::string &Id) const {
  for (const auto &Entry : Sections)
    if (Entry.Id == Id)
      return &Entry;
  return nullptr;
}

std::string SettingsPageDefinition::sectionLabel(const std::string &Id) const {
  const SectionDefinition *Declared = section(Id);
  return Declared ? Declared->Label : Id;
}

bool DefinitionParser::parseMenu(const std::string &Text,
                                 MenuDefinition &Output, ConfigError &Error) {
  try {
    auto Doc = document(Text);
    fields(Doc, {"version", "title", "main", "pause"}, "$");
    MenuDefinition Result;
    Result.Title = text(Doc, "title", "$");
    std::set<std::string> Ids = {"mainmenu",     "panel",     "title",
                                 "focus-status", "page-main", "page-pause"};
    Result.Main = menuEntries(member(Doc, "main", "$"), "$.main", Ids);
    Result.Pause = menuEntries(member(Doc, "pause", "$"), "$.pause", Ids);
    auto HasExit = [](const auto &Entries, const char *Action) {
      return std::any_of(
          Entries.begin(), Entries.end(), [&](const auto &Entry) {
            return Entry.Action == Action && Entry.Condition == "always";
          });
    };
    require(HasExit(Result.Main, "quit"), "$.main",
            "unconditional quit entry required");
    require(HasExit(Result.Pause, "quit") && HasExit(Result.Pause, "resume"),
            "$.pause", "unconditional resume and quit entries required");
    Output = std::move(Result);
    return true;
  } catch (const ValidationError &E) {
    Error.Field = E.Field;
    Error.Message = E.Message;
    return false;
  }
}

bool DefinitionParser::parsePage(const std::string &Text,
                                 SettingsPageDefinition &Output,
                                 ConfigError &Error) {
  try {
    auto Doc = document(Text);
    fields(Doc,
           {"version", "id", "label", "description", "order", "components",
            "sections", "scriptSection", "items"},
           "$");
    SettingsPageDefinition Result;
    Result.Id = text(Doc, "id", "$");
    require(identifier(Result.Id), "$.id", "invalid page identifier");
    Result.Label = text(Doc, "label", "$");
    if (Doc.HasMember("description"))
      Result.Description = text(Doc, "description", "$");
    const auto &Order = member(Doc, "order", "$");
    require(Order.IsInt(), "$.order", "expected integer");
    Result.Order = Order.GetInt();
    if (Doc.HasMember("components")) {
      const auto &Components = Doc["components"];
      array(Components, "$.components", 5);
      for (const auto &C : Components.GetArray()) {
        std::string Name = componentName(C, "$.components", true);
        require(!Result.hasComponent(Name.c_str()), "$.components",
                "duplicate component");
        Result.Components.push_back(std::move(Name));
      }
    }
    if (Doc.HasMember("sections")) {
      const auto &Sections = Doc["sections"];
      array(Sections, "$.sections", 64);
      require(Sections.Size() > 0, "$.sections", "sections must not be empty");
      std::set<std::string> Ids;
      for (unsigned I = 0; I < Sections.Size(); ++I) {
        auto Entry =
            section(Sections[I], "$.sections[" + std::to_string(I) + "]");
        require(Ids.insert(Entry.Id).second,
                "$.sections[" + std::to_string(I) + "].id",
                "duplicate section id");
        Result.Sections.push_back(std::move(Entry));
      }
    }
    if (Doc.HasMember("scriptSection")) {
      Result.ScriptSection = text(Doc, "scriptSection", "$");
      require(identifier(Result.ScriptSection) &&
                  Result.section(Result.ScriptSection) != nullptr,
              "$.scriptSection", "scriptSection must name a declared section");
    }
    require(!Result.hasComponent("user-script") ||
                !Result.ScriptSection.empty(),
            "$.scriptSection",
            "a user-script page must declare its script target section");
    const auto &Items = member(Doc, "items", "$");
    array(Items, "$.items", 512);
    std::set<std::string> Cvars;
    for (unsigned I = 0; I < Items.Size(); ++I) {
      auto Row = setting(Items[I], "$.items[" + std::to_string(I) + "]");
      require(Cvars.insert(Row.Cvar).second,
              "$.items[" + std::to_string(I) + "].cvar", "duplicate cvar");
      // With declared sections the item's `section` is an id: every
      // item must land in one, and it must be a declared one. Without
      // declarations the old free-form grouping label is preserved.
      if (!Result.Sections.empty() && Row.Section.empty())
        require(false, "$.items[" + std::to_string(I) + "].section",
                "item must declare a section (the page declares sections)");
      if (!Result.Sections.empty())
        require(Result.section(Row.Section) != nullptr,
                "$.items[" + std::to_string(I) + "].section",
                "unknown section id");
      Result.Items.push_back(std::move(Row));
    }
    Output = std::move(Result);
    return true;
  } catch (const ValidationError &E) {
    Error.Field = E.Field;
    Error.Message = E.Message;
    return false;
  }
}

MenuDefinition DefinitionParser::fallbackMenu() {
  MenuDefinition Result;
  Result.Main = {{"main-quit", "#MainMenu_Quit", "quit"}};
  Result.Pause = {{"pause-resume", "#MainMenu_Resume", "resume"},
                  {"pause-quit", "#MainMenu_Quit", "quit"}};
  return Result;
}
} // namespace services::uiconfig
