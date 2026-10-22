#pragma once

// Internal aggregate for the page types GameUiModule owns. The public
// GameUiModule.h only forward-declares them; translation units that call page
// methods include this instead of depending on the composition header's
// former include list.

#include "../../panels/menushared/MenuShellWindows.h"
#include "../../panels/settings/SettingsPage.h"
#include "../../panels/settings/VideoConfirmPage.h"
