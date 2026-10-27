// services::settings::VideoPendingStore implementation - see the header.
#include "VideoPendingStore.h"
#include "../UserConfig.h"

#include <abc/Console.h>

namespace services {
namespace settings {

namespace {
constexpr const char *kSection = "video_pending";
}

bool VideoPendingStore::save(const VideoPendingRecord &record) {
  VideoPendingJson json;
  json.Pending = record.Pending;
  json.NewWidth = record.NewWidth;
  json.NewHeight = record.NewHeight;
  json.NewWindowed = record.NewWindowed;
  json.OldWidth = record.OldWidth;
  json.OldHeight = record.OldHeight;
  json.OldWindowed = record.OldWindowed;
  json.AppliedUnixMs = record.AppliedUnixMs;
  json.Generation = record.Generation;
  if (!userconfig::save(json, kSection)) {
    abc::ConsoleWarn("[AbcUi] could not persist the video pending record\n");
    return false;
  }
  return true;
}

bool VideoPendingStore::load(VideoPendingRecord &out) {
  VideoPendingJson json;
  const auto status = userconfig::load(json, kSection);
  if (status == userconfig::LoadStatus::Missing)
    return false;
  if (status != userconfig::LoadStatus::Loaded) {
    // Unreadable record: fail closed into "no record" (the model then
    // keeps the engine's own registry state untouched).
    abc::ConsoleWarn("[AbcUi] video pending record unreadable (%d)\n",
                     static_cast<int>(status));
    return false;
  }
  out.Pending = json.Pending;
  out.NewWidth = json.NewWidth;
  out.NewHeight = json.NewHeight;
  out.NewWindowed = json.NewWindowed;
  out.OldWidth = json.OldWidth;
  out.OldHeight = json.OldHeight;
  out.OldWindowed = json.OldWindowed;
  out.AppliedUnixMs = json.AppliedUnixMs;
  out.Generation = json.Generation;
  return true;
}

} // namespace settings
} // namespace services
