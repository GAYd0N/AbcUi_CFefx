// Exact Win32 member-call ABI, shared by the real factory probe and tests.
#include "ClientMuteApi.h"

namespace services::playerlist {

bool ClientMuteApi::attach(void *Object) {
  *this = ClientMuteApi{};
  if (!Object)
    return false;
  auto **Table = *static_cast<void ***>(Object);
  if (!Table || !Table[2] || !Table[3] || !Table[4])
    return false;
  Query = reinterpret_cast<QueryFn>(Table[2]);
  SetMuted = reinterpret_cast<SetFn>(Table[3]);
  SetUnmuted = reinterpret_cast<SetFn>(Table[4]);
  Object_ = Object;
  return true;
}

bool ClientMuteApi::isMuted(int Index) const {
  return Object_ && Index > 0 && Query(Object_, Index);
}

bool ClientMuteApi::mute(int Index) const {
  if (!Object_ || Index <= 0)
    return false;
  SetMuted(Object_, Index);
  return true;
}

bool ClientMuteApi::unmute(int Index) const {
  if (!Object_ || Index <= 0)
    return false;
  SetUnmuted(Object_, Index);
  return true;
}

} // namespace services::playerlist
