// The projection signature: FNV-1a over the server-browser facts the page
// renders. Kept in its own translation unit so the page's render path and its
// change detection never drift apart into one 1100-line function soup.

#include "../shared/PageShared.h"

#include "ProjectionSignature.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace serverbrowserpage {
namespace {

void mix(std::size_t &hash, const void *data, std::size_t size) {
  const unsigned char *bytes = static_cast<const unsigned char *>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
}
template <typename T> void mixValue(std::size_t &hash, const T &value) {
  mix(hash, &value, sizeof(T));
}
void mixText(std::size_t &hash, const std::string &text) {
  mix(hash, text.data(), text.size());
}

} // namespace

std::size_t projectionSignature(bool detailsOpen, bool menuOpen,
                                bool autoRetryJoin) {
  std::size_t hash = 2166136261u;
  mixValue(hash, Api->serverbrowser->status());
  mixValue(hash, Api->serverbrowser->failure());
  mixValue(hash, Api->serverbrowser->kind());
  mixValue(hash, Api->serverbrowser->generation());
  mixValue(hash, Api->serverbrowser->serverCount());
  mixValue(hash, Api->serverbrowser->noServersListed());
  mixValue(hash, Api->serverbrowser->detailKind());
  mixValue(hash, Api->serverbrowser->detailStatus());
  mixValue(hash, Api->serverbrowser->detailFailed());
  mixValue(hash, Api->serverbrowser->playerCount());
  mixValue(hash, Api->serverbrowser->rowCount());
  mixValue(hash, Api->serverbrowser->page());
  mixValue(hash, Api->serverbrowser->pageCount());
  mixValue(hash, Api->serverbrowser->sortColumn());
  mixValue(hash, Api->serverbrowser->sortAscending());
  mixValue(hash, Api->serverbrowser->blockedOnly());
  mixValue(hash, Api->serverbrowser->ruleRevision());
  mixValue(hash, detailsOpen);
  mixValue(hash, menuOpen);
  mixValue(hash, autoRetryJoin);
  AbcUiSbFilter filter{};
  filter.structSize = sizeof(filter);
  Api->serverbrowser->filter(&filter);
  mixValue(hash, filter.hideEmpty);
  mixValue(hash, filter.hideFull);
  mixValue(hash, filter.secureOnly);
  mixText(hash, filter.gameDir);
  mixText(hash, filter.name);
  mixText(hash, filter.map);
  mixValue(hash, filter.maxPing);
  mixValue(hash, filter.noPassword);
  mixValue(hash, filter.insecureOnly);
  mixValue(hash, filter.hideUnresponsive);
  for (int row = 0; row < ABCUI_SB_PAGE_SIZE; ++row) {
    AbcUiSbEntry entry;
    entry.structSize = sizeof(entry);
    if (!Api->serverbrowser->entryAt(row, &entry))
      break;
    mixValue(hash, row);
    mixText(hash, entry.name);
    mixText(hash, entry.map);
    mixValue(hash, entry.players);
    mixValue(hash, entry.maxPlayers);
    mixValue(hash, entry.pingMs);
    mixValue(hash, entry.responded);
    mixValue(hash, entry.secure);
    mixValue(hash, entry.password);
    mixValue(hash, entry.botPlayers);
  }
  for (int i = 0; i < Api->serverbrowser->playerCount(); ++i) {
    AbcUiSbPlayer player;
    player.structSize = sizeof(player);
    if (!Api->serverbrowser->playerAt(i, &player))
      continue;
    mixText(hash, player.name);
    mixValue(hash, player.score);
  }
  // The selected server's favorite state is read-through (Steam is the one
  // truth); it must move the signature so a change re-projects even when
  // the list did not move.
  mixValue(hash, Api->serverbrowser->selectedFavorite());
  return hash;
}

} // namespace serverbrowserpage
