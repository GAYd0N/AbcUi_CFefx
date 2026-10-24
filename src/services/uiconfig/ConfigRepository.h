#pragma once

#include "UiDefinitions.h"
#include <functional>

namespace services::uiconfig {

// Startup-only owning repository. Never copy/move after handing rows to a
// model. Resource IO is separate from the parser and the UI projection.
class ConfigRepository {
public:
  ConfigRepository() = default;
  ConfigRepository(const ConfigRepository &) = delete;
  ConfigRepository &operator=(const ConfigRepository &) = delete;

  using Reader = std::function<bool(const std::string &, std::string &)>;
  using Listing = std::function<std::vector<std::string>(const std::string &)>;
  void load(const std::string &root, Reader reader, Listing listing,
            const std::vector<settings::SettingRow> &Advanced = {});
  const MenuDefinition &menu() const {
    return Menu;
  }
  const std::vector<SettingsPageDefinition> &pages() const {
    return Pages;
  }
  const std::vector<settings::SettingRow> &rows() const {
    return Rows;
  }
  const std::vector<ConfigError> &errors() const {
    return Errors;
  }

private:
  bool read(const std::string &Path, std::string &Text);
  Reader Read;
  void buildRows();

  MenuDefinition Menu;
  std::vector<SettingsPageDefinition> Pages;
  std::vector<settings::SettingRow> Rows;
  std::vector<ConfigError> Errors;
};

} // namespace services::uiconfig
