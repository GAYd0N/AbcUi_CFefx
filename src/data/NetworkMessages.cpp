// data::netmsg implementation - see NetworkMessages.h.
// from: ABCEnchance/src/core/events/networkmessage.* (batch-A subset);
// hook/chain shape mirrors AbcEssential autofunc/Signals.cpp.
#include <metahook.h>

#include "NetworkMessages.h"
#include <cmath>

#include <abc/NetMsg.h>
#include <abc/Console.h>
#include <abc/Plugin.h>

#include <abcui/events.h>

#include "../modsys/EventBus.h"

#include <cstdint>

#include <cstdint>

#include <cvardef.h>

namespace netmsg {

#pragma region events (definitions - external linkage, see the header)
abc::CallbackList<void(int, int)> AmmoX;
abc::CallbackList<void(const WeaponListData &)> WeaponList;
abc::CallbackList<void()> InitHud;
abc::CallbackList<void(int, const char *)> CustWeapon;
abc::CallbackList<void(int, int, int, int)> CurWeapon;
abc::CallbackList<void(int)> HideWeapon;
abc::CallbackList<void(int)> HideHud;
abc::CallbackList<void(int, const char *)> WeaponSpr;
abc::CallbackList<void(int)> WeapPickup;
abc::CallbackList<void(int, int)> AmmoPickup;
abc::CallbackList<void(const char *)> ItemPickup;
abc::CallbackList<void(int)> Battery;
abc::CallbackList<void(int)> Health;
abc::CallbackList<void(int, float, int, float, float, int, int, int)> ScoreInfo;
abc::CallbackList<void(int, int, int, const std::string &)> ShowMenu;
abc::CallbackList<void(int)> FlashBat;
abc::CallbackList<void(bool, int)> Flashlight;
abc::CallbackList<TextMsgCallback> TextMsg;
abc::CallbackList<SayTextCallback> SayText;
abc::CallbackList<void(const DamageData &)> Damage;
abc::CallbackList<void(const PopNumData &)> PopNum;
// Batch C additions.
abc::CallbackList<void(int, bool)> Spectator;
abc::CallbackList<void(const char *)> ServerName;
abc::CallbackList<void(const char *)> NextMap;
abc::CallbackList<void(int)> TimeEnd;
abc::CallbackList<void(int, const char *)> Motd;
abc::CallbackList<void(const VoteMenuData &)> VoteMenu;
abc::CallbackList<void()> EndVote;
abc::CallbackList<void(const MapListData &)> MapList;
abc::CallbackList<void()> ClExtrasInfo;
abc::CallbackList<void(const VoiceMaskData &)> VoiceMask;
abc::CallbackList<void()> ReqState;
abc::CallbackList<void(const InvItemData &)> InvAdd;
abc::CallbackList<void(int, int)> InvRemove;
#pragma endregion

namespace {

#pragma region hooks
// Previous handler per message (the HookUserMsg return), chained through on
// every path the old hooks forwarded - most importantly on truncated reads.
pfnUserMsgHook original_AmmoX = nullptr;
pfnUserMsgHook original_WeaponList = nullptr;
pfnUserMsgHook original_InitHUD = nullptr;
pfnUserMsgHook original_CustWeapon = nullptr;
pfnUserMsgHook original_CurWeapon = nullptr;
pfnUserMsgHook original_HideWeapon = nullptr;
pfnUserMsgHook original_HideHUD = nullptr;
pfnUserMsgHook original_WeaponSpr = nullptr;
pfnUserMsgHook original_WeapPickup = nullptr;
pfnUserMsgHook original_AmmoPickup = nullptr;
pfnUserMsgHook original_ItemPickup = nullptr;
pfnUserMsgHook original_Battery = nullptr;
pfnUserMsgHook original_Health = nullptr;
pfnUserMsgHook original_ScoreInfo = nullptr;
pfnUserMsgHook original_ShowMenu = nullptr;
pfnUserMsgHook original_FlashBat = nullptr;
pfnUserMsgHook original_Flashlight = nullptr;
pfnUserMsgHook original_TextMsg = nullptr;
pfnUserMsgHook original_SayText = nullptr;
// Handler displaced from the head of the SayText chain by
// ensureSayTextFirst(); only the outermost link forwards to it.
pfnUserMsgHook upstream_SayText = nullptr;
int sayTextDepth = 0;
pfnUserMsgHook original_Damage = nullptr;
pfnUserMsgHook original_MetaHook = nullptr;
// hud_vote gate for the VoteMenu consumer (set by the vote module through
// the module console api at HUD_Init - before any usermsg can arrive, so the
// null branch is unreachable in practice; null would pass the message
// straight through, old IsVoteEnable).
cvar_t *voteEnabled = nullptr;
pfnUserMsgHook original_Spectator = nullptr;
pfnUserMsgHook original_ServerName = nullptr;
pfnUserMsgHook original_NextMap = nullptr;
pfnUserMsgHook original_TimeEnd = nullptr;
pfnUserMsgHook original_MOTD = nullptr;
pfnUserMsgHook original_VoteMenu = nullptr;
pfnUserMsgHook original_EndVote = nullptr;
pfnUserMsgHook original_MapList = nullptr;
pfnUserMsgHook original_ClExtrasInfo = nullptr;
pfnUserMsgHook original_VoiceMask = nullptr;
pfnUserMsgHook original_ReqState = nullptr;
pfnUserMsgHook original_InvAdd = nullptr;
pfnUserMsgHook original_InvRemove = nullptr;

int __MsgFunc_AmmoX(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int index = msg.readByte();
  int count = msg.readLong();
  if (!msg.ok())
    return original_AmmoX(pszName, iSize, pbuf);
  AmmoX(index, count);
  const AbcUiAmmoEvent ammoEvent{index, count};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_AMMO_X, ammoEvent);
  return original_AmmoX(pszName, iSize, pbuf);
}

int __MsgFunc_WeaponList(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  WeaponListData data;
  data.name = msg.readString();
  if (data.name.size() >= 128)
    return original_WeaponList(pszName, iSize, pbuf);
  data.ammoType = msg.readChar();
  data.max1 = msg.readLong();
  if (data.max1 == 255)
    data.max1 = -1;
  data.ammo2Type = msg.readChar();
  data.max2 = msg.readLong();
  if (data.max2 == 255)
    data.max2 = -1;
  data.slot = msg.readChar();
  data.slotPos = msg.readChar();
  data.id = msg.readShort();
  data.flags = msg.readByte();
  if (!msg.ok())
    return original_WeaponList(pszName, iSize, pbuf);
  if (data.id > 0 && data.slot >= 0 && data.slot < 10)
    WeaponList(data);
  return original_WeaponList(pszName, iSize, pbuf);
}

int __MsgFunc_InitHUD(const char *pszName, int iSize, void *pbuf) {
  InitHud();
  framework::eventBus().publishRaw(ABCUI_CHANNEL_NETMSG_INIT_HUD, nullptr);
  return original_InitHUD(pszName, iSize, pbuf);
}

int __MsgFunc_CustWeapon(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int id = msg.readShort();
  std::string name = msg.readString();
  if (!msg.ok())
    return original_CustWeapon(pszName, iSize, pbuf);
  if (!name.empty())
    CustWeapon(id, name.c_str());
  return original_CustWeapon(pszName, iSize, pbuf);
}

int __MsgFunc_CurWeapon(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int state = msg.readByte();
  int id = msg.readShort();
  int clip = msg.readLong();
  int clip2 = msg.readLong();
  if (!msg.ok())
    return original_CurWeapon(pszName, iSize, pbuf);
  CurWeapon(state, id, clip, clip2);
  const AbcUiCurWeaponEvent weaponEvent{state, id, clip, clip2};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_CUR_WEAPON, weaponEvent);
  return original_CurWeapon(pszName, iSize, pbuf);
}

