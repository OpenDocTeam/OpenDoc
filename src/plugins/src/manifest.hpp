#pragma once

#include "opendoc/plugins/plugin.hpp"

#include <filesystem>

namespace opendoc::plugins::manifest {
    // Reads a plugin.json into a PluginMetadata, throwing PluginError when the file
    // is unreadable, not an object, or missing the required name.
    [[nodiscard]] PluginMetadata parse(const std::filesystem::path &manifest_path);
}
