#include "opendoc/mapper/config.hpp"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <functional>
#include <system_error>

namespace opendoc::mapper {
    namespace {
        // Prefixes a yaml-cpp failure with the offending file so the message is actionable.
        std::string yaml_error_context(const std::filesystem::path &path,
                                       const YAML::Exception &e) {
            return "failed to parse " + path.string() + ": " + e.what();
        }

        // Normalizes a path to a canonical generic string for prefix comparisons.
        // Windows keys are lowercased because drive letters and separators are case-insensitive.
        std::string dir_key(const std::filesystem::path &p) {
            std::error_code ec;
            auto c = std::filesystem::weakly_canonical(p, ec);
            if (ec) {
                c = std::filesystem::absolute(p, ec);
                if (ec) {
                    return {};
                }
            }

            auto s = c.generic_string();
#ifdef _WIN32
            for (auto &ch: s) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
#endif
            return s;
        }

        // True when child lies strictly inside parent, matching only on a '/' boundary.
        bool is_descendant(const std::string &child, const std::string &parent) {
            if (parent.empty() || child.size() <= parent.size()) {
                return false;
            }
            if (child.compare(0, parent.size(), parent) != 0) {
                return false;
            }
            return child[parent.size()] == '/';
        }

        // True when the key is empty or names a filesystem root instead of a subdirectory.
        bool is_filesystem_root(const std::string &key) {
            const std::filesystem::path p(key);
            return p.empty() || p.root_path() == p;
        }
    }