int __MsgFunc_HideWeapon(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int token = msg.readByte();
  if (!msg.ok())
    return original_HideWeapon(pszName, iSize, pbuf);
  HideWeapon(token);
  return original_HideWeapon(pszName, iSize, pbuf);
}

int __MsgFunc_HideHUD(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int token = msg.readByte();
  if (!msg.ok())
    return original_HideHUD(pszName, iSize, pbuf);
  HideHud(token);
  return original_HideHUD(pszName, iSize, pbuf);
}

int __MsgFunc_WeaponSpr(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int id = msg.readShort();
  std::string name = msg.readString();
  if (!msg.ok())
    return original_WeaponSpr(pszName, iSize, pbuf);
  if (!name.empty())
    WeaponSpr(id, name.c_str());
  return original_WeaponSpr(pszName, iSize, pbuf);
}

int __MsgFunc_WeapPickup(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int index = msg.readShort();
  if (!msg.ok())
    return original_WeapPickup(pszName, iSize, pbuf);
  WeapPickup(index);
  const AbcUiWeaponIdEvent pickupEvent{index};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_WEAP_PICKUP, pickupEvent);
  return original_WeapPickup(pszName, iSize, pbuf);
}

int __MsgFunc_AmmoPickup(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int index = msg.readByte();
  int count = msg.readLong();
  if (!msg.ok())
    return original_AmmoPickup(pszName, iSize, pbuf);
  AmmoPickup(index, count);
  const AbcUiAmmoEvent pickupEvent{index, count};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_AMMO_PICKUP, pickupEvent);
  return original_AmmoPickup(pszName, iSize, pbuf);
}

