#include "builtin.hpp"

#include "opendoc/plugins/plugin_manager.hpp"

namespace opendoc::plugins::builtin {
    namespace {
        // Registers a built-in with a fixed manifest stub, so built-ins show up in
        // listings with the same metadata shape as plugins read from plugin.json.
        void add(PluginManager &mgr, std::string name, std::string description,
                 PluginFactory factory) {
            PluginMetadata meta;
            meta.name = std::move(name);
            meta.description = std::move(description);
            meta.version = "1.0.0";
            meta.author = "OpenDoc";
            meta.builtin = true;
            mgr.add_builtin(std::move(meta), std::move(factory));
        }
    }
}

namespace opendoc::plugins {
    void register_builtin_plugins(PluginManager &mgr) {
        builtin::add(mgr, "search", "Built-in full-text search index",
                     builtin::search_factory());
        builtin::add(mgr, "sitemap", "Built-in XML sitemap",
                     builtin::sitemap_factory());
        builtin::add(mgr, "rss", "Built-in RSS 2.0 feed", builtin::rss_factory());
    }
}
