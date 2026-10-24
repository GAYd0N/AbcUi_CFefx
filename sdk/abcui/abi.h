#pragma once

#include <stddef.h>

// sdk/abcui/abi.h - ABI contract between the AbcUi framework and UI modules.
//
// The module ABI is pure C: versioned struct tables of function pointers and
// POD payloads (the same pattern as AbcCore_CreateInterface). A module DLL
// exports exactly one symbol, AbcUiModule_Main (see module_api.h); everything
// else crosses the boundary through the AbcUiModuleApi table handed to that
// entry. Modules never include RmlUi headers and never link the framework's
// static libraries.
//
// ABI 2 deliberately breaks the old resource/array APIs. There is no ABI-1
// adapter. Tables and snapshots retain size headers for capacity validation;
// array APIs take explicit byte strides. Initialize snapshot structSize to
// sizeof the caller's record. Breaking changes require another ABI bump.
// ABCUI_API_HAS_MEMBER is a table-validation helper, not a legacy fallback.
//
// Module build constraints (MemoryDll): Win32 x86 DLL, dynamic CRT (/MD),
// no static TLS (`__declspec(thread)`), no delayimp, system DLLs/CRT only in
// the import table. Framework services arrive exclusively through the api
// table, so no import library is needed.

// Bumped on any breaking change to the structs below.
#define ABCUI_ABI_VERSION 2

// All entries, callbacks and service pointers use x86 cdecl, independently
// of the consumer's /Gd, /Gz or /Gr default. Annotate callback definitions too.
#define ABCUI_CALL __cdecl

// Use a stable table pointer expression: it is evaluated more than once.
// Short-circuiting tests the size before reading the appended pointer.
#define ABCUI_API_HAS_MEMBER(type, api, member)                                \
  ((api) != NULL &&                                                            \
   (api)->structSize >= offsetof(type, member) + sizeof((api)->member) &&      \
   (api)->member != NULL)

#ifdef __cplusplus
#define ABCUI_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define ABCUI_MODULE_EXPORT __declspec(dllexport)
#endif
