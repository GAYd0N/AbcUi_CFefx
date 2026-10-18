#pragma once
// Stateless microphone controls projection. Draft editing remains in
// VoiceModel.
namespace Rml {
class ElementDocument;
class Element;
} // namespace Rml
namespace services::settings {
class VoiceModel;
}
namespace panels::settings::sections {
void renderVoice(Rml::ElementDocument *Document,
                 services::settings::VoiceModel *Model,
                 Rml::Element *SkipField);
}