int __MsgFunc_ItemPickup(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  std::string name = msg.readString();
  if (!msg.ok())
    return original_ItemPickup(pszName, iSize, pbuf);
  ItemPickup(name.c_str());
  const AbcUiItemPickupEvent itemEvent{name.c_str()};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_ITEM_PICKUP, itemEvent);
  return original_ItemPickup(pszName, iSize, pbuf);
}

int __MsgFunc_Battery(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int armor = msg.readShort();
  if (!msg.ok())
    return original_Battery(pszName, iSize, pbuf);
  Battery(armor);
  const AbcUiIntEvent armEvent{armor};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_BATTERY, armEvent);
  return original_Battery(pszName, iSize, pbuf);
}

int __MsgFunc_Health(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int health = msg.readLong();
  if (!msg.ok())
    return original_Health(pszName, iSize, pbuf);
  Health(health);
  const AbcUiIntEvent healthEvent{health};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_HEALTH, healthEvent);
  return original_Health(pszName, iSize, pbuf);
}

int __MsgFunc_ScoreInfo(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int clientIndex = msg.readByte();
  if (!msg.ok())
    return original_ScoreInfo(pszName, iSize, pbuf);
  if (clientIndex >= 1 && clientIndex <= 32) {
    float frags = msg.readFloat();
    int deaths = msg.readLong();
    float health = msg.readFloat();
    float armor = msg.readFloat();
    int teamNumber = msg.readByte();
    int hideExtra = msg.readByte();
    int isAdmin = msg.readByte();
    if (!msg.ok())
      return original_ScoreInfo(pszName, iSize, pbuf);
    // Health/armor may go negative on overkill (GoldSrc reports the raw
    // value); both are accepted as long as they are finite. The magnitude
    // bound only keeps the later int display casts in range; panels clamp
    // for display (bars) or render the dead/N/A tag.
    if (std::isfinite(frags) && std::isfinite(health) && std::isfinite(armor) &&
        std::abs(frags) <= 1000000.0f && deaths >= 0 && deaths <= 1000000 &&
        std::abs(health) <= 1000000000.0f && std::abs(armor) <= 1000000000.0f)
      ScoreInfo(clientIndex, frags, deaths, health, armor, teamNumber,
                hideExtra, isAdmin);
  }
  return original_ScoreInfo(pszName, iSize, pbuf);
}

int __MsgFunc_ShowMenu(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int validSlots = msg.readShort();
  int displaySeconds = msg.readChar();
  int bits = msg.readByte();
  std::string text = msg.readString();
  if (!msg.ok())
    return original_ShowMenu(pszName, iSize, pbuf);
  ShowMenu(validSlots, displaySeconds, bits, text);
  const AbcUiShowMenuEvent moduleEvent{validSlots, displaySeconds, bits,
                                       text.c_str()};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_SHOW_MENU, moduleEvent);
  return framework::eventBus().publishVeto(ABCUI_CHANNEL_NETMSG_SHOW_MENU,
                                           moduleEvent)
             ? 1
             : original_ShowMenu(pszName, iSize, pbuf);
}

