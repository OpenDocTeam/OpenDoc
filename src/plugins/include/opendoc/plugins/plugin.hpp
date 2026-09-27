#pragma once

#include "opendoc/plugins/export.hpp"
#include "opendoc/theme/template_engine.hpp"

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Forward declarations keep this header free of mapper and parser includes,
// which matters because every plugin compiles against it.
namespace opendoc::mapper {
    struct SiteConfig;
    struct Page;
}

namespace opendoc::parser {
    struct ParsedPage;
}

namespace opendoc::plugins {
    class BuildContext;

    // Error type thrown by plugin-facing code, so failures can be told apart from
    // internal errors and reported with a plugin name attached.
    class PluginError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // What a plugin declares in plugin.json; also used to describe built-ins.
    struct PluginMetadata {
        // Name, description, version, author and github link as listed to users.
        std::string name;
        std::string description;
        std::string version;
        std::string author;
        std::string github;
        // Shared library file name inside the plugin folder; empty means probe.
        std::string library;
        // Name the plugin registers its theme directory under; empty means the name.
        std::string theme;
        // True for plugins compiled into the binary rather than loaded from disk.
        bool builtin = false;
        // Folder that held plugin.json, used to resolve library and theme paths.
        std::string root_dir;
    };

    // The interface a plugin implements. The manager calls the hooks in build
    // order; every hook has a no-op default so plugins override only what they need.
    class Plugin {
    public:
        // Virtual so built-in plugins deleted through a Plugin* are destroyed correctly.
        virtual ~Plugin() = default;

        // Stable identifier; must equal the "name" field in plugin.json.
        [[nodiscard]] virtual std::string name() const = 0;

        // Called once with the site config before any page work, for settings tweaks.
        virtual void on_config(mapper::SiteConfig & /*config*/) {
        }

        // Called once before the first page is processed.
        virtual void on_pre_build(BuildContext & /*ctx*/) {
        }

        // Lets a plugin rewrite a page's Markdown before it is parsed.
        virtual void on_page_markdown(BuildContext & /*ctx*/,
                                      const mapper::Page & /*page*/,
                                      std::string & /*markdown*/) {
        }

        // Lets a plugin inspect or mutate the parsed document tree before rendering.
        virtual void on_page_content(BuildContext & /*ctx*/,
                                     const mapper::Page & /*page*/,
                                     parser::ParsedPage & /*parsed*/) {
        }

        // Lets a plugin add or override variables in the page's template context.
        virtual void on_page_context(BuildContext & /*ctx*/,
                                     const mapper::Page & /*page*/,
                                     theme::TemplateContext & /*template_context*/) {
        }

        // Lets a plugin post-process the rendered HTML of a single page.
        virtual void on_post_page(BuildContext & /*ctx*/,
                                  const mapper::Page & /*page*/,
                                  std::string & /*html*/) {
        }

        // Called once after every page is written; the place to emit extra artifacts.
        virtual void on_post_build(BuildContext & /*ctx*/) {
        }
    };

    // Creates a fresh instance; each load calls its factory rather than reusing one.
    using PluginFactory = std::function<std::unique_ptr<Plugin>()>;
}
