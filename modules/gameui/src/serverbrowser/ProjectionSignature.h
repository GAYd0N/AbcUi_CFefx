#pragma once

// serverbrowserpage::projectionSignature - the page's change detector: one
// order-sensitive FNV-1a hash over every framework fact the projection reads
// (status, list page, filters, detail roster, favorite state) plus the page's
// own sub-surface flags. menu.paint re-projects only when it moves, so the
// page never rebuilds its DOM on an unchanged beat.

#include <cstddef>

namespace serverbrowserpage {

std::size_t projectionSignature(bool detailsOpen, bool menuOpen,
                                bool autoRetryJoin);

} // namespace serverbrowserpage
