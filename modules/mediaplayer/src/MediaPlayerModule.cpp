// abcui.mediaplayer - the RmlUi replacement for the client's "Sven Co-op
// Media Player" window (client.dll CMediaSelect, command MediaPlayer,
// issue 11).
//
// Playback backend: the engine's own mp3 stream (hw.dll "mp3" command:
// playfile / pause / resume / stop), not the client's FMOD media player and
// not a new decoder. That is the only engine path that can start an
// arbitrary playlist entry by name and still pause/resume, so the page owns
// the transport. The client's FMOD player keeps its own private index and
// offers no play-by-path command; the engine stream is shared with the
// level's CD soundtrack, which is why a map change stops playback (the
// level owns the stream again) while closing the page leaves it running.
//
// The engine exposes no stream position, so elapsed time comes from the
// module clock and the track length from the MP3 frame headers
// (Mp3Duration). Progress is therefore a display estimate; it is what
// drives the automatic next-track hand-off, mirroring the native player's
// end-of-stream advance.
#include "../../common/UiText.h"
#include <abcui/events.h>
#include <abcui/module_api.h>

#include "Mp3Duration.h"
#include "../../common/Parse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;

constexpr const char *kDocumentName = "mediaplayer";
constexpr const char *kDocumentPath =
    "mod://abcui.mediaplayer/gameui/rml/mediaplayer.rml";

// Playlist files, in the client's own order (client.dll CMediaSelect::RelPls
// reads "playlist.txt" and then "playlist.m3u" from GAMECONFIG).
constexpr const char *kPlaylistTxt = "playlist.txt";
constexpr const char *kPlaylistM3u = "playlist.m3u";

constexpr uint32_t kMaxPlaylistBytes = 256 * 1024;
constexpr uint32_t kMaxMediaBytes = 32 * 1024 * 1024;
// The engine's mp3 track table holds 200 paths per process and never frees
// one, so the playlist must stay below it: repeated playfile calls append
// new paths, and the level's own CD tracks share the table.
constexpr std::size_t kMaxTracks = 160;
constexpr int kMaxM3uDepth = 4;

struct Track {
  std::string path;  // engine path as written in the playlist
  std::string label; // basename shown in the list
};

enum class Transport { kStopped, kPlaying, kPaused };

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
uint64_t ChangeBinding = 0;
AbcUiInputToken Token = 0;
AbcUiSubscription MenuStateSubscription = 0;
AbcUiSubscription InitHudSubscription = 0;

std::vector<Track> Tracks;
// Track-duration cache (see trackDuration): namespace scope, never a
// function-local dynamic static - MSVC's thread-safe guard for those lives
// in static TLS and the module loader refuses TLS images.
std::vector<std::pair<std::string, double>> DurationCache;
std::string PlaylistSource;
std::string Hint;
int Current = -1;
int Selected = -1;
Transport State = Transport::kStopped;
double Elapsed = 0.0;
double Duration = 0.0;
bool PageOpen = false;
bool NeedsPlaylistReload = true;
// Change gate for the per-frame progress writes.
int LastElapsedSecond = -1;
int LastProgressPercent = -1;

AbcUiCvarHandle MusicVolume = nullptr;

void closePage();
void addTypedTrack();
void removeSelected();
void moveSelection(int step);

std::string toString(const char *text) {
  return text ? std::string(text) : std::string();
}

std::string translated(const char *token, const char *fallback) {
  char out[512];
  if (token && *token && Api->ui->translate(token, out, sizeof(out)))
    return std::string(out);
  return toString(fallback);
}

std::string escapeText(std::string_view text) {
  return moduleui::escapeText(Api, text);
}

std::string escapeAttribute(std::string_view text) {
  return moduleui::escapeAttribute(Api, text);
}

std::string attributeOf(AbcUiElement *element, const char *name) {
  return moduleui::readString(Api->ui->elementAttribute, element, name);
}

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

