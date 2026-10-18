#pragma once
// Page-internal action constants and formatting helpers shared by sections.
namespace services::settings {
enum class SettingKind;
struct RowView;
} // namespace services::settings
#include <string>
namespace Rml {
class Element;
}
namespace panels::settings::detail {
inline constexpr const char *kActionApply = "apply";
inline constexpr const char *kActionReset = "reset-defaults";
inline constexpr const char *kActionResetYes = "reset-yes";
inline constexpr const char *kActionResetNo = "reset-no";
inline constexpr const char *kActionReturn = "return";
inline constexpr const char *kActionLeaveDiscard = "leave-discard";
inline constexpr const char *kActionLeaveStay = "leave-stay";
inline constexpr const char *kActionToggle = "toggle";
inline constexpr const char *kActionAdopt = "adopt";
inline constexpr const char *kActionBindCapture = "bind-capture";
inline constexpr const char *kActionBindCaptureBegin =
    "bind-capture-begin"; // to the module
inline constexpr const char *kActionBindClear = "bind-clear";
inline constexpr const char *kActionBindRemove = "bind-remove";
inline constexpr const char *kActionBindReset = "bind-reset-defaults";
inline constexpr const char *kActionBindResetYes = "bind-reset-yes";
inline constexpr const char *kActionBindResetNo = "bind-reset-no";
inline constexpr const char *kActionBindConflictYes = "bind-conflict-yes";
inline constexpr const char *kActionBindConflictNo = "bind-conflict-no";
inline constexpr const char *kActionVideoApply = "video-apply";
inline constexpr const char *kActionVideoWindowed = "video-toggle-windowed";
inline constexpr const char *kActionVoiceTest = "voice-test";
inline constexpr const char *kActionVoiceBoost = "voice-mic-boost";
inline constexpr const char *kActionSprayChoose = "spray-choose";
inline constexpr const char *kActionSprayOpen = "spray-file-open";
inline constexpr const char *kActionSprayUp = "spray-file-up";
inline constexpr const char *kActionSprayRoot = "spray-file-root";
inline constexpr const char *kActionSprayConfirm = "spray-file-confirm";
inline constexpr const char *kActionSprayCancel = "spray-file-cancel";
inline constexpr const char *kLayerReset = "reset-confirm";
inline constexpr const char *kLayerLeave = "leave-confirm";
inline constexpr const char *kLayerBindConflict = "bind-conflict";
inline constexpr const char *kLayerBindReset = "bind-reset-confirm";
inline constexpr const char *kLayerSprayFile = "spray-file";
Rml::Element *actionNode(Rml::Element *node);
std::string translated(const char *token);
std::string numberText(float value);
bool isOn(float value);
bool parseNumber(const char *text, float &out);
bool isToggleKind(services::settings::SettingKind kind);
bool isSelectKind(services::settings::SettingKind kind);
bool toggleDraftOn(const services::settings::RowView &view);
const char *safeFocusForLayer(const char *id);
// The engine stores cl_logocolor as a localized token ("#Valve_Ltblue") while
// the shipped option table uses bare names ("Ltblue"); both spellings select
// the same spray colour, so labels and the preview URI normalize to the bare
// name. Other values pass through unchanged.
std::string logoColorName(const std::string &value);

// The section components that own one singleton widget block in
// settings.rml: component name -> block element id. Each claimed block is
// moved into the card of the section that declares it; an unclaimed block
// waits hidden in the widget host. user-script is a page behaviour and has
// no block. `Preview` selects the placement: a preview is a narrow box in the
// card's right column (the rows keep a 232dp column beside it), anything else
// is a full-width block above the rows.
struct WidgetBlock {
  const char *Component;
  const char *Element;
  bool Preview;
};
extern const WidgetBlock kWidgetBlocks[];
extern const int kWidgetBlockCount;
} // namespace panels::settings::detail
