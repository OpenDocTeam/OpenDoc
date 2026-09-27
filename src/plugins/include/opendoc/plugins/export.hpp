#pragma once

// ABI version shared by the host and every plugin binary. It is exported as a
// symbol and checked before a plugin is used, so bump it whenever the Plugin
// interface or any type crossing the boundary changes layout.
#define OPENDOC_PLUGIN_API_VERSION 1

// Marks symbols that must be visible across the shared library boundary, since
// plugins are opened by name lookup rather than linked at build time.
#if defined(_WIN32)
#define OPENDOC_PLUGIN_EXPORT __declspec(dllexport)
#else
#define OPENDOC_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