void setInnerRml(const char *id, const std::string &rml) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

void setText(const char *id, const std::string &text) {
  setInnerRml(id, escapeText(text));
}

void setDisplay(const char *id, bool visible) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetProperty(element, "display", visible ? "block" : "none");
}

bool addInputFocused() {
  AbcUiElement *input = node("mp-add-path");
  return input && Api->ui->elementPseudoClassSet(input, "focus");
}

// ---- text helpers --------------------------------------------------------

std::string basenameOf(std::string_view path) {
  const std::size_t slash = path.find_last_of("/\\");
  return std::string(slash == std::string_view::npos ? path
                                                     : path.substr(slash + 1));
}

std::string formatTime(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds))
    return "0:00";
  const auto total = static_cast<long long>(seconds);
  return std::format("{}:{:02}", total / 60, total % 60);
}

bool validMediaPath(std::string_view path) {
  if (path.empty() || path.size() > 512)
    return false;
  // The path ends up inside an engine command; a quote would break the
  // argument and a control byte or command separator could inject one.
  for (const unsigned char c : path) {
    if (c < 0x20 || c == 0x7F || c == '"' || c == ';')
      return false;
  }
  return true;
}

// ---- file access ---------------------------------------------------------

bool readEngineText(const std::string &path, uint32_t maxBytes,
                    std::string &out) {
  out.clear();
  const int size = Api->vfs->readEngineFile(path.c_str(), nullptr, 0, maxBytes);
  if (size <= 0 || static_cast<uint32_t>(size) > maxBytes)
    return size == 0;
  std::string buffer(static_cast<std::size_t>(size), '\0');
  const int read =
      Api->vfs->readEngineFile(path.c_str(), buffer.data(),
                               static_cast<uint32_t>(buffer.size()), maxBytes);
  if (read != size)
    return false;
  out = std::move(buffer);
  return true;
}

double trackDuration(const std::string &path) {
  // Namespace scope on purpose (DurationCache below): a function-local
  // dynamic static arms MSVC's thread-safe-init guard, whose per-thread
  // epoch is static TLS - the module loader refuses TLS images outright
  // (this exact static once shipped a TLS directory and the module was
  // skipped at boot with every feature of it missing).
  for (const auto &entry : DurationCache)
    if (entry.first == path)
      return entry.second;
  double seconds = 0.0;
  const int size =
      Api->vfs->readEngineFile(path.c_str(), nullptr, 0, kMaxMediaBytes);
  if (size > 0 && static_cast<uint32_t>(size) <= kMaxMediaBytes) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (Api->vfs->readEngineFile(path.c_str(), bytes.data(),
                                 static_cast<uint32_t>(bytes.size()),
                                 kMaxMediaBytes) == size)
      seconds = mediaplayer::mp3DurationSeconds(bytes.data(), bytes.size());
  }
  if (DurationCache.size() >= 128)
    DurationCache.erase(DurationCache.begin());
  DurationCache.emplace_back(path, seconds);
  return seconds;
}

// ---- playlist ------------------------------------------------------------

std::string directoryOf(std::string_view path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string_view::npos ? std::string()
                                         : std::string(path.substr(0, slash));
}

std::string joinPath(std::string_view directory, std::string_view name) {
  if (directory.empty())
    return std::string(name);
  return std::string(directory) + "/" + std::string(name);
}

bool isAbsolutePath(std::string_view path) {
  return (path.size() >= 2 &&
          std::isalpha(static_cast<unsigned char>(path[0])) &&
          path[1] == ':') ||
         (!path.empty() && (path.front() == '/' || path.front() == '\\'));
}

bool endsWithCaseInsensitive(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size())
    return false;
  const std::string_view tail = text.substr(text.size() - suffix.size());
  for (std::size_t i = 0; i < suffix.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(tail[i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i])))
      return false;
  return true;
}

