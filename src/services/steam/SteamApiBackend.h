#pragma once

// rmlui::steamapi backend contract - the shape every provider implements
// (see SteamAPI.h for the layering). Backends live in their own TU and are
// probed by SteamAPI.cpp; nothing else includes this header.

#include "SteamAPI.h"

namespace steamapi {

class SteamApiBackend {
public:
  virtual ~SteamApiBackend() = default;

  // One-shot connect attempt; false = this backend cannot serve here.
  virtual bool connect() = 0;
  virtual void disconnect() {}
  virtual Backend kind() = 0;

  virtual SteamId64 localSteamId() = 0;
  virtual std::string personaName(SteamId64 id) = 0;
  virtual bool avatar(SteamId64 id, AvatarRgba &out) = 0;
};

// Backend accessors for the selector (SteamAPI.cpp). Each returns a static
// instance; connect() may be called once per init().
SteamApiBackend *nativeBackend();
SteamApiBackend *bridgeBackend();

} // namespace steamapi
