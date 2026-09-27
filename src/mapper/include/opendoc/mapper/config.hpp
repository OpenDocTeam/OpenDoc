#pragma once

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace opendoc::mapper {
    // Error type thrown for missing, unparsable, or unsafe site configuration.
    class ConfigError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // One node of the navigation tree: a label, an optional target, and nested children.
    struct NavEntry {
        std::string title;
        std::string href;
        std::vector<NavEntry> children;
    };

    // Theme selection plus free-form options forwarded to the templates.
    struct ThemeConfig {
        std::string name = "default";
        std::filesystem::path custom_dir;
        std::map<std::string, std::string> options;
    };

    // A single hyperlink rendered in the site footer.
    struct FooterLink {
        std::string text;
        std::string href;
    };

    // A titled group of footer links.
    struct FooterColumn {
        std::string title;
        std::vector<FooterLink> links;
    };

    // One entry of the version switcher: display label and the URL it points at.
    struct VersionEntry {
        std::string label;
        std::string url;
    };

    // Everything read from opendoc.yaml, with defaults already applied.
    struct SiteConfig {
        std::string site_name = "OpenDoc";
        std::string site_description;
        std::string site_url;
        std::string logo;
        std::string footer;
        std::string favicon;
        std::string social_card;
        std::string edit_url;
        std::string github_url;
        std::string current_version;
        std::filesystem::path docs_dir{"docs"};
        std::filesystem::path site_dir{"site"};
        std::filesystem::path config_path;
        ThemeConfig theme;
        std::vector<std::string> plugins;
        std::vector<NavEntry> nav;
        std::vector<FooterColumn> footer_columns;
        std::vector<VersionEntry> versions;
        bool use_directory_urls = true;
        bool strict = false;

        // Parses the YAML file at yaml_path, applies defaults, and rejects unsafe paths.
        static SiteConfig load(const std::filesystem::path &yaml_path);

        // Absolute path of docs_dir, resolved against the config file location.
        [[nodiscard]] std::filesystem::path docs_root() const;

        // Absolute path of site_dir, resolved against the config file location.
        [[nodiscard]] std::filesystem::path site_root() const;
    };
}