void appendTrack(const std::string &path) {
  if (Tracks.size() >= kMaxTracks || !validMediaPath(path))
    return;
  Tracks.push_back(Track{path, basenameOf(path)});
}

void parsePlaylistText(std::string_view text, std::string_view directory,
                       int depth);

// One playlist file: blank lines and comments are skipped (the client's
// RelPls skips lines starting with '*' or '#'), nested .m3u files expand in
// place and resolve against their own directory first.
bool expandPlaylistFile(const std::string &name, std::string_view directory,
                        int depth) {
  if (depth > kMaxM3uDepth)
    return false;
  std::string text;
  std::string used = name;
  if (!readEngineText(name, kMaxPlaylistBytes, text)) {
    if (isAbsolutePath(name))
      return false;
    used = joinPath(directory, name);
    if (!readEngineText(used, kMaxPlaylistBytes, text))
      return false;
  }
  parsePlaylistText(text, directoryOf(used), depth);
  return true;
}

void parsePlaylistText(std::string_view text, std::string_view directory,
                       int depth) {
  std::size_t begin = 0;
  while (begin <= text.size()) {
    std::size_t end = text.find('\n', begin);
    if (end == std::string_view::npos)
      end = text.size();
    std::string_view line = text.substr(begin, end - begin);
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
      line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
      line.remove_prefix(1);
    if (!line.empty() && line.front() != '#' && line.front() != '*') {
      if (endsWithCaseInsensitive(line, ".m3u"))
        expandPlaylistFile(std::string(line), directory, depth + 1);
      else
        appendTrack(std::string(line));
    }
    if (end == text.size())
      break;
    begin = end + 1;
  }
}

// The engine's built-in CD soundtrack (hw.dll's track table): used only when
// neither playlist file exists, so the page is never an empty shell on a
// stock install.
void loadBuiltInSoundtrack() {
  for (int i = 1; i <= 17; ++i)
    appendTrack(std::format("media/Half-Life{:02}.mp3", i));
  for (int i = 1; i <= 5; ++i)
    appendTrack(std::format("media/Prospero{:02}.mp3", i));
  static constexpr int kSuspense[] = {1, 2, 3, 5, 7};
  for (const int index : kSuspense)
    appendTrack(std::format("media/Suspense{:02}.mp3", index));
}

void loadPlaylist() {
  const std::string playingPath =
      Current >= 0 && Current < static_cast<int>(Tracks.size())
          ? Tracks[static_cast<std::size_t>(Current)].path
          : std::string();
  Tracks.clear();
  PlaylistSource.clear();
  Hint.clear();
  NeedsPlaylistReload = false;
  std::string text;
  if (readEngineText(kPlaylistTxt, kMaxPlaylistBytes, text)) {
    PlaylistSource = kPlaylistTxt;
    parsePlaylistText(text, {}, 0);
  } else if (readEngineText(kPlaylistM3u, kMaxPlaylistBytes, text)) {
    PlaylistSource = kPlaylistM3u;
    parsePlaylistText(text, {}, 0);
  } else {
    PlaylistSource = translated("abcui.mediaplayer/MediaPlayer_BuiltIn",
                                "Built-in soundtrack");
    loadBuiltInSoundtrack();
    Hint = translated("abcui.mediaplayer/MediaPlayer_NoPlaylist",
                      "No playlist.txt or playlist.m3u found; showing the "
                      "built-in soundtrack.");
  }
  if (Tracks.empty()) {
    Hint = translated("abcui.mediaplayer/MediaPlayer_EmptyPlaylist",
                      "The playlist is empty. Add a media path below.");
    PlaylistSource =
        translated("abcui.mediaplayer/MediaPlayer_EmptySource", "empty");
  }
  if (!playingPath.empty()) {
    for (std::size_t i = 0; i < Tracks.size(); ++i) {
      if (Tracks[i].path == playingPath && static_cast<int>(i) != Current) {
        Current = static_cast<int>(i);
        break;
      }
    }
  }
  Selected =
      Current >= 0 && Current < static_cast<int>(Tracks.size()) ? Current : -1;
}

