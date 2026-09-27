#pragma once

#include <filesystem>
#include <string>

namespace opendoc::plugins::detail {
    // Opens a shared library and returns an opaque handle, or nullptr with the
    // platform's failure reason formatted into error_out.
    void *library_open(const std::filesystem::path &path, std::string &error_out);

    // Resolves an exported symbol; returns nullptr for a null handle or a missing name.
    void *library_symbol(void *handle, const char *name);

    // Closes a handle from library_open; a null handle is a no-op.
    void library_close(void *handle);
}