int __MsgFunc_FlashBat(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int battery = msg.readByte();
  if (!msg.ok())
    return original_FlashBat(pszName, iSize, pbuf);
  FlashBat(battery);
  const AbcUiFlashBatEvent moduleEvent{battery};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_FLASHBAT, moduleEvent);
  return original_FlashBat(pszName, iSize, pbuf);
}

int __MsgFunc_Flashlight(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int on = msg.readByte();
  int battery = msg.readByte();
  if (!msg.ok())
    return original_Flashlight(pszName, iSize, pbuf);
  Flashlight(on > 0, battery);
  const AbcUiFlashlightEvent moduleEvent{on > 0 ? 1 : 0, battery};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_FLASHLIGHT, moduleEvent);
  return original_Flashlight(pszName, iSize, pbuf);
}

int __MsgFunc_TextMsg(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  int target = msg.readByte();
  std::string info = msg.readString();
  std::string param1 = msg.remaining() > 0 ? msg.readString() : std::string();
  std::string param2 = msg.remaining() > 0 ? msg.readString() : std::string();
  std::string param3 = msg.remaining() > 0 ? msg.readString() : std::string();
  std::string param4 = msg.remaining() > 0 ? msg.readString() : std::string();
  if (!msg.ok())
    return original_TextMsg(pszName, iSize, pbuf);
  // Every subscriber runs; a single false swallows the vanilla handler
  // (old g_EventTextMsg forEach with ret &=).
  bool chain = true;
  TextMsg.forEach(
      [&](const abc::CallbackList<TextMsgCallback>::Handle &,
          const abc::CallbackList<TextMsgCallback>::Callback &callback) {
        chain &= callback(target, info.c_str(), param1.c_str(), param2.c_str(),
                          param3.c_str(), param4.c_str());
      });
  return chain ? original_TextMsg(pszName, iSize, pbuf) : 1;
}

// SayText (issue 13): sender index + one composed line. Sven Co-op ships
// exactly those two fields; the HLSDK shape appends up to four substitution
// strings, so whatever remains on the wire is read as parameters and a short
// message still parses (no field is required past the line itself).
int __MsgFunc_SayText(const char *pszName, int iSize, void *pbuf) {
  // A bridge promoted by ensureSayTextFirst() leaves its old link in the
  // chain below. Only the outermost link parses and publishes; the nested
  // link forwards to the chain tail so a chained plugin cannot draw twice.
  if (sayTextDepth > 0)
    return original_SayText ? original_SayText(pszName, iSize, pbuf) : 0;
  ++sayTextDepth;
  struct DepthGuard {
    int &depth;
    ~DepthGuard() { --depth; }
  } guard{sayTextDepth};
  abc::MsgReader msg(pbuf, iSize);
  SayTextData data;
  data.client = msg.readByte();
  data.text = msg.readString();
  if (!msg.ok())
    return original_SayText(pszName, iSize, pbuf);
  for (int i = 0; i < 4 && msg.remaining() > 0; i++)
    data.params[i] = msg.readString();
  // A subscriber that cannot draw the line must not eat it: `chain` mirrors
  // the TextMsg rule, so the native CHudSayText stays the fallback.
  bool chain = true;
  SayText.forEach(
      [&](const abc::CallbackList<SayTextCallback>::Handle &,
          const abc::CallbackList<SayTextCallback>::Callback &callback) {
        chain &= callback(data);
      });
  // Module-facing mirror: the same veto semantics (nonzero = the module drew
  // the line, the native handler is skipped).
  const AbcUiSayTextEvent moduleEvent{
      data.client,
      data.text.c_str(),
      {data.params[0].c_str(), data.params[1].c_str(), data.params[2].c_str(),
       data.params[3].c_str()}};
  const bool moduleConsumed = framework::eventBus().publishVeto(
      ABCUI_CHANNEL_TEXTFEED_SAY_TEXT, moduleEvent);
  if (!chain || moduleConsumed)
    return 1;
  const pfnUserMsgHook next =
      upstream_SayText ? upstream_SayText : original_SayText;
  return next ? next(pszName, iSize, pbuf) : 1;
}

