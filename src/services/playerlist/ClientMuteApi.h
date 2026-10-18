#pragma once

// Borrowed GameClientExports001 ABI adapter, not an interface definition.
// Ghidra GameUI.dll FUN_10015d70: ECX=this; index on stack; slots 2/3/4.
// Only attach a live object returned by the engine factory. This class does
// not validate arbitrary addresses and never owns/destroys the object.
namespace services::playerlist {

class ClientMuteApi {
public:
  bool attach(void *Object);
  bool available() const {
    return Object_ != nullptr;
  }
  bool isMuted(int Index) const;
  bool mute(int Index) const;
  bool unmute(int Index) const;

private:
  using QueryFn = bool(__thiscall *)(void *, int);
  using SetFn = void(__thiscall *)(void *, int);
  void *Object_ = nullptr;
  QueryFn Query = nullptr;
  SetFn SetMuted = nullptr;
  SetFn SetUnmuted = nullptr;
};

} // namespace services::playerlist
