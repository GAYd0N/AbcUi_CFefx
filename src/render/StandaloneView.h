#pragma once

class IStandaloneViewManager;

namespace framework::standaloneview {

// Optional independent plugin. A non-null manager guarantees API_003
// drawfuncs in its triangle callbacks; only API_003 is exported upstream.
IStandaloneViewManager *manager();

} // namespace framework::standaloneview
