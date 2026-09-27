#pragma once

#include "opendoc/mapper/config.hpp"
#include "opendoc/mapper/site_tree.hpp"
#include "opendoc/theme/template_engine.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace opendoc::theme {
    // Raised for theme load failures such as a missing or unreadable layout.
    class ThemeError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // Holds a theme layout plus the static assets copied into the site output.
    class Theme {
    public:
        // A single file written to the site, addressed by its output relative path.
        struct Asset {
            std::string out_rel_path;
            std::string content;
        };

        // Resolves a theme config to a loaded theme, overlaying plugin or custom files.
        static Theme load(const mapper::ThemeConfig &cfg,
                          const std::optional<std::filesystem::path> &plugin_theme =
                                  std::nullopt);

        // Loads a theme entirely from a directory on disk, collecting its assets.
        static Theme load_directory(const std::string &name,
                                    const std::filesystem::path &dir);

        // Expands the layout template with the page context to produce final HTML.
        [[nodiscard]] std::string render_page(const TemplateContext &ctx) const;

        // Builds the sidebar navigation HTML, marking the branch containing current.
        [[nodiscard]] static std::string render_nav(const mapper::SiteTree &site,
                                                    const mapper::Page &current);

        // Builds the header navigation from top level sections, empty when none exist.
        [[nodiscard]] static std::string render_top_nav(const mapper::SiteTree &site,
                                                        const mapper::Page &current);

        // Returns a reference so callers do not copy the whole asset payload.
        [[nodiscard]] const std::vector<Asset> &assets() const noexcept {
            return assets_;
        }

        // Returns the theme name as configured, defaulting to "default".
        [[nodiscard]] const std::string &name() const noexcept {
            return name_;
        }

    private:
        // Compiled layout template; empty until load succeeds.
        TemplateEngine layout_{""};
        std::string name_ = "default";
        std::vector<Asset> assets_;
    };
}
