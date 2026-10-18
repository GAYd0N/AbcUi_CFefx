#pragma once

// panels::settings::sections::SpraySection - the spray card's own half: the
// file picker's state and DOM (the directory model lives in
// services::filedialog::FileBrowser), the maker's request/answer seam and the
// preview of the spray just made.
//
// Seam: the section never decodes, encodes, writes or uploads anything. A
// confirmed pick travels to the module through RequestSink; the module answers
// with setResult() and the Rml source of the preview texture it uploaded. The
// classic spray preview (logo + colour) stays in PreviewSection.

#include "services/filedialog/FileBrowser.h"

#include <functional>
#include <string>

namespace Rml {
class Element;
class ElementDocument;
} // namespace Rml

namespace panels::settings::sections {

class SpraySection {
public:
  using RequestSink = std::function<void(const std::string &path)>;

  SpraySection();
  SpraySection(const SpraySection &) = delete;
  SpraySection &operator=(const SpraySection &) = delete;

  void setRequestSink(RequestSink sink) {
    Request = std::move(sink);
  }

  // Opens the picker at the game folder (the engine's working directory,
  // where the original dialog started too) and projects its first list.
  void open(Rml::ElementDocument *document);
  // Drops the picker state (layer dismissed by the page or a leave).
  void close(Rml::ElementDocument *document);
  bool browsing() const {
    return BrowseOpen;
  }

  // One picker intent from the page's click handler (data-action plus
  // data-entry/data-root on the clicked element).
  void handleAction(const std::string &action, Rml::Element *entry,
                    Rml::ElementDocument *document);

  // The maker's answer from the module.
  void setResult(bool ok, const std::string &message);
  // Rml source of the made spray's preview texture (the module uploads it);
  // an empty uri clears the preview.
  void setPreviewUri(std::string uri) {
    PreviewUri = std::move(uri);
  }
  // Maker status + preview uri, projected by the page on every render.
  const std::string &statusText() const {
    return Message;
  }
  bool hasPreview() const {
    return !PreviewUri.empty();
  }
  const std::string &previewUri() const {
    return PreviewUri;
  }

  // Rebuilds the picker DOM (path, roots, list, status).
  void renderBrowser(Rml::ElementDocument *document);

private:
  services::filedialog::FileBrowser Browser;
  RequestSink Request;
  // The selected file inside the current directory (empty: nothing picked).
  std::string Selected;
  std::string Message;
  std::string PreviewUri;
  bool BrowseOpen = false;
};

} // namespace panels::settings::sections