int __MsgFunc_Damage(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  DamageData data;
  data.armor = msg.readByte();
  data.damage = msg.readByte();
  data.tiles = msg.readLong();
  if (!msg.ok())
    return original_Damage(pszName, iSize, pbuf);
  // Old CHECK_MSG_OK shortcut: an all-zero damage report carries no origin.
  if (data.armor + data.damage + data.tiles == 0)
    return original_Damage(pszName, iSize, pbuf);
  for (int i = 0; i < 3; i++)
    data.from[i] = msg.readCoord();
  if (!msg.ok())
    return original_Damage(pszName, iSize, pbuf);
  Damage(data);
  const AbcUiDamageEvent damageEvent{
      data.armor,
      data.damage,
      data.tiles,
      {data.from[0], data.from[1], data.from[2]}};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_DAMAGE, damageEvent);
  return original_Damage(pszName, iSize, pbuf);
}

int __MsgFunc_Spectator(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  const int index = msg.readByte();
  if (!msg.ok())
    return original_Spectator(pszName, iSize, pbuf);
  if (index <= 0 || index > 32)
    return original_Spectator(pszName, iSize, pbuf);
  const int spectating = msg.readByte();
  if (!msg.ok())
    return original_Spectator(pszName, iSize, pbuf);
  Spectator(index, spectating != 0);
  return original_Spectator(pszName, iSize, pbuf);
}

int __MsgFunc_ServerName(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  const std::string name = msg.readString();
  if (!msg.ok())
    return original_ServerName(pszName, iSize, pbuf);
  ServerName(name.c_str());
  return original_ServerName(pszName, iSize, pbuf);
}

int __MsgFunc_NextMap(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  const std::string map = msg.readString();
  if (!msg.ok())
    return original_NextMap(pszName, iSize, pbuf);
  NextMap(map.c_str());
  return original_NextMap(pszName, iSize, pbuf);
}

int __MsgFunc_TimeEnd(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  const int time = msg.readLong();
  if (!msg.ok())
    return original_TimeEnd(pszName, iSize, pbuf);
  TimeEnd(time);
  return original_TimeEnd(pszName, iSize, pbuf);
}

int __MsgFunc_MOTD(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  const int code = msg.readByte();
  const std::string info = msg.readString();
  if (!msg.ok())
    return original_MOTD(pszName, iSize, pbuf);
  Motd(code, info.c_str());
  const AbcUiMotdEvent moduleEvent{code, info.c_str()};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_MOTD, moduleEvent);
  return original_MOTD(pszName, iSize, pbuf);
}

int __MsgFunc_VoteMenu(const char *pszName, int iSize, void *pbuf) {
  // Old gate: hud_vote off passes the message straight through (no event,
  // no swallow); on, it consumes the message like ShowMenu.
  if (!voteEnabled || voteEnabled->value <= 0)
    return original_VoteMenu(pszName, iSize, pbuf);
  abc::MsgReader msg(pbuf, iSize);
  VoteMenuData data;
  data.type = msg.readByte();
  data.content = msg.readString();
  data.yes = msg.readString();
  data.no = msg.readString();
  if (!msg.ok())
    return original_VoteMenu(pszName, iSize, pbuf);
  VoteMenu(data);
  const AbcUiVoteMenuEvent moduleEvent{data.type, data.content.c_str(),
                                       data.yes.c_str(), data.no.c_str()};
  framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_VOTE_MENU, moduleEvent);
  return framework::eventBus().publishVeto(ABCUI_CHANNEL_NETMSG_VOTE_MENU,
                                           moduleEvent)
             ? 1
             : original_VoteMenu(pszName, iSize, pbuf);
}

int __MsgFunc_EndVote(const char *pszName, int iSize, void *pbuf) {
  EndVote();
  framework::eventBus().publishRaw(ABCUI_CHANNEL_NETMSG_END_VOTE, nullptr);
  return original_EndVote(pszName, iSize, pbuf);
}

