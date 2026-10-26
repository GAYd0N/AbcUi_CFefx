// abcui.gameui - the replacement game-menu pages, one document per page.
// The entry validates the framework's table once and fans the module
// lifecycle out to the page projections (the shell, the console, the
// server browser, the connection surfaces and the vote creation / player
// list / command menu / inventory / create-game / settings pages). A page
// owns
// its DOM, its local gestures and its model-section calls; the framework
// keeps the models, the seams and the navigation state machine.
#include "shared/PageShared.h"
#include "../../common/ImeOverlay.h"

namespace votepage {
void load();
void shutdown();
void unload();
} // namespace votepage

namespace playerlistpage {
void load();
void shutdown();
void unload();
} // namespace playerlistpage

namespace commandmenupage {
void load();
void shutdown();
void unload();
} // namespace commandmenupage

namespace menupage {
void load();
void shutdown();
void unload();
} // namespace menupage

namespace inventorypage {
void load();
void shutdown();
void unload();
} // namespace inventorypage

namespace creategamepage {
void load();
void shutdown();
void unload();
} // namespace creategamepage

namespace consolepage {
void load();
void shutdown();
void unload();
} // namespace consolepage

namespace serverbrowserpage {
void load();
void shutdown();
void unload();
} // namespace serverbrowserpage

namespace connectionpage {
void load();
void shutdown();
void unload();
} // namespace connectionpage

namespace settingspage {
void load();
void shutdown();
void unload();
} // namespace settingspage

namespace quitconfirmpage {
void load();
void shutdown();
void unload();
} // namespace quitconfirmpage

