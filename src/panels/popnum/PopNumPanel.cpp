// panels::PopNumPanel implementation - see PopNumPanel.h.
// from: ABCEnchance/src/hud/vgui/popnum.* (GBK->UTF-8).
#include <metahook.h>

#include "PopNumPanel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <abc/CvarCmd.h>
#include <abc/Console.h>
#include <abc/Format.h>

#include "../../framework/ColorUtil.h"
#include <abc/Math.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/Localization.h"
#include "../../framework/Projection.h"
#include "../../framework/Theme.h"

// Windows.h min/max macros fight std::max (old repo precedent).
#undef max
#undef min

namespace panels {

namespace popnum {

// Old CViewport ctor default (persisted contract - never rename; registered
// there because the spawn gate lived in the Viewport POPNUMBER handler).
cvar_t *PopNumber = nullptr;

void registerCvars() {
  PopNumber = abc::CREATE_CVAR("hud_popnumber", "1", abc::FCVAR_VALUE, nullptr);
}

} // namespace popnum

namespace {

// Old POPNUMBER_* thresholds (POPNUMBER_B = 1e8 - the old code's own value,
// used with the localized 亿 suffix).
constexpr int kThousand = 1000;
constexpr int kMillion = 1000000;
constexpr int kHundredMillion = 100000000;
constexpr float kFrontDotGate = 0.5f; // old cos-60 check
// D-17 #5: fixed lifetime (the old 2-per-frame alpha at the 60fps reference:
// 255 / 2 / 60 seconds), identical at any framerate.
constexpr float kNumberLifetime = 255.0f / 2.0f / 60.0f;

bool viewForward(vec3_t forward) {
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local)
    return false;
  vec3_t view{};
  gEngfuncs.GetViewAngles(view);
  abc::math::AngleVectors(view, forward, nullptr, nullptr);
  return true;
}

bool inFront(const float origin[3], const vec3_t forward) {
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local)
    return false;
  float rel[3] = {
      origin[0] - local->curstate.origin[0],
      origin[1] - local->curstate.origin[1],
      origin[2] - local->curstate.origin[2],
  };
  const float length =
      sqrtf(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
  if (length <= 0.0f)
    return false;
  const float dot =
      (rel[0] * forward[0] + rel[1] * forward[1] + rel[2] * forward[2]) /
      length;
  return dot > kFrontDotGate;
}

} // namespace

PopNumPanel::PopNumPanel()
    : HudPanel("popnum", "abc/abcui/hud/rml/popnum.rml") {}

void PopNumPanel::onDocumentReady() {
  subscribe(netmsg::PopNum, [this](const netmsg::PopNumData &data) {
    // Old Viewport POPNUMBER handler: cvar gate + front-facing check at
    // spawn, then AddPopNumber.
    if (!popnum::PopNumber || popnum::PopNumber->value <= 0)
      return;
    vec3_t forward{};
    if (!viewForward(forward) || !inFront(data.origin, forward))
      return;
    addNumber(data.origin, data.value, data.color);
  });
}

std::string PopNumPanel::formatValue(int value) {
  // Old ctor: divide first, then localize with the matching suffix token.
  std::string token = "PopNumber_O";
  if (value > kHundredMillion) {
    value /= kHundredMillion;
    token = "PopNumber_B";
  } else if (value > kMillion) {
    value /= kMillion;
    token = "PopNumber_M";
  } else if (value > kThousand) {
    value /= kThousand;
    token = "PopNumber_K";
  }
  std::string text;
  if (!framework::Localization::translate(text, token))
    text = "{0}"; // bare number (old ConstructString with a null Find
                  // result rendered nothing - D-17: keep the value visible)
  // D-17: the tokens carry {0} placeholders (was the vgui %s1 style) and
  // substitute through abc::Format - no printf near the table text.
  return abc::Format(text, value);
}