int __MsgFunc_MapList(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  MapListData data;
  data.clearAll = msg.readByte();
  if (!msg.ok())
    return original_MapList(pszName, iSize, pbuf);
  if (data.clearAll == 0) {
    data.allCount = msg.readShort();
    if (!msg.ok())
      return original_MapList(pszName, iSize, pbuf);
  } else {
    data.start = msg.readShort();
    data.end = msg.readShort();
    if (!msg.ok() || data.start < 0 || data.end < data.start)
      return original_MapList(pszName, iSize, pbuf);
    const int count = data.end - data.start;
    // Bound the allocation against both the protocol's practical
    // capacity and the bytes actually left in the message.
    if (count > 4096 || count > msg.remaining())
      return original_MapList(pszName, iSize, pbuf);
    data.maps.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; i++)
      data.maps.push_back(msg.readString());
    if (!msg.ok())
      return original_MapList(pszName, iSize, pbuf);
  }
  MapList(data);
  return original_MapList(pszName, iSize, pbuf);
}

int __MsgFunc_ClExtrasInfo(const char *pszName, int iSize, void *pbuf) {
  // Old comment: the payload is an obfuscated blob the HUD never parsed -
  // the event only marks "extra player data changed" (donor flags etc.),
  // so the port chains the original and fires the refresh event.
  const int result = original_ClExtrasInfo(pszName, iSize, pbuf);
  ClExtrasInfo();
  return result;
}

// Issue 17: the server's voice state (long audible, long serverBanned,
// byte modEnable; the trailing byte absent = 0 - the legacy read rule).
// The service owns the semantics; this is parse + chain like every hook.
int __MsgFunc_VoiceMask(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  VoiceMaskData data;
  data.audible = static_cast<unsigned long>(msg.readLong());
  data.serverBanned = static_cast<unsigned long>(msg.readLong());
  if (!msg.ok())
    return original_VoiceMask(pszName, iSize, pbuf);
  data.modEnable = msg.readByte();
  if (!msg.ok())
    data.modEnable = 0;
  VoiceMask(data);
  return original_VoiceMask(pszName, iSize, pbuf);
}

int __MsgFunc_ReqState(const char *pszName, int iSize, void *pbuf) {
  // Empty payload: the server wants our vban/VModEnable now.
  ReqState();
  return original_ReqState(pszName, iSize, pbuf);
}

// Issue 10: the inventory stream. The native window owns this format
// (CInventoryPanel::MsgFunc_InvAdd): long id, three flag bytes, one float,
// then five strings. Keep the exact order - the native handler stays chained
// behind ours so the client's own bookkeeping never diverges.
int __MsgFunc_InvAdd(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  InvItemData data;
  data.id = msg.readLong();
  data.canActivate = msg.readByte() != 0;
  data.canDrop = msg.readByte() != 0;
  data.removeLocked = msg.readByte() != 0;
  data.value = msg.readFloat();
  data.name = msg.readString();
  data.displayName = msg.readString();
  data.description = msg.readString();
  data.extra = msg.readString();
  data.key = msg.readString();
  if (!msg.ok())
    return original_InvAdd ? original_InvAdd(pszName, iSize, pbuf) : 0;
  InvAdd(data);
  return original_InvAdd ? original_InvAdd(pszName, iSize, pbuf) : 0;
}

int __MsgFunc_InvRemove(const char *pszName, int iSize, void *pbuf) {
  abc::MsgReader msg(pbuf, iSize);
  const int id = msg.readLong();
  const int force = msg.readByte();
  if (!msg.ok())
    return original_InvRemove ? original_InvRemove(pszName, iSize, pbuf) : 0;
  InvRemove(id, force);
  return original_InvRemove ? original_InvRemove(pszName, iSize, pbuf) : 0;
}

