// data::avatars implementation - see Avatars.h.
#include <metahook.h>

#include "Avatars.h"
#include <abc/Paths.h>
#include <abcui/module_api.h>

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "../../framework/ImageLoader.h"
#include "../../framework/ResourceLimits.h"
#include "../../hooks.h"
#include "../../services/steam/SteamAPI.h"
#include "../../framework/TextureSink.h"
#include "../animated/ApngDecoder.h"

namespace avatars {

namespace {

constexpr std::size_t kMaxAnimatedBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxEntries = 256;
constexpr char kAnimatedDirectory[] = "abc/abcui/avatarcache";
constexpr char kDiskMagic[8] = {'A', 'B', 'C', 'A', 'V', 'A', 'T', '1'};

struct Entry {
  std::vector<unsigned char> rgba; // uploaded/ready pixels
  int width = 0;
  int height = 0;
  int textureId = 0;  // 0 = not uploaded yet
  int generation = 0; // bumped on upload - the scoreboard cache-bust suffix
  double nextAttempt = 0.0; // steam retry throttle (old m_fNextLoadTime)

  // Animated (compressed APNG) cache.
  std::vector<std::byte> apng;
  int animGeneration = 0;
  bool diskTried = false;
  bool animFetchPending = false;
  double nextAnimAttempt = 0.0;
  double lastAnimUse = 0.0;
};

// Monotonic client clock for the throttles; frametime-independent.
double NowSeconds() {
  return static_cast<double>(GetTickCount64()) / 1000.0;
}

std::unordered_map<std::uint64_t, Entry> Entries;
std::size_t AnimatedBytes = 0;

bool upload(Entry &entry) {
  if (!framework::texture::available())
    return false;
  if (!entry.textureId)
    entry.textureId = framework::texture::create();
  if (!entry.textureId)
    return false;
  framework::texture::upload(entry.textureId, entry.rgba.data(), entry.width,
                             entry.height);
  return true;
}

// The steamapi fetch attempt behind both consumers (the old CAvatarImage
// 1s throttle, m_fNextLoadTime): GetSmallFriendAvatar returns 0 while steam
// is still downloading, callers re-ask until it lands. True when the RGBA
// uploaded this call.
bool tryFetch(Entry &entry, std::uint64_t steamId64, double now) {
  if (entry.textureId || now < entry.nextAttempt)
    return false;
  entry.nextAttempt = now + 1.0;
  if (entry.rgba.empty()) {
    steamapi::AvatarRgba avatar;
    if (!steamapi::avatar(steamId64, avatar) || !avatar.rgba)
      return false;
    size_t bytes;
    if (!framework::ResourceLimits::imageBytes(avatar.width, avatar.height,
                                               bytes))
      return false;
    entry.rgba.assign(avatar.rgba, avatar.rgba + bytes);
    entry.width = avatar.width;
    entry.height = avatar.height;
  }
  if (!upload(entry))
    return false;
  entry.generation++;
  return true;
}

std::string diskPath(std::uint64_t steamId64) {
  const std::string relative = std::string(kAnimatedDirectory) + "/" +
                               std::to_string(steamId64) + ".apng";
  return abc::ModPath(relative.c_str());
}

bool isAnimatedPngBytes(const std::vector<std::byte> &bytes) {
  return rmlui::animated::detail::isAnimatedPng(bytes);
}

// Returns the resident bytes of the versioned on-disk record. The header keeps
// a format tag so a future cache format (or a corrupted file) is ignored
// instead of being fed to the APNG backend.
bool readDisk(std::uint64_t steamId64, std::vector<std::byte> &out) {
  const std::string path = diskPath(steamId64);
  if (path.empty())
    return false;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const std::streamoff total = file.tellg();
  if (total < static_cast<std::streamoff>(sizeof(kDiskMagic) + 4) ||
      total > static_cast<std::streamoff>(sizeof(kDiskMagic) + 4 +
                                          kMaxAnimatedBytes))
    return false;
  file.seekg(0);
  char magic[sizeof(kDiskMagic)] = {};
  uint32_t size = 0;
  if (!file.read(magic, sizeof(magic)) ||
      !file.read(reinterpret_cast<char *>(&size), sizeof(size)))
    return false;
  if (std::memcmp(magic, kDiskMagic, sizeof(kDiskMagic)) != 0 || size == 0 ||
      size > kMaxAnimatedBytes ||
      total != static_cast<std::streamoff>(sizeof(kDiskMagic) + 4 + size))
    return false;
  out.resize(size);
  if (!file.read(reinterpret_cast<char *>(out.data()), size))
    return false;
  if (!isAnimatedPngBytes(out)) {
    out.clear();
    return false;
  }
  return true;
}

void writeDisk(std::uint64_t steamId64, const std::vector<std::byte> &bytes) {
  if (bytes.empty() || bytes.size() > kMaxAnimatedBytes ||
      !abc::EnsureDir(kAnimatedDirectory))
    return;
  const std::string path = diskPath(steamId64);
  if (path.empty())
    return;
  const std::string temporary = path + ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file)
      return;
    const uint32_t size = static_cast<uint32_t>(bytes.size());
    file.write(kDiskMagic, sizeof(kDiskMagic));
    file.write(reinterpret_cast<const char *>(&size), sizeof(size));
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file)
      return;
  }
  MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Drops the oldest animated payloads until `incoming` bytes fit the budget.