void PopNumPanel::addNumber(const float origin[3], int value,
                            const int color[4]) {
  Rml::Element *container =
      Document ? Document->GetElementById("numbers") : nullptr;
  if (!container)
    return;
  if (Numbers.size() >= 128) {
    container->RemoveChild(Numbers.front().element);
    Numbers.erase(Numbers.begin());
  }
  Rml::ElementPtr holder = Document->CreateElement("div");
  if (!holder)
    return;
  Rml::Element *number = holder.get();
  // RmlUi's SetClass takes ONE class name (a space would make a single
  // bogus class); styling classes go on one per call.
  number->SetClass("number", true);
  number->SetClass("font-default", true);
  number->SetInnerRML(formatValue(value));
  const std::string tint =
      framework::ColorUtil::formatRgba(color[0], color[1], color[2], color[3]);
  number->SetProperty("color", tint);
  container->AppendChild(std::move(holder));

  Number entry;
  for (int i = 0; i < 3; i++)
    entry.origin[i] = origin[i];
  entry.createTime = ClientTimeNow;
  entry.element = number;
  Numbers.push_back(entry);
  if (!isVisible())
    show();
}

void PopNumPanel::reset() {
  // Old per-panel Destory (map change Reset sweeps every pop number).
  if (Document) {
    if (Rml::Element *container = Document->GetElementById("numbers")) {
      while (Rml::Element *child = container->GetFirstChild())
        container->RemoveChild(child);
    }
  }
  Numbers.clear();
  hide();
}

void PopNumPanel::onData(double frameTime) {
  (void)frameTime;
  // The per-frame loop runs in onRedraw (world projection needs the live
  // view matrices; old OnThink ran in the same phase).
}

void PopNumPanel::onRedraw(double frameTime) {
  ClientTimeNow += frameTime;
  Rml::Element *container =
      Document ? Document->GetElementById("numbers") : nullptr;
  if (!container)
    return;

  // Old Viewport-wide visibility (kHideAll token hides everything).
  if (hudstate::isHudHidden(hudstate::kHideAll))
    hide();
  else if (!Numbers.empty())
    show();

  // Old OnThink per number: front-facing gate re-checked for the reposition,
  // bob y = proj.y - tall * (-sin(age)/2).
  const float density = framework::Theme::scale();
  const float boxWidth = kBoxWidth * density;
  const float boxHeight = kBoxHeight * density;

  for (size_t i = 0; i < Numbers.size();) {
    Number &number = Numbers[i];
    // D-17 #5: the old label alpha decayed a fixed 2 per frame - a
    // framerate-dependent lifetime (~2.1s at 60fps, ~1.3s at 100fps).
    // Time-based now, referenced to the old 60fps behavior.
    number.alpha =
        255.0f * std::max(0.0f, 1.0f - static_cast<float>(ClientTimeNow -
                                                          number.createTime) /
                                           kNumberLifetime);
    if (number.alpha <= 0.0f) {
      if (number.element)
        container->RemoveChild(number.element);
      Numbers.erase(Numbers.begin() + i);
      continue;
    }

    vec3_t forward{};
    if (viewForward(forward) && inFront(number.origin, forward)) {
      float screen[2] = {};
      if (framework::projection::worldToScreen(number.origin, screen)) {
        if (number.element)
          number.element->SetProperty("display", "none");
        ++i;
        continue;
      }
      const float age = static_cast<float>(ClientTimeNow - number.createTime);
      const float ratio = -sinf(age) / 2.0f;
      if (number.element) {
        number.element->SetProperty("display", "block");
        number.element->SetProperty(
            "left", std::to_string(screen[0] - boxWidth / 2.0f) + "px");
        number.element->SetProperty(
            "top", std::to_string(screen[1] - boxHeight * ratio) + "px");
        number.element->SetProperty("opacity",
                                    std::to_string(number.alpha / 255.0f));
      }
    } else if (number.element)
      number.element->SetProperty("display", "none");
    i++;
  }
  if (Numbers.empty())
    hide();
}

} // namespace panels