int __MsgFunc_MetaHook(const char *pszName, int iSize, void *pbuf) {
  // The old plugin-to-plugin usermsg carrier (registered by the user's
  // server-side addon; absent on vanilla servers - original may be null).
  // Chain note (D-17): the old hook never stored its prior and returned 0
  // unconditionally, swallowing any earlier plugin's "MetaHook" handler;
  // this port chains the prior on every path (null original = vanilla
  // server, nothing to forward).
  abc::MsgReader msg(pbuf, iSize);
  const int type = msg.readByte();
  if (!msg.ok())
    return original_MetaHook ? original_MetaHook(pszName, iSize, pbuf) : 0;
  if (type == 77) { // MetaHookMsgType::MHSV_CMD_ABC_CUSTOM
    const int subtype = msg.readByte();
    if (!msg.ok())
      return original_MetaHook ? original_MetaHook(pszName, iSize, pbuf) : 0;
    if (subtype == 0) { // ABCCustomMsg::POPNUMBER
      PopNumData data;
      data.origin[0] = msg.readCoord();
      data.origin[1] = msg.readCoord();
      data.origin[2] = msg.readCoord();
      data.value = msg.readLong();
      data.color[0] = msg.readByte();
      data.color[1] = msg.readByte();
      data.color[2] = msg.readByte();
      data.color[3] = msg.readByte();
      if (!msg.ok())
        return original_MetaHook ? original_MetaHook(pszName, iSize, pbuf) : 0;
      PopNum(data);
      const AbcUiPopNumEvent popEvent{
          {data.origin[0], data.origin[1], data.origin[2]},
          data.value,
          {data.color[0], data.color[1], data.color[2], data.color[3]}};
      framework::eventBus().publish(ABCUI_CHANNEL_NETMSG_POP_NUM, popEvent);
    }
  }
  return original_MetaHook ? original_MetaHook(pszName, iSize, pbuf) : 0;
}
#pragma endregion

// HookUserMsg returns the previous handler. A refresh after a client reload
// may report our own handler back; the real predecessor is already stored, so
// only a foreign one is adopted.
void installHook(const char *name, pfnUserMsgHook hook,
                 pfnUserMsgHook &original) {
  pfnUserMsgHook previous = g_pMetaHookAPI->HookUserMsg(name, hook);
  if (previous && previous != hook)
    original = previous;
}

// True when the address is a function inside the loaded client image. Used to
// tell a client-rebuilt usermsg table (head is the client's own handler) from
// a table another plugin still owns (a plugin hook lives outside that image).
bool isClientCode(uintptr_t address) {
  const auto base = reinterpret_cast<uintptr_t>(abc::g_dwClientBase);
  return base && abc::g_dwClientSize && address >= base &&
         address - base < abc::g_dwClientSize;
}