// Static RGBA entries stay untouched: the budget only covers compressed APNG.
void evictAnimated(std::size_t incoming, std::uint64_t keepId) {
  if (incoming > kMaxAnimatedBytes)
    return;
  while (AnimatedBytes + incoming > kMaxAnimatedBytes) {
    Entry *oldest = nullptr;
    for (auto &[id, entry] : Entries) {
      if (id == keepId || entry.apng.empty())
        continue;
      if (!oldest || entry.lastAnimUse < oldest->lastAnimUse)
        oldest = &entry;
    }
    if (!oldest)
      break;
    AnimatedBytes -= oldest->apng.size();
    oldest->apng.clear();
    oldest->animGeneration = 0;
  }
}

bool rememberAnimated(std::uint64_t steamId64, Entry &entry,
                      std::vector<std::byte> bytes) {
  if (bytes.empty() || bytes.size() > kMaxAnimatedBytes)
    return false;
  if (!entry.apng.empty())
    AnimatedBytes -= entry.apng.size();
  evictAnimated(bytes.size(), steamId64);
  entry.apng = std::move(bytes);
  AnimatedBytes += entry.apng.size();
  entry.animGeneration++;
  writeDisk(steamId64, entry.apng);
  return true;
}

} // namespace

void shutdown() {
  for (const auto &[id, entry] : Entries)
    if (entry.textureId)
      framework::texture::release(entry.textureId);
  Entries.clear();
  AnimatedBytes = 0;
}

int textureFor(std::uint64_t steamId64, int &width, int &height,
               bool triggerFetch) {
  if (!steamId64) {
    width = 0;
    height = 0;
    return 0;
  }

  if (!Entries.count(steamId64) && Entries.size() >= kMaxEntries) {
    width = 0;
    height = 0;
    return 0;
  }
  Entry &entry = Entries[steamId64];
  if (entry.textureId && !entry.rgba.empty()) {
    width = entry.width;
    height = entry.height;
    return entry.textureId;
  }

  // Retryable with the old 1s throttle (CAvatarImage m_fNextLoadTime):
  // GetSmallFriendAvatar returns 0 while steam is still fetching, and the
  // throttle keeps the per-refresh polling from hammering the interface.
  if (triggerFetch && tryFetch(entry, steamId64, NowSeconds())) {
    width = entry.width;
    height = entry.height;
    return entry.textureId;
  }

  width = 0;
  height = 0;
  return 0;
}

bool touch(std::uint64_t steamId64) {
  if (!steamId64 ||
      (!Entries.count(steamId64) && Entries.size() >= kMaxEntries))
    return false;
  return tryFetch(Entries[steamId64], steamId64, NowSeconds());
}

void onFrame() {
  const double now = NowSeconds();
  for (auto &[id, entry] : Entries) {
    tryFetch(entry, id, now);
    // A closed scoreboard stops calling animatedStatus; cancel its in-flight
    // download instead of keeping the request (and the body) alive.
    if (entry.animFetchPending && now - entry.lastAnimUse > 2.0) {
      steamapi::abandonAnimatedAvatar(id);
      entry.animFetchPending = false;
    }
  }
}

