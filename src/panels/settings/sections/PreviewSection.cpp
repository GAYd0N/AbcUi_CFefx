#include "panels/settings/SettingsPage.h"
#include "panels/settings/SettingsUi.h"
#include "services/settings/SettingsModel.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Core.h>
#include <algorithm>

namespace panels::settings {
void SettingsPage::renderPreviews() {
  if (!document() || !Model)
    return;
  auto Text = [&](const char *Cvar) -> std::string {
    const auto View = Model->find(Cvar);
    return View && !View->missing ? View->draftText : "";
  };
  auto Color = [&](const char *Cvar) {
    const auto View = Model->find(Cvar);
    return View ? std::to_string(static_cast<int>(
                      std::clamp(View->draftValue, 0.0f, 255.0f)))
                : "0";
  };
  auto Set = [&](const char *Id, const std::string &Uri) {
    if (auto *Image = document()->GetElementById(Id)) {
      const auto Previous = Image->GetAttribute<Rml::String>("src", "");
      if (Previous != Uri) {
        Image->SetAttribute("src", Uri);
        // Rml caches file textures globally. Retire the old palette
        // variant so dragging colors cannot accumulate GPU textures.
        if (!Previous.empty())
          Rml::ReleaseTexture(Previous);
      }
    }
  };
  // The player model is a LIVE Studio preview (drag to turn it): bind the
  // cvar's model directory to a resolvable <model> source, the same mapping
  // the main menu's character card uses. The player colors are not a preview
  // property of the renderer, so they stay out of the 3D view.
  {
    const std::string model = Text("model");
    std::string path = "models/player.mdl";
    if (model.ends_with(".mdl"))
      path = model;
    else if (!model.empty())
      path = "models/player/" + model + "/" + model + ".mdl";
    if (auto *Preview = document()->GetElementById("model-preview")) {
      if (Preview->GetAttribute<Rml::String>("src", "") != path)
        Preview->SetAttribute("src", path);
    }
  }
  // The engine's stored colour token ("#Valve_Ltblue") normalizes to the
  // bare palette name the preview decoder expects.
  Set("spray-preview", "preview://spray/" + Text("cl_logofile") + "/" +
                           detail::logoColorName(Text("cl_logocolor")));
}
void SettingsPage::releasePreviews() {
  if (!document())
    return;
  for (const auto *Id : {"model-preview", "spray-preview"})
    if (auto *Image = document()->GetElementById(Id)) {
      const auto Source = Image->GetAttribute<Rml::String>("src", "");
      if (!Source.empty())
        Rml::ReleaseTexture(Source);
    }
}
} // namespace panels::settings
