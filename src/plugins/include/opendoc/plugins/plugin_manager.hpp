#pragma once

#include "opendoc/plugins/plugin.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Forward declarations keep the manager header light; the hook signatures only
// ever take these types by reference.
namespace opendoc::mapper {
    struct SiteConfig;
    struct Page;
}

namespace opendoc::parser {
    struct ParsedPage;
}

namespace opendoc::theme {
    class TemplateContext;
}

namespace opendoc::plugins {
    class BuildContext;

    // Finds plugin folders on disk, loads the ones the site config asks for, and
    // fans each build hook out across them in load order.
    class PluginManager {
    public:
        // Registers the built-in plugins so they are known before discovery runs.
        PluginManager();

        // Unloads anything still active, closing shared libraries.
        ~PluginManager();

        // Deleted: copying would double-free the plugin objects and library handles.
        PluginManager(const PluginManager &) = delete;

        // Deleted for the same ownership reason as the copy constructor.
        PluginManager &operator=(const PluginManager &) = delete;

        // Scans plugins_dir for subfolders containing a plugin.json and records
        // what it finds (including problems) without loading anything yet.
        void discover(const std::filesystem::path &plugins_dir);

        // Instantiates every plugin named in config, in the order listed, failing
        // the build on duplicates, conflicts, or unknown names.
        void load(const mapper::SiteConfig &config);

        // Destroys active plugins in reverse load order and closes their libraries.
        void unload();

        // Runs on_config on every loaded plugin, wrapping any escape in PluginError.
        void fire_on_config(mapper::SiteConfig &config);

        // Runs on_pre_build on every loaded plugin before any page is processed.
        void fire_on_pre_build(BuildContext &ctx);

        // Runs on_page_markdown on every loaded plugin for the given page.
        void fire_on_page_markdown(BuildContext &ctx, const mapper::Page &page,
                                   std::string &markdown);

        // Runs on_page_content on every loaded plugin for the given page.
        void fire_on_page_content(BuildContext &ctx, const mapper::Page &page,
                                  parser::ParsedPage &parsed);

        // Runs on_page_context on every loaded plugin for the given page.
        void fire_on_page_context(BuildContext &ctx, const mapper::Page &page,
                                  theme::TemplateContext &template_context);

        // Runs on_post_page on every loaded plugin for the given page.
        void fire_on_post_page(BuildContext &ctx, const mapper::Page &page,
                               std::string &html);

        // Runs on_post_build on every loaded plugin after the site is written.
        void fire_on_post_build(BuildContext &ctx);

        // Names a config may list: built-ins plus discovered folders, deduplicated.
        [[nodiscard]] std::vector<std::string> available_names() const;

        // Non-fatal problems noticed while discovering or loading, for the caller to report.
        [[nodiscard]] const std::vector<std::string> &warnings() const noexcept {
            return warnings_;
        }

        // Finds the theme directory of a loaded plugin by the name it registered.
        [[nodiscard]] std::optional<std::filesystem::path> theme_directory(
            const std::string &theme_name) const;

        // One successfully instantiated plugin plus everything needed to tear it down.
        struct Loaded {
            // Manifest copy and the live instance this entry owns.
            PluginMetadata meta;
            Plugin *raw = nullptr;

            // Destructor exported by the plugin's library; null means plain delete.
            void (*destroy)(Plugin *) = nullptr;

            // dlopen/LoadLibrary handle, closed after destroy; null for built-ins.
            void *handle = nullptr;
            // Theme folder this plugin contributed, empty if it has none.
            std::filesystem::path theme_dir;
            std::string theme_name;
        };

        // Directory holding the running executable, the base for default paths.
        [[nodiscard]] static std::filesystem::path executable_directory();

        // The plugins folder next to the executable where installs are looked for.
        [[nodiscard]] static std::filesystem::path default_plugins_directory();

        // Adds an in-process plugin under meta.name, marking it built-in.
        void add_builtin(PluginMetadata meta, PluginFactory factory);

    private:
        // A plugin folder seen on disk during discover(), not yet loaded.
        struct Discovered {
            // Parsed manifest plus where the plugin's files live.
            PluginMetadata meta;
            std::filesystem::path dir;
            std::filesystem::path library;
            // Theme folder and registered name, both empty when none was declared.
            std::filesystem::path theme_dir;
            std::string theme_name;
            // Set when another folder or a built-in claims the same name.
            bool name_conflict = false;
        };

        std::vector<Discovered> discovered_;
        std::vector<Loaded> active_;
        std::vector<std::string> warnings_;
        std::map<std::string, std::pair<PluginMetadata, PluginFactory> > builtins_;

        // Destroys a plugin then closes its library; safe to call more than once.
        void release_loaded(Loaded &ld);

        // Opens a discovered plugin's library, checks the ABI, and instantiates it.
        void load_dynamic(const Discovered &d);

        // Instantiates a built-in from the factory registry; no library involved.
        void load_builtin(const std::string &name);

        // Picks which shared library file to load for a discovered plugin, or an
        // empty path if nothing unambiguous was found.
        [[nodiscard]] std::filesystem::path resolve_library(const Discovered &d) const;
    };

    // Registers search, sitemap and rss with a manager; called by its constructor.
    void register_builtin_plugins(PluginManager &mgr);
}