// ---- engine transport ----------------------------------------------------

void engineCommand(const std::string &command) {
  Api->console->engineCommand(command.c_str());
}

void stopPlayback() {
  if (State != Transport::kStopped)
    engineCommand("mp3 stop");
  State = Transport::kStopped;
  Elapsed = 0.0;
  Duration = 0.0;
  LastElapsedSecond = -1;
  LastProgressPercent = -1;
}

int wrapIndex(int index) {
  const int count = static_cast<int>(Tracks.size());
  if (count <= 0)
    return -1;
  if (index < 0)
    return count - 1;
  if (index >= count)
    return 0;
  return index;
}

void startTrack(int index, bool updateSelection) {
  if (Tracks.empty())
    return;
  index = wrapIndex(index);
  if (index < 0)
    return;
  const Track &track = Tracks[static_cast<std::size_t>(index)];
  if (!validMediaPath(track.path)) {
    Hint = translated("abcui.mediaplayer/MediaPlayer_InvalidPath",
                      "That media path cannot be sent to the engine.");
    return;
  }
  engineCommand("mp3 stop");
  engineCommand("mp3 playfile \"" + track.path + "\"");
  Current = index;
  if (updateSelection)
    Selected = index;
  State = Transport::kPlaying;
  Elapsed = 0.0;
  Duration = trackDuration(track.path);
  Hint.clear();
  LastElapsedSecond = -1;
  LastProgressPercent = -1;
}

void togglePlay() {
  if (Tracks.empty())
    return;
  if (State == Transport::kPlaying) {
    engineCommand("mp3 pause");
    State = Transport::kPaused;
  } else if (State == Transport::kPaused) {
    engineCommand("mp3 resume");
    State = Transport::kPlaying;
  } else {
    startTrack(Current >= 0 ? Current : (Selected >= 0 ? Selected : 0), true);
  }
}

void skipTrack(int step) {
  if (Tracks.empty())
    return;
  const int base = Current >= 0 ? Current : (Selected >= 0 ? Selected : 0);
  startTrack(base + step, true);
}

// ---- volume (the engine stream only has the MP3Volume cvar) --------------

int volumePercent() {
  const float value =
      MusicVolume ? Api->console->cvarValue(MusicVolume, 0.5f) : 0.5f;
  return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
}

void setVolumePercent(int percent) {
  percent = std::clamp(percent, 0, 100);
  if (MusicVolume)
    Api->console->setCvarValue(MusicVolume,
                               static_cast<float>(percent) / 100.0f);
}

// ---- page ----------------------------------------------------------------

void renderPlaylist();
void renderTransport();
void updateProgress();
void updateVolumeRow();

