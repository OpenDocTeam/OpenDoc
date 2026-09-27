#pragma once

#include "opendoc/mapper/config.hpp"
#include "opendoc/mapper/link_resolver.hpp"
#include "opendoc/mapper/site_tree.hpp"
#include "opendoc/plugins/build_context.hpp"
#include "opendoc/plugins/plugin_manager.hpp"
#include "opendoc/theme/theme.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace opendoc::app {
    // Everything one page render needs; all members are references to data owned
    // by the caller, which must outlive the render.
    struct PageRenderContext {
        const mapper::SiteConfig &config;
        const mapper::SiteTree &site;
        const theme::Theme &theme;
        plugins::PluginManager &plugins;
        plugins::BuildContext &plugin_ctx;
        const mapper::LinkResolver &resolver;
        const std::vector<const mapper::Page *> &page_order;
        const std::unordered_set<std::string> &valid_targets;
        const std::string &versions_html;
    };

    // Flattens the nav tree depth-first into the order used for prev/next links.
    [[nodiscard]] std::vector<const mapper::Page *> nav_page_order(
        const mapper::SiteTree &site);

    // Rendered HTML plus any dead links found while scanning that HTML.
    struct PageRenderResult {
        std::string html;
        std::vector<std::string> dead_links;
    };

    // Converts one page's markdown into final HTML; front matter is written back into
    // page, so it must be a mutable reference. Throws BuildError on parse/template errors.
    [[nodiscard]] PageRenderResult render_page(const PageRenderContext &rc,
                                               mapper::Page &page, std::string markdown);
}
