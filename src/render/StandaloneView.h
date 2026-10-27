#pragma once

class IStandaloneViewManager;

namespace framework::standaloneview {

// Optional independent plugin. A non-null manager guarantees API_003
// drawfuncs in its triangle callbacks. Never falls back to API_002.
IStandaloneViewManager *manager();

} // namespace framework::standaloneview