void openPage() {
  if (!Page || PageOpen)
    return;
  if (NeedsPlaylistReload)
    loadPlaylist();
  PageOpen = true;
  if (Current >= 0 && Current < static_cast<int>(Tracks.size()))
    Selected = Current;
  renderPlaylist();
  renderTransport();
  updateProgress();
  updateVolumeRow();
  Api->documents->show(Page);
  Api->ui->documentFocus(Page);
  if (!Token) {
    AbcUiInputCallbacks callbacks{};
    callbacks.structSize = sizeof(callbacks);
    callbacks.claimsKey = [](void *, int32_t, int32_t keynum) -> int32_t {
      if (!PageOpen)
        return 0;
      if (keynum == ABCUI_KEY_ESCAPE)
        return 1;
      if (addInputFocused())
        return 1;
      switch (keynum) {
      case ABCUI_KEY_UPARROW:
      case ABCUI_KEY_DOWNARROW:
      case ABCUI_KEY_LEFTARROW:
      case ABCUI_KEY_RIGHTARROW:
      case ABCUI_KEY_ENTER:
      case ABCUI_KEY_KP_ENTER:
      case ABCUI_KEY_DELETE:
        return 1;
      default:
        return 0;
      }
    };
    callbacks.claimsWheel = [](void *) -> int32_t { return PageOpen ? 1 : 0; };
    callbacks.blocksGameplay = [](void *) -> int32_t {
      return addInputFocused() ? 1 : 0;
    };
    callbacks.onClaimedKeyDown = [](void *, int32_t keynum) {
      if (!PageOpen)
        return;
      if (keynum == ABCUI_KEY_ESCAPE) {
        closePage();
        return;
      }
      if (addInputFocused()) {
        if (keynum == ABCUI_KEY_ENTER || keynum == ABCUI_KEY_KP_ENTER)
          addTypedTrack();
        return;
      }
      if (keynum == ABCUI_KEY_DELETE) {
        removeSelected();
        return;
      }
      if (keynum == ABCUI_KEY_UPARROW || keynum == ABCUI_KEY_DOWNARROW) {
        moveSelection(keynum == ABCUI_KEY_UPARROW ? -1 : 1);
        return;
      }
      if ((keynum == ABCUI_KEY_ENTER || keynum == ABCUI_KEY_KP_ENTER) &&
          Selected >= 0) {
        startTrack(Selected, true);
        renderPlaylist();
        renderTransport();
      }
    };
    callbacks.onOwnershipLost = [](void *) {
      if (PageOpen)
        closePage();
    };
    Token = Api->input->takeToken(&callbacks);
  }
}

void closePage() {
  if (!PageOpen)
    return;
  PageOpen = false;
  if (Token) {
    Api->input->releaseToken(Token);
    Token = 0;
  }
  if (Page) {
    Api->ui->documentBlur(Page);
    Api->ui->documentHide(Page);
  }
}

void togglePage() {
  if (PageOpen)
    closePage();
  else
    openPage();
}

// ---- commands / selection ------------------------------------------------

void moveSelection(int step) {
  if (Tracks.empty())
    return;
  int index = Selected;
  if (index < 0)
    index = Current >= 0 ? Current : 0;
  else
    index = wrapIndex(index + step);
  Selected = index;
  renderPlaylist();
  if (AbcUiElement *list = node("mp-list")) {
    AbcUiElement *row = Api->ui->elementChild(list, index);
    if (row)
      Api->ui->elementScrollIntoView(row, 0);
  }
}

void removeSelected() {
  if (Selected < 0 || Selected >= static_cast<int>(Tracks.size()))
    return;
  const bool removingCurrent = Selected == Current;
  if (removingCurrent)
    stopPlayback();
  Tracks.erase(Tracks.begin() + Selected);
  if (Current > Selected)
    --Current;
  if (removingCurrent)
    Current = -1;
  if (Tracks.empty())
    Selected = -1;
  else
    Selected = std::min(Selected, static_cast<int>(Tracks.size()) - 1);
  renderPlaylist();
  renderTransport();
}

void moveSelected(int step) {
  if (Selected < 0 || Selected >= static_cast<int>(Tracks.size()))
    return;
  const int target = Selected + step;
  if (target < 0 || target >= static_cast<int>(Tracks.size()))
    return;
  std::swap(Tracks[Selected], Tracks[target]);
  if (Current == Selected)
    Current = target;
  else if (Current == target)
    Current = Selected;
  Selected = target;
  renderPlaylist();
}

void addTypedTrack() {
  AbcUiElement *input = node("mp-add-path");
  if (!input)
    return;
  char value[512] = {};
  const int size = Api->ui->elementValue(input, value, sizeof(value));
  if (size <= 0)
    return;
  const std::size_t length =
      static_cast<std::size_t>(std::min<int>(size, sizeof(value) - 1));
  std::string path(value, length);
  while (!path.empty() && (path.back() == ' ' || path.back() == '\t'))
    path.pop_back();
  while (!path.empty() && (path.front() == ' ' || path.front() == '\t'))
    path.erase(path.begin());
  if (!validMediaPath(path)) {
    Hint = translated("abcui.mediaplayer/MediaPlayer_InvalidPath",
                      "That media path cannot be sent to the engine.");
    renderPlaylist();
    return;
  }
  if (Tracks.size() >= kMaxTracks)
    return;
  Tracks.push_back(Track{path, basenameOf(path)});
  Selected = static_cast<int>(Tracks.size()) - 1;
  Hint.clear();
  Api->ui->elementSetValue(input, "");
  renderPlaylist();
  renderTransport();
}

