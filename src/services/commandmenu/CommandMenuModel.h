#pragma once

// services::commandmenu - data and navigation model for the in-game command
// menu. The parser and navigation rules are engine/Rml-free so the shipped
// JSON and every transition can be exercised by the offline test binary.
//
// Schema (version 1):
//   {
//     "version": 1,
//     "title": "#CommandMenu_Title",
//     "items": [
//       { "label": "Commands", "items": [ ... ] },
//       { "label": "Mission Briefing", "command": "missionbriefing" },
//       { "label": "Map only", "command": "status", "maps": ["co1a"] }
//     ]
//   }
//
// An item is either a leaf (`command`) or a submenu (`items`), never both.
// Array order defines the one-based shortcut number shown in the left gutter;
// keys 1-9 activate the corresponding item directly (0 activates the tenth).
// `maps` optionally limits an item to the named BSPs (case-insensitive,
// path/extension omitted).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace services::commandmenu {

struct Item {
  std::string Label;
  std::string Command;
  std::vector<std::string> Maps;
  std::vector<Item> Items;

  bool hasItems() const {
    return !Items.empty();
  }
};

struct Definition {
  std::string Title = "#CommandMenu_Title";
  std::vector<Item> Items;
};

struct ParseError {
  std::string Field;
  std::string Message;
};

class DefinitionParser {
public:
  // Pure JSON parsing/validation. Output is replaced only on success.
  static bool parse(std::string_view text, Definition &output,
                    ParseError &error);
};

struct ActionResult {
  enum class Kind {
    kNone,
    kSubmenu,
    kCommand,
    kClose,
  };

  Kind kind = Kind::kNone;
  std::string command;
};

class Model {
public:
  void setDefinition(Definition definition);
  const Definition &definition() const {
    return Definition_;
  }
  bool hasDefinition() const {
    return HasDefinition;
  }

  // Opens at the root for a map. `mapName` accepts the engine's level name
  // spelling ("maps/co1a.bsp"); matching uses the basename without .bsp.
  void open(const std::string &mapName);
  void close();
  bool isOpen() const {
    return Open;
  }

  // Open columns, left to right. Column i is the child list selected by the
  // item chosen in column i-1. Every vector is recomputed after navigation.
  std::size_t columnCount() const {
    return Columns.size();
  }
  const std::vector<const Item *> &itemsAt(std::size_t column) const;
  std::size_t selectedAt(std::size_t column) const;
  std::size_t firstVisibleColumn(std::size_t windowSize) const;
  std::size_t visibleColumnCount(std::size_t windowSize) const;

  // Changes the selection in the deepest column. Returns true when it moved.
  bool moveSelection(int delta);
  // Opens the selected submenu; a leaf is executed by activateSelected().
  ActionResult openSelected();
  ActionResult activateSelected();
  ActionResult activateAt(std::size_t column, std::size_t index);
  ActionResult activateShortcut(char shortcut);
  // Pops one open column, or returns kClose when already at the root.
  ActionResult back();

  std::size_t revision() const {
    return Revision;
  }
  const std::string &mapName() const {
    return MapName;
  }

private:
  bool itemVisible(const Item &item) const;
  bool mapMatches(const Item &item) const;
  const std::vector<Item> &sourceFor(std::size_t column) const;
  void rebuild();
  void changed() {
    ++Revision;
  }
  ActionResult activate(const Item &item, std::size_t column,
                        std::size_t index);

  Definition Definition_;
  bool HasDefinition = false;
  bool Open = false;
  std::string MapName;
  std::size_t Depth = 0;
  std::vector<std::size_t> Selected;
  std::vector<std::vector<const Item *>> Columns;
  std::size_t Revision = 0;
};

} // namespace services::commandmenu
