#pragma once

// panels::NoticePanel - centerprint/notify text with the keep_time countdown
// (old CNoticePanel). Two instances: notice (PRINTNOTIFY, top-left) and
// notice_center (PRINTCENTER, screen-center); fed by data::textfeed::Notice.

#include "../../framework/HudPanel.h"

namespace panels {

class NoticePanel : public framework::HudPanel {
public:
  // target = the HUDNOTICE value this instance serves (1 notify / 4 center).
  NoticePanel(const char *name, const char *documentPath, int target);

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void reset() override;

private:
  int Target = 0;
  double CloseTime = 0.0;
  double ClientTimeNow = 0.0;
};

} // namespace panels
