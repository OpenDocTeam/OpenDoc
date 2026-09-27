#include "opendoc/plugins/build_context.hpp"
#include "opendoc/plugins/plugin.hpp"
#include "opendoc/plugins/plugin_manager.hpp"

#include "opendoc/parser/markdown_parser.hpp"

#include "dynamic_loader.hpp"
#include "manifest.hpp"

#include <algorithm>
#include <exception>
#include <set>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <cstdlib>
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace opendoc::plugins {
    namespace {
        namespace fs = std::filesystem;

        // Locates the running executable per platform, falling back to the working
        // directory when the OS refuses to say, so default plugin paths still resolve.
        fs::path executable_dir() {
#if defined(_WIN32)
            wchar_t buf[32768];
            const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
            if (n > 0 && n < 32768) {
                return fs::path(buf).parent_path();
            }
#elif defined(__APPLE__)
            char buf[PATH_MAX];
            uint32_t size = sizeof(buf);
            if (_NSGetExecutablePath(buf, &size) == 0) {
                std::error_code ec;
                auto canon = fs::weakly_canonical(fs::path(buf), ec);
                return (ec ? fs::path(buf) : canon).parent_path();
            }
#else
            std::error_code ec;
            auto p = fs::read_symlink("/proc/self/exe", ec);
            if (!ec) {
                return p.parent_path();
            }
#endif

            return fs::current_path();
        }

        // Re-wraps whatever a plugin threw so the message names the plugin, and so
        // nothing but PluginError ever escapes the manager's hooks.
        [[noreturn]] void rethrow_as_plugin_error(const PluginMetadata &meta,
                                                  const std::exception_ptr &cause) {
            std::string prefix = "plugin '" + meta.name + "': ";

            try {
                std::rethrow_exception(cause);
            } catch (const PluginError &e) {
                throw PluginError(prefix + e.what());
            } catch (const std::exception &e) {
                throw PluginError(prefix + e.what());
            } catch (...) {
                throw PluginError(prefix + "threw a non-standard exception");
            }
        }

        // Runs a single hook on one plugin, tagging any thrown exception with the
        // plugin's name so a bad plugin is identifiable in the build log.
        template<typename Fn>
        void invoke(const PluginManager::Loaded &plugin, Fn &&fn) {
            try {
                fn(*plugin.raw);
            } catch (...) {
                rethrow_as_plugin_error(plugin.meta, std::current_exception());
            }
        }
    }

    PluginManager::PluginManager() {
        register_builtin_plugins(*this);
    }

    PluginManager::~PluginManager() {
        unload();
    }

    fs::path PluginManager::executable_directory() {
        return executable_dir();
    }

    fs::path PluginManager::default_plugins_directory() {
        return executable_dir() / "plugins";
    }

    void PluginManager::add_builtin(PluginMetadata meta, PluginFactory factory) {
        if (meta.name.empty()) {
            throw PluginError("builtin plugin name must not be empty");
        }

        const std::string key = meta.name;
        meta.builtin = true;
        builtins_[key] = {std::move(meta), std::move(factory)};
    }

    void PluginManager::discover(const fs::path &plugins_dir) {
        // Re-scanning replaces the previous results, so folders removed since the
        // last call do not linger.
        discovered_.clear();
        warnings_.clear();

        std::error_code ec;
        // A missing plugins folder is normal on a fresh install, not a warning.
        if (!fs::is_directory(plugins_dir, ec)) {
            return;
        }

        // Tallies how many sources claim each name; anything above one is a conflict
        // that must be reported before load() tries to pick a winner.
        std::map<std::string, int> name_counts;

        fs::directory_iterator it(plugins_dir, ec);
        if (ec) {
            warnings_.push_back("cannot iterate plugin directory " + plugins_dir.string() +
                                ": " + ec.message());
            return;
        }

        for (const auto &entry: it) {
            std::error_code dec;
            if (!entry.is_directory(dec)) {
                continue;
            }

            const auto json_path = entry.path() / "plugin.json";
            if (!fs::is_regular_file(json_path, dec)) {
                continue;
            }

            Discovered d;
            try {
                d.meta = manifest::parse(json_path);
            } catch (const PluginError &e) {
                warnings_.push_back(e.what());
                continue;
            }

            d.dir = entry.path();
            d.library = resolve_library(d);
            if (d.library.empty()) {
                warnings_.push_back("no shared library found for plugin '" + d.meta.name +
                                    "' in " + d.dir.string());
            }

            // A theme is offered when the manifest names one or a theme/ folder exists;
            // the folder has to be real, otherwise drop it and say why.
            if (!d.meta.theme.empty() || fs::is_directory(d.dir / "theme", dec)) {
                d.theme_dir = d.dir / "theme";
                d.theme_name = d.meta.theme.empty() ? d.meta.name : d.meta.theme;
                if (!fs::is_directory(d.theme_dir, dec)) {
                    warnings_.push_back("plugin '" + d.meta.name +
                                        "' declares a theme but has no theme/ directory");
                    d.theme_dir.clear();
                    d.theme_name.clear();
                }
            }

            name_counts[d.meta.name]++;
            discovered_.push_back(std::move(d));
        }

        // Fold built-ins into the same tally so a folder shadowing a built-in counts
        // as a conflict rather than silently winning.
        for (const auto &[bname, _]: builtins_) {
            for (const auto &d: discovered_) {
                if (d.meta.name == bname) {
                    name_counts[bname]++;
                }
            }
        }

        for (auto &d: discovered_) {
            if (name_counts[d.meta.name] > 1) {
                d.name_conflict = true;
            }
        }

        // Warn once per duplicated name, not once per folder that carries it.
        std::set<std::string> warned;

        for (const auto &d: discovered_) {
            if (!d.name_conflict || !warned.insert(d.meta.name).second) {
                continue;
            }

            std::size_t folders = 0;

            for (const auto &other: discovered_) {
                if (other.meta.name == d.meta.name && other.name_conflict) {
                    folders++;
                }
            }

            // Two or more folders fighting over one name means there is no safe
            // choice; a single folder against a built-in at least has a winner.
            if (folders > 1) {
                warnings_.push_back("plugin name '" + d.meta.name + "' appears in " +
                                    std::to_string(folders) +
                                    " plugin folders; none of them will be loaded");
            } else {
                warnings_.push_back("folder plugin '" + d.meta.name +
                                    "' conflicts with the built-in plugin of the same name; "
                                    "the folder plugin will not be loaded");
            }
        }
    }

    fs::path PluginManager::resolve_library(const Discovered &d) const {
        namespace fs2 = std::filesystem;
        // An explicit "library" in the manifest wins, as long as the file is there.
        if (!d.meta.library.empty()) {
            const auto p = d.dir / d.meta.library;
            std::error_code ec;
            if (fs2::is_regular_file(p, ec)) {
                return p;
            }
        }

        // Next, conventional names for every platform's extensions, so a plugin
        // built on one OS is still found by name on another.
        const auto &n = d.meta.name;
        const fs2::path candidates[] = {
            d.dir / (n + ".dll"),
            d.dir / ("lib" + n + ".dll"),
            d.dir / (n + ".so"),
            d.dir / ("lib" + n + ".so"),
            d.dir / (n + ".dylib"),
            d.dir / ("lib" + n + ".dylib"),
        };

        for (const auto &c: candidates) {
            std::error_code ec;
            if (fs2::is_regular_file(c, ec)) {
                return c;
            }
        }

        // Last resort: accept any library in the folder, but only when there is
        // exactly one, so a stray helper binary cannot be loaded by mistake.
        std::error_code ec;
        fs2::directory_iterator it(d.dir, ec);
        if (ec) {
            return {};
        }

        fs2::path found;
        int matches = 0;

        for (const auto &entry: it) {
            std::error_code fec;
            if (!entry.is_regular_file(fec)) {
                continue;
            }
            const auto fname = entry.path().filename().string();
            const auto ext = entry.path().extension().string();
            const bool so_versioned = fname.find(".so.") != std::string::npos;
            if (ext == ".dll" || ext == ".dylib" || ext == ".so" || so_versioned) {
                found = entry.path();
                ++matches;
            }
        }

        if (matches == 1) {
            return found;
        }
        return {};
    }

    void PluginManager::release_loaded(Loaded &ld) {
        // Destroy the object before closing the library: its destructor runs code
        // that only exists while the library is mapped.
        if (ld.raw) {
            if (ld.destroy) {
                ld.destroy(ld.raw);
            } else {
                delete ld.raw;
            }
            ld.raw = nullptr;
        }

        if (ld.handle) {
            detail::library_close(ld.handle);
            ld.handle = nullptr;
        }
    }

    void PluginManager::load_dynamic(const Discovered &d) {
        if (d.library.empty()) {
            throw PluginError("plugin '" + d.meta.name +
                              "' has no loadable shared library in " + d.dir.string());
        }

        std::string err;
        void *handle = detail::library_open(d.library, err);
        if (!handle) {
            throw PluginError(err);
        }

        using ApiVersionFn = int (*)();
        using CreateFn = Plugin *(*)();
        using DestroyFn = void (*)(Plugin *);

        // The three exports are the whole ABI: a version probe to reject mismatched
        // builds, plus factory functions so the plugin is created and destroyed by
        // the same module that owns its memory layout.
        auto *api_version = reinterpret_cast<ApiVersionFn>(
            detail::library_symbol(handle, "opendoc_plugin_api_version"));
        auto *create = reinterpret_cast<CreateFn>(
            detail::library_symbol(handle, "opendoc_create_plugin"));
        auto *destroy = reinterpret_cast<DestroyFn>(
            detail::library_symbol(handle, "opendoc_destroy_plugin"));

        if (!api_version || !create || !destroy) {
            detail::library_close(handle);
            throw PluginError(
                "plugin '" + d.meta.name +
                "' does not export opendoc_plugin_api_version / opendoc_create_plugin / "
                "opendoc_destroy_plugin (" + d.library.string() + ")");
        }

        // Exact match only: the Plugin vtable and every type crossing the boundary
        // change layout when the headers change, and a wrong build of the C++ runtime
        // would also break dynamic_cast and exception unwinding between the two images.
        const int ver = api_version();
        if (ver != OPENDOC_PLUGIN_API_VERSION) {
            detail::library_close(handle);
            throw PluginError("plugin '" + d.meta.name + "' targets API version " +
                              std::to_string(ver) + " but this OpenDoc expects " +
                              std::to_string(OPENDOC_PLUGIN_API_VERSION));
        }

        Plugin *raw = nullptr;

        // Every failure path below closes the handle first; a leaked one would keep
        // the plugin mapped and pinned for the life of the process.
        try {
            raw = create();
        } catch (...) {
            detail::library_close(handle);
            throw;
        }

        if (!raw) {
            detail::library_close(handle);
            throw PluginError("plugin '" + d.meta.name +
                              "' factory (opendoc_create_plugin) returned nullptr");
        }

        // The runtime name must match the manifest name, since that is what the site
        // config lists and what warnings and errors quote back to the user.
        if (raw->name() != d.meta.name) {
            const std::string actual = raw->name();
            destroy(raw);
            detail::library_close(handle);
            throw PluginError("plugin '" + d.meta.name + "' reports name '" + actual +
                              "' from Plugin::name(); it must match plugin.json");
        }

        Loaded ld;
        ld.meta = d.meta;
        ld.raw = raw;
        ld.destroy = destroy;
        ld.handle = handle;
        ld.theme_dir = d.theme_dir;
        ld.theme_name = d.theme_name;
        active_.push_back(std::move(ld));
    }

    void PluginManager::load_builtin(const std::string &name) {
        auto it = builtins_.find(name);
        if (it == builtins_.end()) {
            throw PluginError("unknown built-in plugin '" + name + "'");
        }

        auto instance = it->second.second();
        if (!instance) {
            throw PluginError("built-in plugin factory for '" + name + "' returned nullptr");
        }

        if (instance->name() != name) {
            throw PluginError("built-in plugin '" + name + "' reports name '" +
                              instance->name() + "'");
        }

        // Built-ins have no library and no exported destructor, so release_loaded
        // falls back to plain delete on the host's own heap.
        Loaded ld;
        ld.meta = it->second.first;
        ld.raw = instance.release();
        ld.destroy = nullptr;
        ld.handle = nullptr;
        active_.push_back(std::move(ld));
    }

    void PluginManager::load(const mapper::SiteConfig &config) {
        unload();

        // Rejects a name listed twice: loading it twice would run its hooks twice.
        std::set<std::string> seen;

        for (const auto &name: config.plugins) {
            if (!seen.insert(name).second) {
                throw PluginError("plugin '" + name + "' listed twice in config");
            }

            bool conflicted = false;
            bool has_folder = false;

            for (const auto &d: discovered_) {
                if (d.meta.name == name) {
                    has_folder = true;
                    if (d.name_conflict) {
                        conflicted = true;
                    }
                }
            }

            // Ambiguity is a hard error rather than a silent pick: with several
            // candidates the user cannot tell which one ended up running.
            if (conflicted) {
                throw PluginError(
                    "plugin '" + name +
                    "' is provided by multiple sources with the same name; none were "
                    "loaded. Remove the duplicate folders and rebuild");
            }

            // A plugin folder shadows the built-in of the same name; only when no
            // folder survived the conflict check do we fall back to the built-in.
            if (has_folder) {
                const auto it = std::find_if(discovered_.begin(), discovered_.end(),
                                             [&](const Discovered &d) {
                                                 return d.meta.name == name &&
                                                        !d.name_conflict;
                                             });
                if (it != discovered_.end()) {
                    load_dynamic(*it);
                    continue;
                }
            }

            if (builtins_.contains(name)) {
                load_builtin(name);
                continue;
            }

            // Spell out every legal name so a typo in the config is obvious.
            std::string known;

            for (const auto &n: available_names()) {
                if (!known.empty()) {
                    known += ", ";
                }
                known += n;
            }

            throw PluginError("unknown plugin '" + name + "' (available: " +
                              (known.empty() ? "none" : known) + ")");
        }
    }

    void PluginManager::unload() {
        for (auto it = active_.rbegin(); it != active_.rend(); ++it) {
            release_loaded(*it);
        }

        active_.clear();
    }

    std::vector<std::string> PluginManager::available_names() const {
        std::set<std::string> names;

        for (const auto &[n, _]: builtins_) {
            names.insert(n);
        }
        for (const auto &d: discovered_) {
            if (!d.name_conflict) {
                names.insert(d.meta.name);
            }
        }

        return {names.begin(), names.end()};
    }

    std::optional<fs::path> PluginManager::theme_directory(
        const std::string &theme_name) const {
        for (const auto &a: active_) {
            if (!a.theme_name.empty() && a.theme_name == theme_name) {
                return a.theme_dir;
            }
        }
        return std::nullopt;
    }

    void PluginManager::fire_on_config(mapper::SiteConfig &config) {
        for (const auto &p: active_) {
            if (p.raw) {
                invoke(p, [&](Plugin &plugin) { plugin.on_config(config); });
            }
        }
    }

    void PluginManager::fire_on_pre_build(BuildContext &ctx) {
        for (const auto &p: active_) {
            if (p.raw) {
                invoke(p, [&](Plugin &plugin) { plugin.on_pre_build(ctx); });
            }
        }
    }

    void PluginManager::fire_on_page_markdown(BuildContext &ctx, const mapper::Page &page,
                                              std::string &markdown) {
        for (const auto &p: active_) {
            if (p.raw) {
                invoke(p, [&](Plugin &plugin) {
                    plugin.on_page_markdown(ctx, page, markdown);
                });
            }
        }
    }

    void PluginManager::fire_on_page_content(BuildContext &ctx, const mapper::Page &page,
                                             parser::ParsedPage &parsed) {
        for (const auto &p: active_) {
            if (p.raw) {
                invoke(p, [&](Plugin &plugin) {
                    plugin.on_page_content(ctx, page, parsed);
                });
            }
        }
    }

    void PluginManager::fire_on_page_context(BuildContext &ctx, const mapper::Page &page,
                                             theme::TemplateContext &template_context) {
        for (const auto &p: active_) {
            if (p.raw) {
                invoke(p, [&](Plugin &plugin) {
                    plugin.on_page_context(ctx, page, template_context);
                });
            }
        }
    }

    void PluginManager::fire_on_post_page(BuildContext &ctx, const mapper::Page &page,
                                          std::string &html) {
        for (const auto &p: active_) {
            if (p.raw) {
                invoke(p, [&](Plugin &plugin) { plugin.on_post_page(ctx, page, html); });
            }
        }
    }

    void PluginManager::fire_on_post_build(BuildContext &ctx) {
        for (const auto &p: active_) {
            if (p.raw) {
                invoke(p, [&](Plugin &plugin) { plugin.on_post_build(ctx); });
            }
        }
    }
}
