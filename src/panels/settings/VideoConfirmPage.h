#pragma once

// panels::settings::VideoConfirmPage - the T12 unconfirmed-display-mode
// dialog (one interactive document, videoconfirm.rml).
//
// Shown by the gameui module whenever services::settings::VideoModeModel is
// awaiting confirmation (in-session apply or startup recovery of a pending
// record). It is a PROJECTION of the model: the headline is the ACTUAL mode
// the engine reports right now - never the requested one (task c: "显示实
// 际模式而非请求值") - plus the monotonic countdown and the three outcome
// shapes (applied / not-applied / unreadable). The page itself decides
// nothing: Keep/Revert report fixed intent names through the sink
// ("video-keep" / "video-revert") and the module runs the model transaction.
//
// Safe default focus sits on Restore (the non-committing entry); while the
// model reports NOT-applied or unreadable the Keep button is disabled in the
// projection (the model refuses the confirmation anyway - this only makes
// the refusal visible).

#include "../../framework/InteractiveDocument.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace Rml {
class Event;
}

namespace services {
namespace settings {
class VideoModeModel;
}
} // namespace services

namespace panels {
namespace settings {

using ActionSink = std::function<void(const char *action)>;

class VideoConfirmPage final : public framework::InteractiveDocument {
public:
  VideoConfirmPage();
  ~VideoConfirmPage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }
  void setVideoModel(services::settings::VideoModeModel *model) {
    Model = model;
  }

  // Re-projects the model (actual mode, kind, countdown). Called by the
  // module on show, on second ticks and on outcome changes.
  void render();

  // Safe default focus (module's focus guard): the Restore entry.
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class PageListener final : public Rml::EventListener {
  public:
    explicit PageListener(VideoConfirmPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    VideoConfirmPage *Owner;
  };

  void handleClick(Rml::Event &event);
  void setText(const char *id, const std::string &text);

  PageListener Listener;
  ActionSink Sink;
  services::settings::VideoModeModel *Model = nullptr;
};

} // namespace settings
} // namespace panels
