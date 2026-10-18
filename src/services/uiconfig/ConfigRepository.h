#pragma once

#include "UiDefinitions.h"
#include <filesystem>

namespace services::uiconfig {

// Startup-only owning repository. Never copy/move after handing rows to a
// model. Resource IO is separate from the parser and the UI projection.
class ConfigRepository {
public:
  ConfigRepository() = default;
  ConfigRepository(const ConfigRepository &) = delete;
  ConfigRepository &operator=(const ConfigRepository &) = delete;

  void load(const std::filesystem::path &Root,
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
  bool read(const std::filesystem::path &Path, std::string &Text);
  void buildRows();

  MenuDefinition Menu;
  std::vector<SettingsPageDefinition> Pages;
  std::vector<settings::SettingRow> Rows;
  std::vector<ConfigError> Errors;
};

} // namespace services::uiconfig
