#include "dynamic_loader.hpp"

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace opendoc::plugins::detail {
    namespace fs = std::filesystem;

    void *library_open(const fs::path &path, std::string &error_out) {
#if defined(_WIN32)
        HMODULE mod = LoadLibraryW(path.wstring().c_str());
        if (!mod) {
            const DWORD err = GetLastError();
            error_out = "LoadLibrary failed (error " + std::to_string(err) + "): " +
                        path.string();
            return nullptr;
        }

        return static_cast<void *>(mod);
#else
        // Clear any stale error so the dlerror() below reports this call only.
        // RTLD_NOW resolves everything up front so a missing dependency fails here
        // rather than at the first hook call, and RTLD_LOCAL keeps plugin symbols
        // out of the global namespace so two plugins cannot collide.
        dlerror();
        void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char *msg = dlerror();
            error_out = msg ? msg : ("dlopen failed: " + path.string());
            return nullptr;
        }

        return handle;
#endif
    }

    void *library_symbol(void *handle, const char *name) {
        if (!handle) {
            return nullptr;
        }

#if defined(_WIN32)
        return reinterpret_cast<void *>(
            GetProcAddress(static_cast<HMODULE>(handle), name));
#else
        dlerror();
        return dlsym(handle, name);
#endif
    }

    void library_close(void *handle) {
        if (!handle) {
            return;
        }

#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }
}
