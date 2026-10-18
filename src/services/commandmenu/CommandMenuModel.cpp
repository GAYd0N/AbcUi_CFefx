#include "CommandMenuModel.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>

namespace services::commandmenu {
namespace {

using Value = rapidjson::Value;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr unsigned kMaxItemsPerMenu = 100;
constexpr unsigned kMaxDepth = 40;
constexpr std::size_t kMaxNodes = 2048;
constexpr unsigned kMaxMapsPerItem = 64;

struct ValidationError {
  std::string Field;
  std::string Message;
};

struct ParseContext {
  std::size_t Nodes = 0;
};

void require(bool valid, const std::string &field, const char *message) {
  if (!valid)
    throw ValidationError{field, message};
}

std::string lowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

std::string normalizeMapName(std::string name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  const std::size_t slash = name.find_last_of('/');
  if (slash != std::string::npos)
    name.erase(0, slash + 1);
  if (name.size() >= 4 && lowerAscii(name.substr(name.size() - 4)) == ".bsp")
    name.resize(name.size() - 4);
  return lowerAscii(std::move(name));
}

bool mapToken(const std::string &token) {
  if (token.empty() || token.size() > 96)
    return false;
  return std::all_of(token.begin(), token.end(), [](unsigned char ch) {
    return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
  });
}

void fields(const Value &object, std::initializer_list<const char *> allowed,
            const std::string &path) {
  require(object.IsObject(), path, "expected object");
  std::set<std::string> seen;
  for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
    std::string key(it->name.GetString(), it->name.GetStringLength());
    require(seen.insert(key).second, path + "." + key, "duplicate field");
    require(std::any_of(allowed.begin(), allowed.end(),
                        [&](const char *name) { return key == name; }),
            path + "." + key, "unknown field");
  }
}

const Value &member(const Value &object, const char *key,
                    const std::string &path) {
  require(object.HasMember(key), path + "." + key, "required field missing");
  return object[key];
}

std::string text(const Value &object, const char *key, const std::string &path,
                 std::size_t limit, bool emptyAllowed = false) {
  const Value &value = member(object, key, path);
  require(value.IsString(), path + "." + key, "expected string");
  std::string result(value.GetString(), value.GetStringLength());
  require((emptyAllowed || !result.empty()) && result.size() <= limit &&
              result.find('\0') == std::string::npos,
          path + "." + key, "invalid string length or NUL");
  require(
      std::all_of(result.begin(), result.end(),
                  [](unsigned char ch) { return ch >= 0x20 && ch != 0x7f; }),
      path + "." + key, "control characters are not allowed");
  return result;
}

std::vector<std::string> maps(const Value &object, const std::string &path) {
  std::vector<std::string> result;
  if (!object.HasMember("maps"))
    return result;
  const Value &values = object["maps"];
  require(values.IsArray(), path + ".maps", "expected array");
  require(values.Size() > 0 && values.Size() <= kMaxMapsPerItem, path + ".maps",
          "invalid map filter size");
  std::set<std::string> seen;
  for (unsigned i = 0; i < values.Size(); ++i) {
    const std::string itemPath = path + ".maps[" + std::to_string(i) + "]";
    require(values[i].IsString(), itemPath, "expected string");
    std::string value(values[i].GetString(), values[i].GetStringLength());
    require(mapToken(value), itemPath, "invalid map token");
    value = normalizeMapName(std::move(value));
    require(!value.empty() && seen.insert(value).second, itemPath,
            "duplicate map token");
    result.push_back(std::move(value));
  }
  return result;
}

Item item(const Value &value, const std::string &path, unsigned depth,
          ParseContext &context) {
  require(depth <= kMaxDepth, path, "menu nesting is too deep");
  fields(value, {"label", "command", "items", "maps"}, path);

  Item result;
  result.Label = text(value, "label", path, 512);
  result.Maps = maps(value, path);
  require(!value.HasMember("command") || !value.HasMember("items"), path,
          "an item cannot declare both command and items");

  if (value.HasMember("items")) {
    const Value &children = value["items"];
    require(children.IsArray(), path + ".items", "expected array");
    require(children.Size() > 0 && children.Size() <= kMaxItemsPerMenu,
            path + ".items", "invalid item count");
    for (unsigned i = 0; i < children.Size(); ++i) {
      Item child = item(children[i], path + ".items[" + std::to_string(i) + "]",
                        depth + 1, context);
      result.Items.push_back(std::move(child));
    }
  } else {
    result.Command = text(value, "command", path, 4096);
  }

  require(result.hasItems() || !result.Command.empty(), path,
          "item requires command or items");
  ++context.Nodes;
  require(context.Nodes <= kMaxNodes, path, "too many menu items");
  return result;
}

} // namespace