namespace {
moduleui::ImeOverlay Ime;
AbcUiCvarHandle MusicSource = nullptr;
AbcUiCvarHandle MusicVolume = nullptr;
AbcUiAudio Music = 0;
std::string AppliedMusic;
bool MusicRefused = false;
// The engine's own startup track (media/gamestartup*.mp3) plays on the engine
// mp3 stream, which the module's independent SDL channel does not replace.
// One stop per no-level menu presentation keeps the two from overlapping.
bool EngineMusicStopped = false;
bool MenuWasVisible = false;

void onModuleInit() {
  MusicSource = Api->console->registerCvar(
      "abc_ui_menu_music", "mod://abcui.gameui/gameui/scence/audio/opfor.mp3",
      ABCUI_CVAR_VALUE);
  MusicVolume = Api->console->findCvar("mp3volume");
  Api->console->registerCvar(
      "abc_ui_menu_video",
      "mod://abcui.gameui/gameui/scence/video/trainstation.mp4",
      ABCUI_CVAR_VALUE);
  Api->console->registerCvar("abc_ui_menu_backdrop_blur", "2",
                             ABCUI_CVAR_VALUE);
}

void onModuleFrame(double) {
  Ime.frame();
  AbcUiMenuState state{};
  state.structSize = sizeof(state);
  Api->menu->state(&state);
  const bool visible =
      state.up && !state.hasLevel &&
      !(state.loading && state.attemptKind == ABCUI_MENU_ATTEMPT_MAP_LOAD);
  char source[512]{};
  Api->console->cvarString(MusicSource, source, sizeof(source), "");
  if (AppliedMusic != source) {
    Api->audio->close(Music);
    Music = 0;
    AppliedMusic = source;
    MusicRefused = false;
  }
  // The original menu music is the engine's startup stream: the legacy engine
  // track used to be replaced by "mp3 loop", but this module plays its own
  // channel, so the engine keeps sounding media/gamestartup*.mp3 underneath.
  // Stop it once when the no-level menu takes the music over; a later menu
  // presentation re-arms the latch (MediaPlayer, which also owns the engine
  // stream, is closed by the level transition before that edge).
  if (visible && !MenuWasVisible)
    EngineMusicStopped = false;
  MenuWasVisible = visible;
  if (visible && !EngineMusicStopped && !AppliedMusic.empty()) {
    EngineMusicStopped = true;
    Api->console->engineCommand("mp3 stop");
  }
  if (visible && !Music && !MusicRefused && !AppliedMusic.empty()) {
    const int result = Api->audio->open(AppliedMusic.c_str(), 1, &Music);
    if (result != ABCUI_OK) {
      Api->core->warn("menu audio unavailable (%d)", result);
      MusicRefused = true;
    }
  }
  if (Music) {
    const int result = Api->audio->status(Music);
    if (result < 0 && result != ABCUI_ERROR_PENDING) {
      Api->core->warn("menu audio decode failed (%d)", result);
      Api->audio->close(Music);
      Music = 0;
      MusicRefused = true;
    } else {
      Api->audio->setVolume(Music, Api->console->cvarValue(MusicVolume, 1));
      Api->audio->setPaused(Music, visible ? 0 : 1);
    }
  }
}

int onMenuCommand(void *userdata, const void *) {
  return Api->menu->dispatchCommand(static_cast<const char *>(userdata));
}

void onModuleLoad() {
  if (Api->menuconfig->setResourceRoot("mod://abcui.gameui/gameui/config") !=
      ABCUI_OK) {
    Api->core->failInitialization("menu resource catalog refused");
    return;
  }
  Ime.load(Api, "abcui.gameui");
  if (!Api->menu->setNativeTakeover(1)) {
    Api->core->failInitialization("native menu takeover already owned");
    return;
  }
  static const char *const commands[] = {"votemenu",     "+commandmenu",
                                         "-commandmenu", "inventory",
                                         "+inventory",   "-inventory"};
  for (const char *name : commands) {
    if (!Api->commands->subscribeCommand(name, 0, onMenuCommand,
                                         const_cast<char *>(name))) {
      Api->core->failInitialization("native menu command hook unavailable");
      return;
    }
  }
  votepage::load();
  playerlistpage::load();
  commandmenupage::load();
  menupage::load();
  inventorypage::load();
  creategamepage::load();
  consolepage::load();
  serverbrowserpage::load();
  connectionpage::load();
  settingspage::load();
  quitconfirmpage::load();
}

void onModuleShutdown() {
  Ime.reset();
  Api->audio->close(Music);
  Music = 0;
  AppliedMusic.clear();
  MusicRefused = false;
  EngineMusicStopped = false;
  MenuWasVisible = false;
  // The documents die with the context; every page forgets its handles.
  votepage::shutdown();
  playerlistpage::shutdown();
  commandmenupage::shutdown();
  menupage::shutdown();
  inventorypage::shutdown();
  creategamepage::shutdown();
  consolepage::shutdown();
  serverbrowserpage::shutdown();
  connectionpage::shutdown();
  settingspage::shutdown();
  quitconfirmpage::shutdown();
}

void onModuleUnload() {
  Ime.unload();
  votepage::unload();
  playerlistpage::unload();
  commandmenupage::unload();
  menupage::unload();
  inventorypage::unload();
  creategamepage::unload();
  consolepage::unload();
  serverbrowserpage::unload();
  connectionpage::unload();
  settingspage::unload();
  quitconfirmpage::unload();
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  // Every section the pages act through (see PageShared.h and the page
  // sources for which calls each one makes).
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->events ||
      !api->ui || !api->console || !api->documents || !api->menu ||
      !api->votemenu || !api->playerlist || !api->commandmenu ||
      !api->menuconfig || !api->social || !api->inventory || !api->creategame ||
      !api->consolepage || !api->input || !api->shell || !api->serverbrowser ||
      !api->connection || !api->settings || !api->commands || !api->audio ||
      !api->players)
    return 0;
  if (!ABCUI_API_HAS_MEMBER(AbcUiMenuApi, api->menu, dispatchCommand) ||
      !ABCUI_API_HAS_MEMBER(AbcUiCoreApi, api->core, failInitialization) ||
      api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->documents->structSize <
          offsetof(AbcUiDocumentsApi, registerRecovery) ||
      api->menu->structSize < offsetof(AbcUiMenuApi, quitNow) ||
      api->votemenu->structSize < sizeof(AbcUiVoteMenuApi) ||
      api->playerlist->structSize < sizeof(AbcUiPlayerListApi) ||
      api->commandmenu->structSize < sizeof(AbcUiCommandMenuApi) ||
      api->menuconfig->structSize < sizeof(AbcUiMenuConfigApi) ||
      api->social->structSize < sizeof(AbcUiSocialApi) ||
      api->inventory->structSize < sizeof(AbcUiInventoryApi) ||
      api->creategame->structSize < sizeof(AbcUiCreateGameApi) ||
      !ABCUI_API_HAS_MEMBER(AbcUiConsolePageApi, api->consolepage,
                            noteEscapeClaim) ||
      api->serverbrowser->structSize < sizeof(AbcUiServerBrowserApi) ||
      api->connection->structSize < sizeof(AbcUiConnectionApi) ||
      api->settings->structSize < sizeof(AbcUiSettingsApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.gameui";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = &onModuleFrame;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
