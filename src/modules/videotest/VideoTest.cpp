// New Debug-only <video> verification window. Mirrors the model test harness:
// real documents, real attributes, production decode and upload path, and a
// console dump of what each element currently holds. Debug-only by build, the
// element itself ships in Release.
#ifndef NDEBUG
#include <metahook.h>
#include "../Module.h"
#include "../../rmlui/video/ElementVideo.h"
#include "../../services/video/AsyncDecoder.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <Windows.h>
#include <string>

namespace videotest {
namespace {
Rml::Context *Context = nullptr;
Rml::ElementDocument *Document = nullptr;

void close() {
  if (Document)
    Document->Close();
  Document = nullptr;
}

void show() {
  if (!Context)
    return;
  close();
  Document = Context->LoadDocumentFromMemory(R"(
<rml><head><style>
body { position:absolute; left:20px; top:40px; width:1000px; height:560px;
 background-color:#0d1114; color:#ffffff; font-family:Verdana; font-size:16px;
 padding:12px; z-index:2000; }
video { display:inline-block; width:660px; height:360px; margin:6px; overflow:hidden; position:relative;
 border:1px #3a4148; background-color:#000000; }
video#small { width:300px; height:180px; }
div { display:block; }
.row { display:flex; align-items:flex-start; }
</style></head><body id="video-test">
<div>Video element: abc/abcui/gameui/scence/video/trainstation.mp4 (loop, cover)</div>
<div class="row">
<video id="main" src="abc/abcui/gameui/scence/video/trainstation.mp4" fit="cover" loop="true"/>
<video id="small" src="abc/abcui/gameui/scence/video/viewport.mp4" fit="contain" loop="true"/>
</div>
<div>abc_ui_video_test hide | status | set id attribute value | src id path</div>
</body></rml>)");
  if (Document)
    Document->Show();
}

void command() {
  const std::string Action =
      gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv(1) : "show";
  if (Action == "show")
    show();
  else if (Action == "hide")
    close();
  else if (Action == "set" && Document && gEngfuncs.Cmd_Argc() >= 5) {
    if (auto *Element = Document->GetElementById(gEngfuncs.Cmd_Argv(2))) {
      std::string Value = gEngfuncs.Cmd_Argv(4);
      for (int i = 5; i < gEngfuncs.Cmd_Argc(); ++i)
        Value += std::string(" ") + gEngfuncs.Cmd_Argv(i);
      Element->SetAttribute(gEngfuncs.Cmd_Argv(3), Value);
    }
  }
  if (Action == "perf") {
    const auto Perf = rmlui::video::perfSnapshot();
    abc::ConsolePrint("[AbcUi video] perf frames=%d avg=%.3fms max=%.3fms\n",
                      Perf.Frames, Perf.AverageMs, Perf.MaximumMs);
    return;
  }
  abc::ConsolePrint("[AbcUi video] sdk=%d window=%d\n",
                    video::Decoder::available() ? 1 : 0, Document != nullptr);
  if (Document) {
    for (const char *Id : {"main", "small"}) {
      auto *Element = dynamic_cast<rmlui::video::ElementVideo *>(
          Document->GetElementById(Id));
      if (!Element)
        continue;
      const auto &Frame = Element->currentFrame();
      abc::ConsolePrint("[AbcUi video] %s frame=%dx%d seconds=%.3f\n", Id,
                        Frame.Width, Frame.Height, Element->currentSeconds());
    }
  }
}

class Module : public modules::IUiModule {
public:
  Module() {
    modules::registry().add(this);
  }
  const char *name() const override {
    return "videotest";
  }
  void onInit() override {
    abc::ADD_COMMAND("abc_ui_video_test", command);
  }
  void onContextReady(Rml::Context *Value) override {
    Context = Value;
  }
  void onShutdown() override {
    close();
    Context = nullptr;
  }
};
Module Instance;
} // namespace
} // namespace videotest
#endif
