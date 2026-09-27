#pragma once

#include "opendoc/mapper/config.hpp"
#include "opendoc/mapper/site_tree.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace opendoc::plugins {
    // Everything a plugin is handed during a build: read access to the site being
    // built, a safe way to write output files, and per-plugin scratch storage.
    class BuildContext {
    public:
        // Binds the context to the live config and site tree; site_root is the
        // directory that write_artifact resolves its relative paths against.
        BuildContext(const mapper::SiteConfig &config, mapper::SiteTree &site,
                     std::filesystem::path site_root);

        // The site configuration as loaded, including site_url and plugin list.
        [[nodiscard]] const mapper::SiteConfig &config() const noexcept {
            return config_;
        }

        // Mutable access to the site tree so plugins can add or adjust pages.
        [[nodiscard]] mapper::SiteTree &site() const noexcept {
            return site_;
        }

        // Root folder for generated output, i.e. where artifacts end up on disk.
        [[nodiscard]] const std::filesystem::path &site_root() const noexcept {
            return site_root_;
        }

        // Writes content below site_root_, rejecting absolute paths and '..' so a
        // plugin cannot escape the output directory.
        void write_artifact(std::string_view rel_path, std::string content) const;

        // Key/value bucket owned by one plugin, used to pass state between hooks.
        [[nodiscard]] std::map<std::string, std::string> &scratch(std::string_view plugin_name);

    private:
        // References into the caller's build state; the context does not own them.
        const mapper::SiteConfig &config_;
        mapper::SiteTree &site_;
        std::filesystem::path site_root_;
        std::map<std::string, std::map<std::string, std::string> > scratch_;
    };
}
