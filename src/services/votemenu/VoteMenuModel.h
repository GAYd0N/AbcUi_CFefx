#pragma once

// services::votemenu - the vote-creation menu model.
//
// This is the replacement for ABCEnchance's GameUI VoteMenuDialog: the four
// server vote pages (ban / kick / kill / map), their filters, selection and
// command construction. Engine I/O is injected as a roster seam; the model
// receives the server's MapList chunks from data::netmsg and stays free of
// Rml/VGUI/engine headers so the production rules link into offline tests.
//
// The page owns only presentation. A selected row is revalidated at submit
// time: a player who left (or a map that disappeared from the list) clears the
// action instead of retargeting it. Candidate strings come from the server or
// other players, so command arguments are rejected when they contain a
// console separator/control character - a name can never smuggle a second
// client command into `voteban`/`votemap`.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace services::votemenu {

enum class Kind {
  kBan,
  kKick,
  kKill,
  kMap,
};

const char *kindId(Kind value);
bool parseKind(std::string_view text, Kind &out);

// One occupied player slot as the roster seam reports it. A value copy, not
// an alias into playerres.
struct PlayerCandidate {
  int slot = 0;
  std::string name;
  std::string realName;
  std::uint64_t steamId64 = 0;
  bool local = false;
};

// One row currently visible on the selected vote page.
struct Candidate {
  Kind kind = Kind::kBan;
  int slot = 0;         // players only
  std::string name;     // game name / map token
  std::string realName; // Steam persona, when available
  std::string steamId;  // decimal SteamID64, when available
  std::string value;    // command argument (name / map token)
};

class VoteMenuModel {
public:
  struct Seams {
    // Fills `out` with the live occupied roster, returns the count.
    int (*roster)(PlayerCandidate *out, int max) = nullptr;
  };

  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }

  // Fresh opening: Ban page, empty filter, no selection, live roster.
  // The server map list is retained until the next MapList clear.
  void prepare();

  // Re-reads the roster. Returns true when the projected rows changed.
  bool refresh();

  // MapList user message. clearAll == 0 starts a new list (allCount is the
  // advertised total); any other value appends the received chunk. The old
  // implementation ignored `start` for storage and appended every chunk, so
  // the replacement keeps that ordering while deduplicating repeated names.
  void applyMapList(int clearAll, int allCount, int start, int end,
                    const std::vector<std::string> &maps);
  void clearMapList();

  Kind kind() const {
    return Kind_;
  }
  void setKind(Kind value);

  const std::string &filter() const {
    return Filter_;
  }
  void setFilter(std::string value);

  const std::vector<Candidate> &visible() const {
    return Visible;
  }
  void select(std::size_t visibleIndex);
  const Candidate *selected() const;

  // Called only after the user presses the primary action. Re-reads the
  // live roster, revalidates the selection and returns the exact server
  // command. On refusal `errorToken` is a localization token (or text).
  bool submitCommand(std::string &command, const char *&errorToken);

  std::size_t revision() const {
    return Revision;
  }
  std::size_t playerCount() const {
    return Players.size();
  }
  std::size_t mapCount() const {
    return Maps.size();
  }

private:
  struct Selection {
    Kind kind = Kind::kBan;
    int slot = 0;
    std::string value;
  };

  void rebuild();
  void clearSelection();
  bool selectionMatches(const Candidate &candidate) const;
  static bool sameRoster(const std::vector<PlayerCandidate> &a,
                         const std::vector<PlayerCandidate> &b);
  static bool safeCommandArgument(const std::string &value);

  Seams Seams_;
  std::vector<PlayerCandidate> Players;
  std::vector<std::string> Maps;
  std::vector<Candidate> Visible;
  Selection Selected;
  Kind Kind_ = Kind::kBan;
  std::string Filter_;
  std::size_t Revision = 0;
  bool Primed = false;
};

} // namespace services::votemenu
