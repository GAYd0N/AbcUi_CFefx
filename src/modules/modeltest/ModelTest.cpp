// New Debug-only test window. Commands exercise real DOM attributes and the
// production preview renderer, including cold startup before any map exists.
#ifndef NDEBUG
#include <metahook.h>
#include "../Module.h"
#include "../../rmlui/model/ElementModel.h"
#include "../../rmlui/modelpreview/Target.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <string>

namespace modeltest {
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
body { position:absolute; left:30px; top:55px; width:720px; height:520px;
 background-color:#18202a; color:#ffffff; font-family:Verdana; font-size:16px;
 padding:15px; z-index:2000; }
model { display:inline-block; width:340px; height:440px; margin:5px; }
div { display:block; }
.views { display:flex; align-items:flex-start; }
</style></head><body id="model-test">
<div>Studio preview: models/player.mdl — independent views</div>
<div class="views">
<model id="front" src="models/player.mdl" background="0.10 0.12 0.16 1"/>
<model id="side" src="models/player.mdl" angles="0 90 0"
 ambient="40" shade="160" light-color="0.5 0.7 1" background="0.18 0.12 0.10 1"/>
</div>
<div>abc_ui_model_test hide | status | set front/side attribute value</div>
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
  } else if (Action == "remove" && Document && gEngfuncs.Cmd_Argc() == 4) {
    if (auto *Element = Document->GetElementById(gEngfuncs.Cmd_Argv(2)))
      Element->RemoveAttribute(gEngfuncs.Cmd_Argv(3));
  }
  abc::ConsolePrint("[AbcUi model] world=%d window=%d\n",
                    modelpreview::hasWorld(), Document != nullptr);
  if (Document) {
    for (const char *Id : {"front", "side"}) {
      auto *Element = static_cast<rmlui::model::ElementModel *>(
          Document->GetElementById(Id));
      if (Element)
        abc::ConsolePrint("[AbcUi model] %s texture=%u frames=%u\n", Id,
                          Element->texture(), Element->frames());
    }
  }
}

class Module : public modules::IUiModule {
public:
  Module() {
    modules::registry().add(this);
  }
  const char *name() const override {
    return "modeltest";
  }
  void onInit() override {
    abc::ADD_COMMAND("abc_ui_model_test", command);
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
} // namespace modeltest
#endif
