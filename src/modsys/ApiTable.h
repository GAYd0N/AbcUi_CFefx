#pragma once

// framework::ApiTable - the C ABI service table handed to module entries.
//
// The table and its section structs live in sdk/abcui/module_api.h; this
// header is the framework-side owner of the single frozen instance.

#include <abcui/module_api.h>

namespace framework {

namespace ApiTable {

// The one table instance; its layout is append-only (see abi.h).
const AbcUiModuleApi *get();

} // namespace ApiTable

// SDK section implementations living outside ApiTable.cpp (they pull in
// their own subsystems); ApiTable::get() wires the pointers.
namespace DataApi {
const AbcUiDataApi *get();
} // namespace DataApi

namespace UiApi {
const AbcUiUiApi *get();
} // namespace UiApi

namespace WeaponsApi {
const AbcUiWeaponsApi *get();
} // namespace WeaponsApi

namespace PlayersApi {
const AbcUiPlayersApi *get();
} // namespace PlayersApi

namespace ScoreApi {
const AbcUiScoreApi *get();
} // namespace ScoreApi

namespace InputApi {
const AbcUiInputApi *get();
} // namespace InputApi

namespace ShellApi {
const AbcUiShellApi *get();
} // namespace ShellApi

namespace VoiceApi {
const AbcUiVoiceApi *get();
} // namespace VoiceApi

namespace DocumentsApi {
const AbcUiDocumentsApi *get();
} // namespace DocumentsApi

namespace MenuApi {
const AbcUiMenuApi *get();
} // namespace MenuApi

namespace VoteMenuApi {
const AbcUiVoteMenuApi *get();
} // namespace VoteMenuApi

namespace PlayerListApi {
const AbcUiPlayerListApi *get();
} // namespace PlayerListApi

namespace CommandMenuApi {
const AbcUiCommandMenuApi *get();
} // namespace CommandMenuApi

namespace MenuConfigApi {
const AbcUiMenuConfigApi *get();
} // namespace MenuConfigApi

namespace SocialApi {
const AbcUiSocialApi *get();
} // namespace SocialApi

namespace InventoryApi {
const AbcUiInventoryApi *get();
} // namespace InventoryApi

namespace CreateGameApi {
const AbcUiCreateGameApi *get();
} // namespace CreateGameApi

namespace ConsolePageApi {
const AbcUiConsolePageApi *get();
// framework-internal (the menu owner's routed ESC): reads-and-clears the
// console page's one-layer ESC claim.
bool consumeEscapeClaim();
} // namespace ConsolePageApi

namespace ServerBrowserApi {
const AbcUiServerBrowserApi *get();
// framework-internal (the menu owner's routed ESC): reads-and-clears the
// browser page's one-subpage ESC claim.
bool consumeEscapeClaim();
} // namespace ServerBrowserApi

namespace ConnectionApi {
const AbcUiConnectionApi *get();
} // namespace ConnectionApi

} // namespace framework
