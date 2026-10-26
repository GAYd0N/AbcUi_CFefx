#pragma once

#include <abcui/module_api.h>
#include "UiText.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <string>

namespace moduleui {
// Compiled independently into each editor module; no shared runtime provider.
class ImeOverlay {
public:
  void load(const AbcUiModuleApi *api, const char *id) {
    Api = api;
    const std::string name = std::string(id) + ".ime";
    const std::string path = "mod://" + std::string(id) + "/hud/rml/ime.rml";
    AbcUiDocumentDesc desc{};
    desc.structSize = sizeof(desc);
    desc.name = name.c_str();
    desc.path = path.c_str();
    desc.flags = ABCUI_DOC_FLAG_DECORATION;
    desc.zIndex = 700;
    Document = Api->documents->create(&desc);
  }

  void reset() {
    Previous = {};
  }

  void unload() {
    if (Document && Api)
      Api->documents->destroy(Document);
    Document = nullptr;
    Api = nullptr;
  }

  void frame() {
    if (!Document || !Api->documents->loaded(Document))
      return;
    AbcUiImeState state{};
    state.structSize = sizeof(state);
    Api->input->imeState(&state);
    if (!state.visible) {
      Api->documents->hide(Document);
      Previous = {};
      return;
    }
    if (std::memcmp(&state, &Previous, sizeof(state)) != 0) {
      std::string rml;
      if (*state.composition)
        rml = "<div id=\"ime-composition\">" + escape(state.composition) +
              "</div>";
      if (state.candidateCount) {
        rml += "<div id=\"ime-candidates\">";
        for (int i = 0; i < state.candidateCount; ++i) {
          rml += "<div class=\"ime-candidate";
          if (i == state.selected)
            rml += " selected";
          rml += "\"><span class=\"ime-index\">" + std::to_string(i + 1) +
                 "</span><span class=\"ime-text\">" +
                 escape(state.candidates[i]) + "</span></div>";
        }
        rml += "</div>";
      }
      if (auto *panel = Api->ui->querySelector(Document, "ime-panel"))
        Api->ui->elementSetInnerRML(panel, rml.c_str());
      float density = 1;
      Api->ui->documentScale(Document, &density);
      density = density > 0 ? density : 1;
      int width = 0, height = 0;
      Api->ui->documentSize(Document, &width, &height);
      const float x = std::clamp(state.x / density, 4.0f,
                                 std::max(4.0f, width / density - 184.0f));
      const float boxHeight = 10.0f + (*state.composition ? 13.0f : 0.0f) +
                              (state.candidateCount ? 30.0f : 0.0f);
      float y = (state.y + state.lineHeight) / density + 2;
      if (y + boxHeight > height / density - 4)
        y = state.y / density - boxHeight - 2;
      Api->ui->documentSetProperty(Document, "left",
                                   std::format("{:.2f}dp", x).c_str());
      Api->ui->documentSetProperty(
          Document, "top", std::format("{:.2f}dp", std::max(4.0f, y)).c_str());
      Previous = state;
    }
    Api->documents->show(Document);
  }

private:
  const AbcUiModuleApi *Api = nullptr;
  AbcUiDocument *Document = nullptr;
  AbcUiImeState Previous{};

  std::string escape(const char *text) const {
    return moduleui::escapeText(Api, text);
  }
};
} // namespace moduleui
