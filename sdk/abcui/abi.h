#pragma once

// sdk/abcui/abi.h - ABI contract between the AbcUi framework and UI modules.
//
// The module ABI is pure C: versioned struct tables of function pointers and
// POD payloads (the same pattern as AbcCore_CreateInterface). A module DLL
// exports exactly one symbol, AbcUiModule_Main (see module_api.h); everything
// else crosses the boundary through the AbcUiModuleApi table handed to that
// entry. Modules never include RmlUi headers and never link the framework's
// static libraries.
//
// Extensibility rules (mirroring AbcCore's append-only table):
//  - every struct starts with a uint32_t structSize;
//  - new functions are appended, existing members never move or change
//    signature; breaking changes bump ABCUI_ABI_VERSION;
//  - the framework fills every member it knows; a module null-checks any
//    member it uses beyond what its own header knows (a framework may be
//    older than the module's SDK copy).
//
// Module build constraints (MemoryDll): Win32 x86 DLL, dynamic CRT (/MD),
// no static TLS (`__declspec(thread)`), no delayimp, system DLLs/CRT only in
// the import table. Framework services arrive exclusively through the api
// table, so no import library is needed.

// Bumped on any breaking change to the structs below.
#define ABCUI_ABI_VERSION 1

#ifdef __cplusplus
#define ABCUI_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define ABCUI_MODULE_EXPORT __declspec(dllexport)
#endif
