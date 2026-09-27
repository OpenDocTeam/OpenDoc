#pragma once

#include "opendoc/mapper/config.hpp"
#include "opendoc/mapper/site_tree.hpp"
#include "opendoc/plugins/plugin_manager.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace opendoc::app {
    // Counters and messages accumulated over one full site build.
    struct BuildStats {
        std::size_t pages_written = 0;
        std::size_t assets_written = 0;
        std::size_t artifacts_written = 0;
        std::chrono::milliseconds elapsed{0};
        std::vector<std::string> warnings;
    };

    // Fatal build failure; aborts the build with a message meant for the user.
    class BuildError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // Drives a complete site build: scan, validate, render, and write output.
    class Builder {
    public:
        // Takes ownership of the site config; plugins are discovered lazily in build().
        explicit Builder(mapper::SiteConfig config);

        // Renders every page and asset into site_root; throws BuildError on any fatal problem.
        BuildStats build();

    private:
        mapper::SiteConfig config_;
        plugins::PluginManager plugins_;
    };
}
