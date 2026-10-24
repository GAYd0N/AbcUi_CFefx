// The live preview widgets (model, spray, crosshair) and the ten-part
// crosshair shape projection.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <cstdio>
#include <format>
#include <string>

namespace settingspage {

// ------------------------------------------------------------ previews -----

void setPreviewSource(const char *id, const std::string &uri) {
  AbcUiElement *image = node(id);
  if (!image)
    return;
  const std::string previous = attributeOf(image, "src");
  if (previous == uri)
    return;
  Api->ui->elementSetAttribute(image, "src", uri.c_str());
  // Rml caches file textures globally. Retire the old palette variant so
  // dragging colors cannot accumulate GPU textures.
  if (!previous.empty())
    Api->ui->releaseTexture(previous.c_str());
}

void renderPreviews() {
  if (!Page || !Api->settings)
    return;
  const auto textOf = [](const char *cvar) -> std::string {
    AbcUiSettingRow row{};
    if (!rowFound(cvar, row) || (row.flags & ABCUI_SET_ROW_MISSING))
      return {};
    return row.draftText;
  };
  // The player model is a LIVE Studio preview (drag to turn it): bind the
  // cvar's model directory to a resolvable <model> source, the same mapping
  // the main menu's character card uses. The player colors are not a preview
  // property of the renderer, so they stay out of the 3D view.
  {
    const std::string model = textOf("model");
    std::string path = "models/player.mdl";
    if (model.size() >= 4 && model.compare(model.size() - 4, 4, ".mdl") == 0)
      path = model;
    else if (!model.empty())
      path = "models/player/" + model + "/" + model + ".mdl";
    if (AbcUiElement *preview = node("model-preview")) {
      if (attributeOf(preview, "src") != path)
        Api->ui->elementSetAttribute(preview, "src", path.c_str());
    }
  }
  // The engine's stored colour token ("#abcui.gameui/Valve_Ltblue") normalizes
  // to the bare palette name the preview decoder expects.
  setPreviewSource("spray-preview", "preview://spray/" + textOf("cl_logofile") +
                                        "/" +
                                        logoColorName(textOf("cl_logocolor")));
}

// ----------------------------------------------------- crosshair preview ---

constexpr const char *kCoreIds[5] = {"cx-core-center", "cx-core-top",
                                     "cx-core-bottom", "cx-core-left",
                                     "cx-core-right"};
constexpr const char *kOutlineIds[5] = {"cx-outline-center", "cx-outline-top",
                                        "cx-outline-bottom", "cx-outline-left",
                                        "cx-outline-right"};

std::string rgbaText(int red, int green, int blue, int alpha) {
  char buffer[48];
  buffer[0] = '\0';
  Api->ui->formatRgba(red, green, blue, alpha, buffer, sizeof(buffer));
  return buffer;
}

std::string translateRect(float x, float y) {
  return std::format("translate({:g}px, {:g}px)", x, y);
}

std::string sizeText(float value) {
  return std::format("{:g}px", value);
}

void applyPreviewPart(AbcUiElement *element, PreviewPart &applied,
                      const char *color, const float rect[4]) {
  if (!element)
    return;
  const bool same = applied.Valid && applied.Color == color &&
                    applied.X == rect[0] && applied.Y == rect[1] &&
                    applied.Width == rect[2] && applied.Height == rect[3];
  if (same)
    return;
  applied.Valid = true;
  applied.Color = color;
  applied.X = rect[0];
  applied.Y = rect[1];
  applied.Width = rect[2];
  applied.Height = rect[3];
  setProperty(element, "background-color", color);
  const std::string transform = translateRect(rect[0], rect[1]);
  setProperty(element, "transform", transform.c_str());
  const std::string width = sizeText(rect[2]);
  const std::string height = sizeText(rect[3]);
  setProperty(element, "width", width.c_str());
  setProperty(element, "height", height.c_str());
}

void renderCrosshairPreview() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *box = node("crosshair-preview");
  if (!box)
    return;

  // Draft values with the shipped defaults as the fallback (a page is free to
  // omit a row; the preview then shows the documented default).
  const auto value = [](const char *cvar, float fallback) {
    AbcUiSettingRow row{};
    if (!rowFound(cvar, row) || (row.flags & ABCUI_SET_ROW_MISSING))
      return fallback;
    return row.state == ABCUI_SET_STATE_INVALID ? row.engineValue
                                                : row.draftValue;
  };
  AbcUiCrosshairLayout layout{};
  layout.thickness = value("cl_crosshairthickness", 2.0f);
  layout.length = value("cl_crosshairsize", 24.0f);
  layout.offset = value("cl_crosshairgap", 16.0f);
  layout.outlineWidth = value("cl_crosshair_outline_draw", 1.0f) > 0.0f
                            ? value("cl_crosshair_outline", 1.0f)
                            : 0.0f;
  layout.dot = value("cl_crosshairdot", 0.0f) > 0.0f ? 1 : 0;
  layout.tShape = value("cl_crosshair_t", 0.0f) > 0.0f ? 1 : 0;

  const std::string coreColor =
      rgbaText(channelValue(value("cl_crosshaircolor_r", 50.0f)),
               channelValue(value("cl_crosshaircolor_g", 250.0f)),
               channelValue(value("cl_crosshaircolor_b", 50.0f)),
               channelValue(value("cl_crosshairalpha", 200.0f)));
  const std::string outlineColor =
      rgbaText(channelValue(value("cl_crosshaircolor_outline_r", 0.0f)),
               channelValue(value("cl_crosshaircolor_outline_g", 0.0f)),
               channelValue(value("cl_crosshaircolor_outline_b", 0.0f)),
               channelValue(value("cl_crosshair_outline_alpha", 255.0f)));

  float core[5][4] = {};
  float outline[5][4] = {};
  int32_t visible[5] = {};
  Api->ui->crosshairLayout(&layout, core, outline, visible);
  for (int index = 0; index < 5; ++index) {
    const bool shown = visible[index] != 0;
    applyPreviewPart(node(kCoreIds[index]), PreviewCore[index],
                     shown ? coreColor.c_str() : kTransparentColor,
                     core[index]);
    applyPreviewPart(node(kOutlineIds[index]), PreviewOutline[index],
                     (shown && layout.outlineWidth > 0.0f)
                         ? outlineColor.c_str()
                         : kTransparentColor,
                     outline[index]);
  }
}

} // namespace settingspage
