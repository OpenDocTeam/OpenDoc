#include "manifest.hpp"

#include <yaml-cpp/yaml.h>

namespace opendoc::plugins::manifest {
    PluginMetadata parse(const std::filesystem::path &manifest_path) {
        PluginMetadata meta;
        YAML::Node root;

        try {
            root = YAML::LoadFile(manifest_path.string());
        } catch (const std::exception &e) {
            // yaml-cpp reports IO and syntax errors here; rephrase them as manifest problems.
            throw PluginError("invalid plugin.json (" + manifest_path.string() + "): " +
                              e.what());
        }

        // A manifest must be a JSON object; a bare list or scalar has no fields to read.
        if (!root.IsMap()) {
            throw PluginError("plugin.json must be a JSON object: " +
                              manifest_path.string());
        }

        // Required string fields; a missing or non-scalar value is a manifest error.
        auto req = [&](const char *key) -> std::string {
            const auto n = root[key];
            if (!n || !n.IsScalar()) {
                throw PluginError(std::string("plugin.json missing required string '") +
                                  key + "': " + manifest_path.string());
            }

            return n.as<std::string>();
        };

        // Optional string fields; absent or non-scalar simply yields an empty value.
        auto opt = [&](const char *key) -> std::string {
            const auto n = root[key];
            if (!n || !n.IsScalar()) {
                return {};
            }

            return n.as<std::string>();
        };

        // The name doubles as a lookup key and path component, so reject empty names
        // and path separators before anything else derives paths from it.
        meta.name = req("name");
        if (meta.name.empty()) {
            throw PluginError("plugin.json 'name' must not be empty: " +
                              manifest_path.string());
        }
        if (meta.name.find('/') != std::string::npos ||
            meta.name.find('\\') != std::string::npos) {
            throw PluginError("plugin.json 'name' must not contain path separators: " +
                              meta.name);
        }

        meta.description = opt("description");
        meta.version = opt("version");
        meta.author = opt("author");
        meta.github = opt("github");
        meta.library = opt("library");
        meta.theme = opt("theme");
        // Remember the manifest's folder so the loader can find the library and theme.
        meta.root_dir = manifest_path.parent_path().string();
        return meta;
    }
}