    SiteConfig SiteConfig::load(const std::filesystem::path &yaml_path) {
        if (!std::filesystem::exists(yaml_path)) {
            throw ConfigError("config file not found: " + yaml_path.string() +
                              " (run `opendoc build` in a folder containing opendoc.yaml)");
        }

        YAML::Node root;
        try {
            root = YAML::LoadFile(yaml_path.string());
        } catch (const YAML::Exception &e) {
            throw ConfigError(yaml_error_context(yaml_path, e));
        }

        SiteConfig cfg;
        cfg.config_path = std::filesystem::absolute(yaml_path);

        try {
            if (root["site_name"]) {
                cfg.site_name = root["site_name"].as<std::string>();
            }
            if (root["site_description"]) {
                cfg.site_description = root["site_description"].as<std::string>();
            }
            if (root["site_url"]) {
                cfg.site_url = root["site_url"].as<std::string>();
            }
            if (root["logo"]) {
                cfg.logo = root["logo"].as<std::string>();
            }
            if (root["footer"] && root["footer"].IsScalar()) {
                cfg.footer = root["footer"].as<std::string>();
            }
            if (root["favicon"]) {
                cfg.favicon = root["favicon"].as<std::string>();
            }
            if (root["social_card"]) {
                cfg.social_card = root["social_card"].as<std::string>();
            }
            if (root["edit_url"]) {
                cfg.edit_url = root["edit_url"].as<std::string>();
            }
            if (root["github_url"]) {
                cfg.github_url = root["github_url"].as<std::string>();
            }
            if (root["current_version"]) {
                cfg.current_version = root["current_version"].as<std::string>();
            }
            if (root["docs_dir"]) {
                cfg.docs_dir = root["docs_dir"].as<std::string>();
            }
            if (root["site_dir"]) {
                cfg.site_dir = root["site_dir"].as<std::string>();
            }
            if (root["use_directory_urls"]) {
                cfg.use_directory_urls = root["use_directory_urls"].as<bool>();
            }
            if (root["strict"]) {
                cfg.strict = root["strict"].as<bool>();
            }

            // theme may be a bare name string or a map with name, custom_dir and options.
            if (root["theme"]) {
                if (const auto &theme = root["theme"]; theme.IsScalar()) {
                    cfg.theme.name = theme.as<std::string>();
                } else if (theme.IsMap()) {
                    if (theme["name"]) {
                        cfg.theme.name = theme["name"].as<std::string>();
                    }
                    if (theme["custom_dir"]) {
                        cfg.theme.custom_dir = theme["custom_dir"].as<std::string>();
                    }

                    if (theme["options"] && theme["options"].IsMap()) {
                        for (const auto &kv: theme["options"]) {
                            cfg.theme.options[kv.first.as<std::string>()] =
                                    kv.second.as<std::string>();
                        }
                    }
                }
            }

            // Each link may be a {text, href} map or a bare string with no target.
            if (root["footer_columns"] && root["footer_columns"].IsSequence()) {
                for (const auto &col: root["footer_columns"]) {
                    if (!col.IsMap()) {
                        continue;
                    }

                    FooterColumn column;
                    if (col["title"]) {
                        column.title = col["title"].as<std::string>();
                    }

                    if (col["links"] && col["links"].IsSequence()) {
                        for (const auto &lnk: col["links"]) {
                            FooterLink link;
                            if (lnk.IsMap()) {
                                if (lnk["text"]) {
                                    link.text = lnk["text"].as<std::string>();
                                }
                                if (lnk["href"]) {
                                    link.href = lnk["href"].as<std::string>();
                                }
                            } else if (lnk.IsScalar()) {
                                link.text = lnk.as<std::string>();
                                link.href = "";
                            }

                            if (!link.text.empty()) {
                                column.links.push_back(std::move(link));
                            }
                        }
                    }

                    cfg.footer_columns.push_back(std::move(column));
                }
            }

            // Each version may be a {label, url} map or a bare label with no url.
            if (root["versions"] && root["versions"].IsSequence()) {
                for (const auto &v: root["versions"]) {
                    VersionEntry entry;
                    if (v.IsMap()) {
                        if (v["label"]) {
                            entry.label = v["label"].as<std::string>();
                        }
                        if (v["url"]) {
                            entry.url = v["url"].as<std::string>();
                        }
                    } else if (v.IsScalar()) {
                        entry.label = v.as<std::string>();
                        entry.url = "";
                    }

                    if (!entry.label.empty()) {
                        cfg.versions.push_back(std::move(entry));
                    }
                }
            }

            if (root["plugins"]) {
                for (const auto &p: root["plugins"]) {
                    if (p.IsScalar()) {
                        cfg.plugins.push_back(p.as<std::string>());
                    }
                }
            }

            // Recurses over nav: "Title: path" maps, nested sequences for submenus,
            // or bare path scalars that produce entries with no title.
            if (root["nav"]) {
                std::function < void(const YAML::Node &, std::vector<NavEntry> &) > parse_nav;
                parse_nav = [&](const YAML::Node &node, std::vector<NavEntry> &out) {
                    if (!node) {
                        return;
                    }

                    if (node.IsSequence()) {
                        for (const auto &item: node) {
                            if (item.IsMap()) {
                                for (const auto &kv: item) {
                                    NavEntry entry;
                                    entry.title = kv.first.as<std::string>();
                                    if (kv.second.IsScalar()) {
                                        entry.href = kv.second.as<std::string>();
                                    } else if (kv.second.IsSequence()) {
                                        parse_nav(kv.second, entry.children);
                                    } else if (kv.second.IsMap()) {
                                        entry.href = "";
                                        parse_nav(kv.second, entry.children);
                                    }
                                    out.push_back(std::move(entry));
                                }
                            } else if (item.IsScalar()) {
                                NavEntry entry;
                                entry.href = item.as<std::string>();
                                out.push_back(std::move(entry));
                            }
                        }
                    }
                };

                parse_nav(root["nav"], cfg.nav);
            }
        } catch (const YAML::Exception &e) {
            throw ConfigError(yaml_error_context(yaml_path, e));
        }

        // Required keys must be non-empty or path math and rendering would degenerate.
        if (cfg.site_name.empty()) {
            throw ConfigError(cfg.config_path.string() + ": site_name must not be empty");
        }
        if (cfg.docs_dir.empty()) {
            throw ConfigError(cfg.config_path.string() + ": docs_dir must not be empty");
        }
        if (cfg.site_dir.empty()) {
            throw ConfigError(cfg.config_path.string() + ": site_dir must not be empty");
        }

        // Guard against a build that could wipe its own sources: the output tree
        // must be disjoint from the input tree and must not sit on a root path.
        const auto docs = cfg.docs_root();
        const auto site = cfg.site_root();
        const auto base = cfg.config_path.parent_path();

        // Rebase custom_dir like docs_dir/site_dir so it resolves from the
        // config file location instead of the process working directory.
        if (!cfg.theme.custom_dir.empty() && cfg.theme.custom_dir.is_relative()) {
            cfg.theme.custom_dir = std::filesystem::absolute(base / cfg.theme.custom_dir);
        }

        const std::string dkey = dir_key(docs);
        const std::string skey = dir_key(site);
        const std::string bkey = dir_key(base);

        if (skey.empty() || dkey.empty()) {
            throw ConfigError("cannot resolve docs_dir (" + docs.string() +
                              ") or site_dir (" + site.string() + ")");
        }

        if (skey == dkey) {
            throw ConfigError("docs_dir and site_dir must be different (both " +
                              docs.string() + ")");
        }

        if (is_descendant(dkey, skey) || is_descendant(skey, dkey)) {
            throw ConfigError("site_dir (" + site.string() +
                              ") and docs_dir (" + docs.string() +
                              ") must not overlap");
        }

        if (!bkey.empty() && skey == bkey) {
            throw ConfigError("site_dir (" + site.string() +
                              ") must not be the directory containing " +
                              cfg.config_path.filename().string());
        }

        if (is_filesystem_root(skey)) {
            throw ConfigError("refusing to use a filesystem root as site_dir: " +
                              site.string());
        }

        if (!std::filesystem::exists(docs)) {
            throw ConfigError("docs_dir does not exist: " + docs.string());
        }

        return cfg;
    }

    std::filesystem::path SiteConfig::docs_root() const {
        const std::filesystem::path base =
                config_path.empty() ? std::filesystem::current_path() : config_path.parent_path();
        std::filesystem::path p = docs_dir;
        if (p.is_relative()) {
            p = base / p;
        }
        return std::filesystem::absolute(p);
    }

    std::filesystem::path SiteConfig::site_root() const {
        const std::filesystem::path base =
                config_path.empty() ? std::filesystem::current_path() : config_path.parent_path();
        std::filesystem::path p = site_dir;
        if (p.is_relative()) {
            p = base / p;
        }
        return std::filesystem::absolute(p);
    }
}
