#include "opendoc/plugins/build_context.hpp"
#include "opendoc/plugins/plugin.hpp"

#include <fstream>
#include <utility>

namespace opendoc::plugins {
    BuildContext::BuildContext(const mapper::SiteConfig &config, mapper::SiteTree &site,
                               std::filesystem::path site_root)
        : config_(config), site_(site), site_root_(std::move(site_root)) {
    }

    void BuildContext::write_artifact(const std::string_view rel_path,
                                      const std::string content) const {
        const std::filesystem::path rel(rel_path);
        // Artifacts must stay inside site_root_: refuse absolute paths and any '..'
        // segment before joining, otherwise a plugin could write anywhere on disk.
        if (rel.is_absolute()) {
            throw PluginError("write_artifact path must be relative: " +
                              std::string(rel_path));
        }

        for (const auto &part: rel) {
            if (part == "..") {
                throw PluginError("write_artifact path must not contain '..': " +
                                  std::string(rel_path));
            }
        }

        const auto out = site_root_ / rel;
        std::error_code ec;
        // Create parent folders so plugins can emit nested paths without setup.
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream ofs(out, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            throw PluginError("failed to write artifact: " + out.string());
        }

        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));

        if (!ofs) {
            throw PluginError("failed to write artifact: " + out.string());
        }
    }

    std::map<std::string, std::string> &BuildContext::scratch(const std::string_view plugin_name) {
        return scratch_[std::string(plugin_name)];
    }
}