bool DefinitionParser::parse(std::string_view json, Definition &output,
                             ParseError &error) {
  try {
    require(json.size() <= kMaxFileBytes, "$", "file exceeds 1 MiB");
    require(json.find('\0') == std::string_view::npos, "$",
            "unexpected NUL byte");

    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag |
                   rapidjson::kParseIterativeFlag>(json.data(), json.size());
    if (document.HasParseError())
      throw ValidationError{
          "$@" + std::to_string(document.GetErrorOffset()),
          rapidjson::GetParseError_En(document.GetParseError())};

    fields(document, {"version", "title", "items"}, "$");
    const Value &version = member(document, "version", "$");
    require(version.IsInt() && version.GetInt() == 1, "$.version",
            "unsupported version");

    Definition result;
    if (document.HasMember("title"))
      result.Title = text(document, "title", "$", 512, true);

    const Value &items = member(document, "items", "$");
    require(items.IsArray(), "$.items", "expected array");
    require(items.Size() > 0 && items.Size() <= kMaxItemsPerMenu, "$.items",
            "invalid root item count");
    ParseContext context;
    for (unsigned i = 0; i < items.Size(); ++i) {
      Item entry =
          item(items[i], "$.items[" + std::to_string(i) + "]", 1, context);
      result.Items.push_back(std::move(entry));
    }
    output = std::move(result);
    return true;
  } catch (const ValidationError &failure) {
    error.Field = failure.Field;
    error.Message = failure.Message;
    return false;
  }
}

void Model::setDefinition(Definition definition) {
  Definition_ = std::move(definition);
  HasDefinition = true;
  close();
}

void Model::open(const std::string &mapName) {
  MapName = normalizeMapName(mapName);
  Open = true;
  Depth = 1;
  Selected.assign(1, 0);
  rebuild();
  changed();
}

void Model::close() {
  Open = false;
  MapName.clear();
  Depth = 0;
  Selected.clear();
  Columns.clear();
  changed();
}

const std::vector<const Item *> &Model::itemsAt(std::size_t column) const {
  static const std::vector<const Item *> empty;
  return column < Columns.size() ? Columns[column] : empty;
}

std::size_t Model::selectedAt(std::size_t column) const {
  return column < Selected.size() ? Selected[column] : 0;
}

std::size_t Model::firstVisibleColumn(std::size_t windowSize) const {
  if (windowSize == 0 || Columns.size() <= windowSize)
    return 0;
  return Columns.size() - windowSize;
}

std::size_t Model::visibleColumnCount(std::size_t windowSize) const {
  const std::size_t start = firstVisibleColumn(windowSize);
  return Columns.size() - start;
}

bool Model::mapMatches(const Item &item) const {
  if (item.Maps.empty())
    return true;
  return std::any_of(item.Maps.begin(), item.Maps.end(),
                     [&](const std::string &map) { return map == MapName; });
}

bool Model::itemVisible(const Item &item) const {
  if (!mapMatches(item))
    return false;
  if (!item.hasItems())
    return true;
  return std::any_of(item.Items.begin(), item.Items.end(),
                     [&](const Item &child) { return itemVisible(child); });
}

