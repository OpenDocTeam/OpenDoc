#pragma once

#include "opendoc/mapper/scanner.hpp"
#include "opendoc/mapper/site_tree.hpp"
#include "opendoc/theme/theme.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace opendoc::app {
    // Gathers every path a generated page may legitimately link to, so dead links
    // can be detected without touching the filesystem.
    [[nodiscard]] std::unordered_set<std::string> collect_valid_link_targets(
        const mapper::SiteTree &site,
        const std::vector<mapper::ScannedStatic> &static_files,
        const std::vector<theme::Theme::Asset> &theme_assets);

    // Scans raw HTML for href/src attributes whose targets are not in valid_targets.
    // Fragment-only and external URLs are skipped; returns one issue per dead link.
    [[nodiscard]] std::vector<std::string> check_dead_links(
        const std::string &html, const mapper::Page &page,
        const std::unordered_set<std::string> &valid_targets);
}