void registerUserMsgHooks() {
  // A rebuilt client table invalidates the head displaced for SayText; the
  // next ownership decision re-captures the current head.
  upstream_SayText = nullptr;
  installHook("AmmoX", __MsgFunc_AmmoX, original_AmmoX);
  installHook("WeaponList", __MsgFunc_WeaponList, original_WeaponList);
  installHook("InitHUD", __MsgFunc_InitHUD, original_InitHUD);
  installHook("CustWeapon", __MsgFunc_CustWeapon, original_CustWeapon);
  installHook("CurWeapon", __MsgFunc_CurWeapon, original_CurWeapon);
  installHook("HideWeapon", __MsgFunc_HideWeapon, original_HideWeapon);
  installHook("HideHUD", __MsgFunc_HideHUD, original_HideHUD);
  installHook("WeaponSpr", __MsgFunc_WeaponSpr, original_WeaponSpr);
  installHook("WeapPickup", __MsgFunc_WeapPickup, original_WeapPickup);
  installHook("AmmoPickup", __MsgFunc_AmmoPickup, original_AmmoPickup);
  installHook("ItemPickup", __MsgFunc_ItemPickup, original_ItemPickup);
  installHook("Battery", __MsgFunc_Battery, original_Battery);
  installHook("Health", __MsgFunc_Health, original_Health);
  installHook("ScoreInfo", __MsgFunc_ScoreInfo, original_ScoreInfo);
  installHook("ShowMenu", __MsgFunc_ShowMenu, original_ShowMenu);
  installHook("FlashBat", __MsgFunc_FlashBat, original_FlashBat);
  installHook("Flashlight", __MsgFunc_Flashlight, original_Flashlight);
  installHook("TextMsg", __MsgFunc_TextMsg, original_TextMsg);
  installHook("SayText", __MsgFunc_SayText, original_SayText);
  installHook("Damage", __MsgFunc_Damage, original_Damage);
  // The MetaHook carrier is addon-registered; a null chain (vanilla servers)
  // leaves every read a no-op passthrough (see __MsgFunc_MetaHook).
  installHook("MetaHook", __MsgFunc_MetaHook, original_MetaHook);
  // Batch C additions.
  installHook("Spectator", __MsgFunc_Spectator, original_Spectator);
  installHook("ServerName", __MsgFunc_ServerName, original_ServerName);
  installHook("NextMap", __MsgFunc_NextMap, original_NextMap);
  installHook("TimeEnd", __MsgFunc_TimeEnd, original_TimeEnd);
  installHook("MOTD", __MsgFunc_MOTD, original_MOTD);
  installHook("VoteMenu", __MsgFunc_VoteMenu, original_VoteMenu);
  installHook("EndVote", __MsgFunc_EndVote, original_EndVote);
  installHook("MapList", __MsgFunc_MapList, original_MapList);
  installHook("ClExtrasInfo", __MsgFunc_ClExtrasInfo, original_ClExtrasInfo);
  installHook("VoiceMask", __MsgFunc_VoiceMask, original_VoiceMask);
  installHook("ReqState", __MsgFunc_ReqState, original_ReqState);
  // Issue 10: the client registers both at its own HUD_Init, which runs
  // before this init() from the hook spine. A null result means the message
  // table moved; the inventory page then has no data (fail-closed, the
  // native panel is still suppressed by the command hook).
  installHook("InvAdd", __MsgFunc_InvAdd, original_InvAdd);
  installHook("InvRemove", __MsgFunc_InvRemove, original_InvRemove);
  static bool warnedMissingInventory = false;
  if ((!original_InvAdd || !original_InvRemove) && !warnedMissingInventory) {
    warnedMissingInventory = true;
    abc::ConsoleWarn("[AbcUi] inventory messages (InvAdd/InvRemove) were not "
                     "registered by the client; the inventory page has no "
                     "data\n");
  }
}

} // namespace

int32_t ensureSayTextFirst() {
  if (!g_pMetaHookAPI || !g_pMetaHookAPI->FindUserMsgHook ||
      !g_pMetaHookAPI->HookUserMsg)
    return 0;
  // Never move a frame that is currently dispatching.
  if (sayTextDepth > 0)
    return 0;
  usermsg_t *head = g_pMetaHookAPI->FindUserMsgHook("SayText");
  if (!head || !head->function)
    return 0;
  if (head->function == &__MsgFunc_SayText)
    return 1;
  const pfnUserMsgHook previous =
      g_pMetaHookAPI->HookUserMsg("SayText", __MsgFunc_SayText);
  if (!previous || previous == &__MsgFunc_SayText)
    return 0;
  upstream_SayText = previous;
  return 1;
}

void init() {
  // Installed once per process. HUD_Init can repeat (map cycles), but the
  // usermsg table then still points at this chain: re-hooking every cycle
  // would make two chained plugins point at each other (AbcEssential hooks
  // the same messages) and recurse forever.
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  registerUserMsgHooks();
}

void refreshAfterClientLoad() {
  // A client DLL reload re-registers its usermsg table, replacing our hooks
  // and leaving the stored predecessors pointing into the old image. Only in
  // that case re-chain. The probe is a message no other plugin hooks: a
  // client-image handler means the table was rebuilt. A normal HUD cycle
  // leaves our hook at the head and does nothing.
  if (!g_pMetaHookAPI->FindUserMsgHook)
    return;
  usermsg_t *probe = g_pMetaHookAPI->FindUserMsgHook("SayText");
  if (!probe || !isClientCode(reinterpret_cast<uintptr_t>(probe->function)))
    return;
  registerUserMsgHooks();
}

void setVoteGate(cvar_t *cvar) {
  voteEnabled = cvar;
}

} // namespace netmsg