const std::vector<Item> &Model::sourceFor(std::size_t column) const {
  if (column == 0 || column > Columns.size())
    return Definition_.Items;
  const std::size_t parentIndex = Selected[column - 1];
  const auto &parentItems = Columns[column - 1];
  if (parentIndex >= parentItems.size())
    return Definition_.Items;
  return parentItems[parentIndex]->Items;
}

void Model::rebuild() {
  Columns.clear();
  if (!Open || !HasDefinition)
    return;

  if (Selected.empty())
    Selected.assign(1, 0);
  Depth = std::max<std::size_t>(Depth, 1);
  Selected.resize(Depth, 0);

  for (std::size_t column = 0; column < Depth; ++column) {
    std::vector<const Item *> visible;
    for (const Item &candidate : sourceFor(column)) {
      if (itemVisible(candidate))
        visible.push_back(&candidate);
    }
    if (visible.empty())
      break;
    Columns.push_back(std::move(visible));
    Selected[column] = std::min(Selected[column], Columns.back().size() - 1);
  }

  if (Columns.empty()) {
    Depth = 1;
    Selected.assign(1, 0);
  } else {
    Depth = Columns.size();
    Selected.resize(Depth);
  }
}

bool Model::moveSelection(int delta) {
  if (Columns.empty() || delta == 0)
    return false;
  const std::size_t column = Columns.size() - 1;
  const std::size_t before = Selected[column];
  if (delta < 0) {
    const std::size_t amount = static_cast<std::size_t>(-delta);
    Selected[column] = before > amount ? before - amount : 0;
  } else {
    const std::size_t amount = static_cast<std::size_t>(delta);
    const std::size_t remaining = Columns[column].size() - 1 - before;
    Selected[column] = before + std::min(amount, remaining);
  }
  if (Selected[column] == before)
    return false;
  rebuild();
  changed();
  return true;
}

ActionResult Model::activate(const Item &selected, std::size_t column,
                             std::size_t index) {
  if (selected.hasItems()) {
    Selected.resize(column + 1);
    Selected[column] = index;
    Depth = column + 2;
    Selected.push_back(0);
    rebuild();
    changed();
    return {ActionResult::Kind::kSubmenu, {}};
  }
  if (!selected.Command.empty())
    return {ActionResult::Kind::kCommand, selected.Command};
  return {};
}

ActionResult Model::openSelected() {
  if (Columns.empty())
    return {};
  const std::size_t column = Columns.size() - 1;
  const std::size_t index = Selected[column];
  if (index >= Columns[column].size() || !Columns[column][index]->hasItems())
    return {};
  return activate(*Columns[column][index], column, index);
}

ActionResult Model::activateSelected() {
  if (Columns.empty())
    return {};
  const std::size_t column = Columns.size() - 1;
  const std::size_t index = Selected[column];
  if (index >= Columns[column].size())
    return {};
  return activate(*Columns[column][index], column, index);
}

ActionResult Model::activateAt(std::size_t column, std::size_t index) {
  if (column >= Columns.size() || index >= Columns[column].size())
    return {};
  Selected.resize(column + 1);
  Selected[column] = index;
  Depth = column + 1;
  return activate(*Columns[column][index], column, index);
}

ActionResult Model::activateShortcut(char shortcut) {
  if (Columns.empty())
    return {};
  const std::size_t column = Columns.size() - 1;
  if (shortcut < '0' || shortcut > '9')
    return {};
  const std::size_t position =
      shortcut == '0' ? 10 : static_cast<std::size_t>(shortcut - '0');
  if (position == 0 || position > Columns[column].size())
    return {};
  const std::size_t index = position - 1;
  Selected[column] = index;
  return activate(*Columns[column][index], column, index);
}

ActionResult Model::back() {
  if (!Open)
    return {};
  if (Columns.empty() || Columns.size() <= 1) {
    return {ActionResult::Kind::kClose, {}};
  }
  Depth = Columns.size() - 1;
  Selected.resize(Depth);
  rebuild();
  changed();
  return {};
}

} // namespace services::commandmenu
