#pragma once

#include "opendoc/plugins/plugin.hpp"

namespace opendoc::plugins::builtin {
    // Builds the full-text search index plugin.
    [[nodiscard]] PluginFactory search_factory();

    // Builds the XML sitemap plugin.
    [[nodiscard]] PluginFactory sitemap_factory();

    // Builds the RSS 2.0 feed plugin.
    [[nodiscard]] PluginFactory rss_factory();
}
