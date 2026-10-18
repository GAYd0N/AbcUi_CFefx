// services::serverbrowser::favorites - the pure half (see ServerFavorites.h).
// Engine-free so the offline acceptance test links this exact file.
#include "ServerFavorites.h"

#include <cstdio>

namespace services::serverbrowser {

std::string formatServerIp(uint32_t ip) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", ip >> 24,
                (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
  return buffer;
}

std::string buildConnectAddress(const ServerEntry &entry) {
  if (entry.ip == 0)
    return {};
  uint16_t port = entry.connectionPort;
  if (port == 0)
    port = entry.queryPort;
  if (port == 0)
    return {}; // fail closed: never a connect to port 0
  std::string address = formatServerIp(entry.ip);
  address += ':';
  address += std::to_string(port);
  return address;
}

FavoriteKey favoriteKeyOf(const ServerEntry &entry) {
  FavoriteKey key;
  key.ip = entry.ip;
  key.connPort = entry.connectionPort ? entry.connectionPort : entry.queryPort;
  key.queryPort = entry.queryPort ? entry.queryPort : key.connPort;
  return key;
}

} // namespace services::serverbrowser