// ---- DOM rendering -------------------------------------------------------

void renderPlaylist() {
  if (!Page)
    return;
  std::string rows;
  rows.reserve(Tracks.size() * 96 + 64);
  for (std::size_t i = 0; i < Tracks.size(); ++i) {
    const Track &track = Tracks[i];
    const bool playing = static_cast<int>(i) == Current;
    const bool selected = static_cast<int>(i) == Selected;
    rows += std::format(
        "<div class=\"mp-row{}{}\" data-action=\"mp-play\" data-index=\"{}\" "
        "title=\"{}\"><span class=\"mp-row-num\">{}</span>"
        "<span class=\"mp-row-name\">{}</span>"
        "<span class=\"mp-row-mark\">{}</span></div>",
        selected ? " mp-row-selected" : "", playing ? " mp-row-playing" : "", i,
        escapeAttribute(track.path), i + 1, escapeText(track.label),
        playing ? "&#9654;" : "");
  }
  if (rows.empty()) {
    rows = std::format(
        "<div class=\"mp-empty\">{}</div>",
        escapeText(translated("abcui.mediaplayer/MediaPlayer_EmptyPlaylist",
                              "The playlist is empty. Add a media path "
                              "below.")));
  }
  setInnerRml("mp-list", rows);
  setText("mp-playlist-source", PlaylistSource);
  if (AbcUiElement *count = node("mp-playlist-count"))
    Api->ui->elementSetInnerRML(count,
                                std::format("{}", Tracks.size()).c_str());
  setText("mp-hint", Hint);
  setDisplay("mp-hint", !Hint.empty());
}

void renderTransport() {
  if (!Page)
    return;
  const bool hasTrack =
      Current >= 0 && Current < static_cast<int>(Tracks.size());
  setText("mp-track", hasTrack
                          ? Tracks[static_cast<std::size_t>(Current)].label
                          : translated("abcui.mediaplayer/MediaPlayer_NoTrack",
                                       "No track selected"));
  const char *stateToken = "abcui.mediaplayer/MediaPlayer_Stopped";
  const char *stateFallback = "Stopped";
  const char *toggleToken = "abcui.mediaplayer/MediaPlayer_Play";
  const char *toggleFallback = "Play";
  if (State == Transport::kPlaying) {
    stateToken = "abcui.mediaplayer/MediaPlayer_Playing";
    stateFallback = "Playing";
    toggleToken = "abcui.mediaplayer/MediaPlayer_Pause";
    toggleFallback = "Pause";
  } else if (State == Transport::kPaused) {
    stateToken = "abcui.mediaplayer/MediaPlayer_Paused";
    stateFallback = "Paused";
    toggleToken = "abcui.mediaplayer/MediaPlayer_Resume";
    toggleFallback = "Resume";
  }
  setText("mp-state", translated(stateToken, stateFallback));
  setText("mp-toggle-label", translated(toggleToken, toggleFallback));
}

void updateProgress() {
  if (!Page)
    return;
  const int second = static_cast<int>(Elapsed);
  const int percent =
      Duration > 0.0
          ? static_cast<int>(std::clamp(Elapsed / Duration, 0.0, 1.0) * 100.0)
          : 0;
  if (second == LastElapsedSecond && percent == LastProgressPercent)
    return;
  LastElapsedSecond = second;
  LastProgressPercent = percent;
  setText("mp-elapsed", formatTime(Elapsed));
  setText("mp-total",
          Duration > 0.0
              ? formatTime(Duration)
              : translated("abcui.mediaplayer/MediaPlayer_Unknown", "--:--"));
  if (AbcUiElement *progress = node("mp-progress"))
    Api->ui->elementProgressValue(
        progress, Duration > 0.0 ? static_cast<float>(percent) / 100.0f : 0.0f);
}

