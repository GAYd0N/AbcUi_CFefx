#pragma once

// panels::loading - the T14 connection surfaces:
//
//   LoadingPage - the kLoading page: primary/secondary progress from the
//   engine stream (services::loading::LoadingModel), the status/resource
//   lines, and the once-only Cancel entry. Unknown totals stay visibly
//   unknown (no fill, no percentage) - never a fabricated animation (T14
//   step b.2).
//
//   ConnectionErrorOverlay - the kConnectionError overlay: the plain-text
//   failure reason (controller-owned copy, escaped before the DOM), Retry
//   (a NEW attempt generation through the controller) and Return (back to
//   the no-map home).
//
//   PasswordOverlay - the kPassword overlay: the password retry prompt of a
//   live attempt (original ServerPasswordDialog parity: the value travels
//   only into the submitPassword call, never stored, never logged).
//
//   DirectConnectOverlay - the basic address entry ("不等待服务器浏览器"):
//   host or host:port, validated by the model's grammar before any engine
//   call. The browser (T18) will replace this entry with the full flow.
//
// Every window follows the T09 shell contract: one delegated click binding,
// fixed data-action names (never user strings), intents only - the module
// owns all visibility and every engine call. All dynamic text is escaped.

#include "../../framework/InteractiveDocument.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace Rml {
class Element;
class Event;
} // namespace Rml

namespace services {
namespace loading {
class LoadingModel;
}
} // namespace services

namespace panels::loading {

using ActionSink = std::function<void(const char *action)>;

class LoadingPage final : public framework::InteractiveDocument {
public:
  LoadingPage();
  ~LoadingPage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }
  void setModel(const services::loading::LoadingModel *model) {
    Model = model;
  }

  // T19: the attempt-kind title seam. The local-game start shows the
  // create-game title, a map change the map-load title, and a connect the
  // shipping connect title. All are "#tokens" resolved through the project
  // table at render time (never markup).
  void setTitleToken(const char *token) {
    TitleToken = token ? token : "";
  }
  // Fallback status while the engine has not streamed one yet. The connect
  // wording ("Contacting the server...") would be wrong during a changelevel.
  void setStatusToken(const char *token) {
    StatusToken = token ? token : "";
  }

  // Projects the model's current values into the document. Called by the
  // module after each drained engine event and on presentation.
  void render();

  // Re-takes the Cancel focus (module's per-frame focus guard).
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class WindowListener final : public Rml::EventListener {
  public:
    explicit WindowListener(LoadingPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    LoadingPage *Owner;
  };

  void handle(Rml::Event &event);

  WindowListener Listener;
  ActionSink Sink;
  const services::loading::LoadingModel *Model = nullptr;
  // "#token" of the current title (connect vs local-game start; empty = the
  // shipping connect title).
  std::string TitleToken;
  // Fallback status token used until the engine streams status text; empty =
  // the shipping connect status.
  std::string StatusToken;
};

class ConnectionErrorOverlay final : public framework::InteractiveDocument {
public:
  ConnectionErrorOverlay();
  ~ConnectionErrorOverlay() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }

  // `reason` is the controller-space plain-text reason; resolved through the
  // local tables and escaped before it reaches the DOM.
  void showFor(const char *reason);
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class WindowListener final : public Rml::EventListener {
  public:
    explicit WindowListener(ConnectionErrorOverlay *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    ConnectionErrorOverlay *Owner;
  };

  void handle(Rml::Event &event);

  WindowListener Listener;
  ActionSink Sink;
};

class PasswordOverlay final : public framework::InteractiveDocument {
public:
  PasswordOverlay();
  ~PasswordOverlay() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }

  // Clears the field and updates the lead line; called once per opening
  // (`invalid` = the engine asked again after a rejected password).
  void prepare(bool invalid);

  // The typed password (empty when the field is missing). Not logged.
  std::string value() const;

  // The password field element - the dialog's keyboard target. The
  // per-frame focus guard keeps it focused while the overlay is up (a
  // click on a dialog button would otherwise strand the focus there and
  // characters typed afterwards would be dropped). Null before load.
  Rml::Element *editor() const;

  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class WindowListener final : public Rml::EventListener {
  public:
    explicit WindowListener(PasswordOverlay *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    PasswordOverlay *Owner;
  };

  void handle(Rml::Event &event);

  WindowListener Listener;
  ActionSink Sink;
};

class DirectConnectOverlay final : public framework::InteractiveDocument {
public:
  DirectConnectOverlay();
  ~DirectConnectOverlay() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }

  // Clears the address field for a fresh entry.
  void prepare();

  // The typed address (empty when the field is missing).
  std::string value() const;

  // The address field element - the dialog's keyboard target (see
  // PasswordOverlay::editor). Null before load.
  Rml::Element *editor() const;

  // Inline feedback for a refused entry (escaped; empty clears the line).
  void showHint(const char *text);
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class WindowListener final : public Rml::EventListener {
  public:
    explicit WindowListener(DirectConnectOverlay *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    DirectConnectOverlay *Owner;
  };

  void handle(Rml::Event &event);

  WindowListener Listener;
  ActionSink Sink;
};

} // namespace panels::loading
