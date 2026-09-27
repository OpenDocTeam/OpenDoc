#pragma once

#include "opendoc/mapper/config.hpp"
#include "opendoc/mapper/scanner.hpp"

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace opendoc::mapper {
    // Error type thrown when the built site fails validation (dup URLs, broken nav).
    class ValidationError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // One document: its source locations, output URL, and derived metadata.
    struct Page {
        std::string title;
        std::string description;
        std::filesystem::path abs_source;
        std::filesystem::path rel_source;
        std::filesystem::path abs_output;
        std::string url;
        std::vector<std::string> breadcrumbs;
        std::map<std::string, std::string> front_matter;
        std::string html_fragment;
        std::string plain_text;
    };

    // A navigation tree node; page is null for pure grouping/section entries.
    struct SiteNode {
        std::string title;
        const Page *page = nullptr;
        std::vector<SiteNode> children;
    };

    // Index of every page plus the navigation tree, built from config and a scan.
    class SiteTree {
    public:
        // Assigns URLs and output paths, then derives the nav tree from config.
        static SiteTree build(const SiteConfig &config, const ScanResult &scan);

        // Mutable page list, used by later build stages to fill in rendered content.
        [[nodiscard]] std::vector<Page> &pages() noexcept {
            return pages_;
        }

        // Read-only view of every page in scan (sorted) order.
        [[nodiscard]] const std::vector<Page> &pages() const noexcept {
            return pages_;
        }

        // Root of the navigation tree; its title is the site name.
        [[nodiscard]] const SiteNode &nav_root() const noexcept {
            return nav_root_;
        }

        // Page whose source path (relative to docs_dir) is `rel`, or nullptr.
        [[nodiscard]] const Page *find_by_rel_source(std::string_view rel) const noexcept;

        // Page owning the exact output URL `url`, or nullptr when unmapped.
        [[nodiscard]] const Page *find_by_url(std::string_view url) const noexcept;

        // Throws ValidationError listing duplicate URLs, dead nav links, orphans.
        void validate(const SiteConfig &config) const;

    private:
        std::vector<Page> pages_;
        SiteNode nav_root_;
        std::map<std::string, std::size_t> by_url_;
        std::map<std::string, std::size_t> by_rel_;
    };
}