void updateVolumeRow() {
  const int percent = volumePercent();
  if (AbcUiElement *slider = node("mp-volume"))
    Api->ui->elementSetValue(slider, std::format("{}", percent).c_str());
  setText("mp-volume-value", std::format("{}%", percent));
}

// ---- DOM events ----------------------------------------------------------

int32_t ABCUI_CALL onClick(void *, AbcUiElement *target,
                           const AbcUiElementEvent *) {
  if (!target)
    return 0;
  AbcUiElement *entry = target;
  std::string action;
  std::string index;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty()) {
      index = attributeOf(entry, "data-index");
      break;
    }
    entry = Api->ui->elementParent(entry);
  }
  if (action.empty())
    return 0;
  if (action == "mp-close") {
    closePage();
  } else if (action == "mp-prev") {
    skipTrack(-1);
  } else if (action == "mp-next") {
    skipTrack(1);
  } else if (action == "mp-toggle") {
    togglePlay();
  } else if (action == "mp-stop") {
    stopPlayback();
  } else if (action == "mp-play") {
    const auto row = moduleui::parseNumber<int>(index);
    if (row && *row >= 0 && *row < static_cast<int>(Tracks.size())) {
      Selected = *row;
      startTrack(*row, true);
      renderPlaylist();
    }
  } else if (action == "mp-remove") {
    removeSelected();
  } else if (action == "mp-up") {
    moveSelected(-1);
  } else if (action == "mp-down") {
    moveSelected(1);
  } else if (action == "mp-add") {
    addTypedTrack();
  } else if (action == "mp-reload") {
    loadPlaylist();
    renderPlaylist();
  } else {
    return 0;
  }
  renderTransport();
  updateProgress();
  return 1;
}

int32_t ABCUI_CALL onChange(void *, AbcUiElement *target,
                            const AbcUiElementEvent *) {
  if (!target)
    return 0;
  const char *id = Api->ui->elementId(target);
  if (!id || std::strcmp(id, "mp-volume") != 0)
    return 0;
  char value[16] = {};
  if (Api->ui->elementValue(target, value, sizeof(value)) > 0) {
    if (const auto volume = moduleui::parseNumber<int>(value)) {
      setVolumePercent(*volume);
      updateVolumeRow();
    }
  }
  return 1;
}

// ---- document lifecycle --------------------------------------------------

void ABCUI_CALL onDocumentReady(void *) {
  BoundRoot = Api->documents->root(Page);
  if (!BoundRoot)
    return;
  ClickBinding =
      Api->ui->elementAddListener(BoundRoot, "click", &onClick, nullptr);
  ChangeBinding =
      Api->ui->elementAddListener(BoundRoot, "change", &onChange, nullptr);
  // Attributes ride outside the framework's #token text localization, so the
  // input's placeholder is resolved and set from the module's own strings.
  if (AbcUiElement *input = Api->ui->querySelector(Page, "mp-add-path"))
    Api->ui->elementSetAttribute(
        input, "placeholder",
        translated("abcui.mediaplayer/MediaPlayer_AddPlaceholder", "").c_str());
  updateVolumeRow();
  renderPlaylist();
  renderTransport();
  updateProgress();
}

void ABCUI_CALL onDocumentClose(void *) {
  if (BoundRoot && ClickBinding) {
    Api->ui->elementRemoveListener(BoundRoot, "click", ClickBinding);
    ClickBinding = 0;
  }
  if (BoundRoot && ChangeBinding) {
    Api->ui->elementRemoveListener(BoundRoot, "change", ChangeBinding);
    ChangeBinding = 0;
  }
  BoundRoot = nullptr;
  if (PageOpen)
    closePage();
}