int status(std::uint64_t steamId64, int &generationOut) {
  generationOut = 0;
  if (!steamId64)
    return ABCUI_ERROR_NOT_FOUND;
  if (!steamapi::available())
    return ABCUI_ERROR_UNAVAILABLE;
  if (!Entries.count(steamId64) && Entries.size() >= kMaxEntries)
    return ABCUI_ERROR_LIMIT;
  touch(steamId64);
  const auto &entry = Entries.at(steamId64);
  if (entry.textureId) {
    generationOut = entry.generation;
    return ABCUI_OK;
  }
  return entry.rgba.empty() ? ABCUI_ERROR_PENDING : ABCUI_ERROR_UPLOAD;
}

int generation(std::uint64_t steamId64) {
  const auto itor = Entries.find(steamId64);
  return itor == Entries.end() ? 0 : itor->second.generation;
}

int animatedStatus(std::uint64_t steamId64, int &generationOut) {
  generationOut = 0;
  if (!steamId64)
    return ABCUI_ERROR_NOT_FOUND;
  if (!steamapi::available())
    return ABCUI_ERROR_UNAVAILABLE;
  if (!Entries.count(steamId64) && Entries.size() >= kMaxEntries)
    return ABCUI_ERROR_LIMIT;

  Entry &entry = Entries[steamId64];
  const double now = NowSeconds();
  entry.lastAnimUse = now;

  // Try the on-disk mirror once per id before asking Steam.
  if (entry.apng.empty() && !entry.diskTried) {
    entry.diskTried = true;
    std::vector<std::byte> cached;
    if (readDisk(steamId64, cached) &&
        rememberAnimated(steamId64, entry, std::move(cached))) {
      generationOut = entry.animGeneration;
      return ABCUI_OK;
    }
  }
  if (!entry.apng.empty()) {
    generationOut = entry.animGeneration;
    return ABCUI_OK;
  }

  if (now < entry.nextAnimAttempt)
    return ABCUI_ERROR_PENDING;

  steamapi::AvatarAnimation animation;
  const steamapi::AvatarAnimationState state =
      steamapi::animatedAvatar(steamId64, animation);
  switch (state) {
  case steamapi::AvatarAnimationState::Ready: {
    if (!animation.bytes || animation.size == 0 ||
        animation.size > kMaxAnimatedBytes) {
      entry.nextAnimAttempt = now + 300.0;
      return ABCUI_ERROR_NOT_FOUND;
    }
    std::vector<std::byte> bytes(
        reinterpret_cast<const std::byte *>(animation.bytes),
        reinterpret_cast<const std::byte *>(animation.bytes) + animation.size);
    if (!isAnimatedPngBytes(bytes)) {
      // A static PNG or an error page: not an animated avatar. Remember the
      // negative answer for a while; the static path still serves the row.
      entry.nextAnimAttempt = now + 300.0;
      entry.animFetchPending = false;
      return ABCUI_ERROR_NOT_FOUND;
    }
    if (!rememberAnimated(steamId64, entry, std::move(bytes))) {
      entry.nextAnimAttempt = now + 300.0;
      entry.animFetchPending = false;
      return ABCUI_ERROR_LIMIT;
    }
    entry.animFetchPending = false;
    generationOut = entry.animGeneration;
    return ABCUI_OK;
  }
  case steamapi::AvatarAnimationState::Pending:
    entry.animFetchPending = true;
    return ABCUI_ERROR_PENDING;
  case steamapi::AvatarAnimationState::None:
    entry.animFetchPending = false;
    entry.nextAnimAttempt = now + 60.0;
    return ABCUI_ERROR_NOT_FOUND;
  case steamapi::AvatarAnimationState::Unavailable:
    entry.animFetchPending = false;
    entry.nextAnimAttempt = now + 60.0;
    return ABCUI_ERROR_UNAVAILABLE;
  }
  return ABCUI_ERROR_UNAVAILABLE;
}

bool animatedBytes(std::uint64_t steamId64, std::vector<std::byte> &out) {
  out.clear();
  const auto itor = Entries.find(steamId64);
  if (itor == Entries.end() || itor->second.apng.empty())
    return false;
  itor->second.lastAnimUse = NowSeconds();
  out = itor->second.apng;
  return true;
}

} // namespace avatars
