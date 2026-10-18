#pragma once

// Internal aggregate for the page types GameUiModule owns. The public
// GameUiModule.h only forward-declares them; translation units that call page
// methods include this instead of depending on the composition header's
// former include list.

#include "../../panels/commandmenu/CommandMenuPage.h"
#include "../../panels/console/ConsolePage.h"
#include "../../panels/creategame/CreateGamePage.h"
#include "../../panels/loading/LoadingWindows.h"
#include "../../panels/mainmenu/MainMenuPage.h"
#include "../../panels/menushared/MenuShellWindows.h"
#include "../../panels/playerlist/PlayerListPage.h"
#include "../../panels/serverbrowser/ServerBrowserPage.h"
#include "../../panels/settings/SettingsPage.h"
#include "../../panels/settings/VideoConfirmPage.h"
#include "../../panels/votemenu/VoteMenuPage.h"