// ---- event sinks ---------------------------------------------------------

void ABCUI_CALL onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  // The replacement menu (and its console overlay) owns the screen: the
  // media window steps aside but the stream keeps running.
  if (state && (state->up || state->consoleOpen) && PageOpen)
    closePage();
}

void ABCUI_CALL onInitHud(void *, const void *) {
  // A new level owns the engine's single MP3 stream; drop the window and the
  // transport state so nothing looks "playing" over the level's music.
  stopPlayback();
  closePage();
  Current = -1;
  Selected = -1;
  NeedsPlaylistReload = true;
}

// ---- module lifecycle ----------------------------------------------------

// The chained MediaPlayer command. A page that is not loaded cannot replace
// anything: the command declines (once, with a console note) and the native
// window opens.
int ABCUI_CALL onMediaPlayerCommand(void *, const void *) {
  if (!Page || !Api->documents->loaded(Page)) {
    static bool Declined = false;
    if (!Declined) {
      Declined = true;
      Api->core->print("mediaplayer: replacement page is not loaded; the "
                       "native window stays\n");
    }
    return 0;
  }
  togglePage();
  return 1;
}

bool CommandHooked = false;

// The engine's MediaPlayer command is registered by the client DLL, which
// can happen after this module's onLoad; subscribeCommand refuses a name it
// cannot find, so the hook is retried every frame until it sticks.
void tryHookCommand() {
  if (CommandHooked)
    return;
  CommandHooked = Api->commands->subscribeCommand(
                      "MediaPlayer", 0, &onMediaPlayerCommand, nullptr) != 0;
}

void ABCUI_CALL onModuleLoad() {
  AbcUiDocumentDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = kDocumentName;
  desc.path = kDocumentPath;
  desc.flags = 0; // in-game window, not part of the menu page stack
  desc.zIndex = 0;
  desc.userdata = nullptr;
  desc.onReady = &onDocumentReady;
  desc.onShow = nullptr;
  desc.onHide = nullptr;
  desc.onClose = &onDocumentClose;
  Page = Api->documents->create(&desc);

  MenuStateSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_STATE, 0,
                                                 &onMenuState, nullptr);
  InitHudSubscription = Api->events->subscribe(ABCUI_CHANNEL_NETMSG_INIT_HUD, 0,
                                               &onInitHud, nullptr);
  tryHookCommand();
}

void ABCUI_CALL onModuleInit() {
  MusicVolume = Api->console->findCvar("MP3Volume");
}

void ABCUI_CALL onModuleFrame(double frameTime) {
  tryHookCommand();
  if (frameTime <= 0.0)
    return;
  if (State == Transport::kPlaying) {
    Elapsed += frameTime;
    if (Duration > 0.0 && Elapsed >= Duration) {
      // The engine stream ended; advance like the native player does.
      const int next = wrapIndex((Current >= 0 ? Current : 0) + 1);
      if (next >= 0) {
        startTrack(next, false);
        if (PageOpen) {
          renderPlaylist();
          renderTransport();
        }
      }
    }
  }
  if (PageOpen)
    updateProgress();
}

void ABCUI_CALL onModuleShutdown() {
  closePage();
  stopPlayback();
}

void ABCUI_CALL onModuleUnload() {
  closePage();
  stopPlayback();
  Page = nullptr;
  Tracks.clear();
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->console ||
      !api->events || !api->vfs || !api->ui || !api->commands ||
      !api->documents || !api->input)
    return 0;
  if (api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->vfs->structSize < sizeof(AbcUiVfsApi) ||
      api->documents->structSize < sizeof(AbcUiDocumentsApi) ||
      api->input->structSize < sizeof(AbcUiInputApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.mediaplayer";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = &onModuleFrame;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
