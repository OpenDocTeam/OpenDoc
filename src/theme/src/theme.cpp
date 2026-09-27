#include "opendoc/theme/theme.hpp"

#include "theme_assets.hpp"

#include "opendoc/mapper/link_resolver.hpp"
#include "opendoc/mapper/scanner.hpp"
#include "opendoc/parser/html_renderer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

namespace opendoc::theme {
    namespace {
        // Single HTML escaper lives in the parser module; this just forwards.
        std::string esc(const std::string_view s) {
            return parser::HtmlRenderer::escape_html(s);
        }

        // Reads a whole file; missing or unreadable files come back as an empty string.
        std::string read_file(const std::filesystem::path &p) {
            std::ifstream in(p, std::ios::binary);
            if (!in) {
                return {};
            }

            std::ostringstream ss;
            ss << in.rdbuf();

            return ss.str();
        }

        // True when this node or any descendant is the page being rendered.
        bool contains_current(const mapper::SiteNode &node, const std::string &url) {
            if (node.page && node.page->url == url) {
                return true;
            }

            return std::ranges::any_of(node.children,
                                       [&](const mapper::SiteNode &c) {
                                           return contains_current(c, url);
                                       });
        }
    }

    Theme Theme::load(const mapper::ThemeConfig &cfg,
                      const std::optional<std::filesystem::path> &plugin_theme) {
        Theme theme;
        theme.name_ = cfg.name.empty() ? "default" : cfg.name;

        // Only the embedded default ships in the binary; other names need a plugin.
        if (theme.name_ != "default") {
            if (!plugin_theme) {
                throw ThemeError(
                    "unknown theme '" + theme.name_ +
                    "' (available: default). If a plugin provides this theme, list it "
                    "under plugins: in opendoc.yaml.");
            }
            return load_directory(theme.name_, *plugin_theme);
        }

        std::string layout_src(embedded::default_layout_html);

        theme.assets_.push_back(Asset{
            .out_rel_path = "assets/css/base.css",
            .content = std::string(embedded::default_base_css)
        });
        theme.assets_.push_back(Asset{
            .out_rel_path = "assets/js/theme.js",
            .content = std::string(embedded::default_theme_js)
        });
        theme.assets_.push_back(Asset{
            .out_rel_path = "assets/js/search.js",
            .content = std::string(embedded::default_search_js)
        });

        // User files in custom_dir override the embedded layout and assets one by one.
        if (!cfg.custom_dir.empty()) {
            namespace fs = std::filesystem;
            if (fs::exists(cfg.custom_dir / "layout.html")) {
                if (auto content = read_file(cfg.custom_dir / "layout.html"); !content.empty()) {
                    layout_src = std::move(content);
                }
            }
            if (fs::exists(cfg.custom_dir / "css" / "base.css")) {
                if (const auto content = read_file(cfg.custom_dir / "css" / "base.css");
                    !content.empty()) {
                    for (auto &a: theme.assets_) {
                        if (a.out_rel_path == "assets/css/base.css") {
                            a.content = content;
                        }
                    }
                }
            }
            if (fs::exists(cfg.custom_dir / "js" / "theme.js")) {
                if (const auto content = read_file(cfg.custom_dir / "js" / "theme.js");
                    !content.empty()) {
                    for (auto &a: theme.assets_) {
                        if (a.out_rel_path == "assets/js/theme.js") {
                            a.content = content;
                        }
                    }
                }
            }
            if (fs::exists(cfg.custom_dir / "js" / "search.js")) {
                if (const auto content = read_file(cfg.custom_dir / "js" / "search.js");
                    !content.empty()) {
                    for (auto &a: theme.assets_) {
                        if (a.out_rel_path == "assets/js/search.js") {
                            a.content = content;
                        }
                    }
                }
            }
        }

        theme.layout_ = TemplateEngine(std::move(layout_src));
        return theme;
    }

    Theme Theme::load_directory(const std::string &name, const std::filesystem::path &dir) {
        namespace fs = std::filesystem;
        Theme theme;
        theme.name_ = name;

        const auto layout_path = dir / "layout.html";
        std::error_code ec;
        if (!fs::is_regular_file(layout_path, ec)) {
            throw ThemeError("theme '" + name + "' is missing layout.html in " +
                             dir.string());
        }

        auto layout_src = read_file(layout_path);
        if (layout_src.empty()) {
            throw ThemeError("theme '" + name + "' has empty or unreadable layout.html: " +
                             layout_path.string());
        }

        fs::recursive_directory_iterator it(dir, ec);
        if (ec) {
            throw ThemeError("cannot read theme directory " + dir.string() + ": " +
                             ec.message());
        }

        // Collects every file under dir except layout, hidden, empty, or linked ones.
        for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            std::error_code fec;
            // Symlinks and reparse points are skipped so traversal stays inside dir.
            const bool reparse =
                    it->is_symlink(fec) || mapper::is_reparse_point(it->path());
            fec.clear();
            if (reparse) {
                it.disable_recursion_pending();
                continue;
            }

            if (!it->is_regular_file(fec)) {
                continue;
            }

            const auto rel = fs::relative(it->path(), dir, fec);
            if (fec) {
                continue;
            }

            const auto rel_str = rel.generic_string();
            if (rel_str == "layout.html") {
                continue;
            }

            bool hidden = false;
            for (const auto &part: fs::path(rel_str)) {
                if (const auto s = part.string(); !s.empty() && s.front() == '.') {
                    hidden = true;
                    break;
                }
            }
            if (hidden) {
                continue;
            }

            auto content = read_file(it->path());
            if (content.empty()) {
                continue;
            }

            theme.assets_.push_back(Asset{
                .out_rel_path = rel_str,
                .content = std::move(content)
            });
        }

        theme.layout_ = TemplateEngine(std::move(layout_src));
        return theme;
    }

    std::string Theme::render_page(const TemplateContext &ctx) const {
        return layout_.render(ctx);
    }

    std::string Theme::render_nav(const mapper::SiteTree &site,
                                  const mapper::Page &current) {
        std::string out = "<ul class=\"nav-list\">\n";

        // Emits one nav item; children nest in a list that is hidden unless active.
        std::function < void(const mapper::SiteNode &, int) > walk =
                [&](const mapper::SiteNode &node, const int depth) {
                    const bool has_children = !node.children.empty();
                    const bool is_active = node.page && node.page->url == current.url;
                    const bool active_branch = contains_current(node, current.url);
                    const bool expanded = active_branch;

                    out += "<li class=\"nav-item";
                    if (is_active) {
                        out += " active";
                    }
                    if (has_children) {
                        out += " has-children";
                    }
                    if (has_children && expanded) {
                        out += " expanded";
                    }
                    out += "\">";

                    out += "<div class=\"nav-row\">";
                    if (node.page) {
                        out += "<a href=\"";
                        out += esc(mapper::LinkResolver::url_to_url(current.url, node.page->url));
                        out += '"';
                        if (is_active) {
                            out += " aria-current=\"page\"";
                        }
                        out += '>';
                        out += esc(node.title);
                        out += "</a>";
                    } else {
                        out += "<span class=\"nav-label\">";
                        out += esc(node.title);
                        out += "</span>";
                    }
                    if (has_children) {
                        out += "<button aria-expanded=\"";
                        out += expanded ? "true" : "false";
                        out += R"(" aria-label="Toggle section" class="nav-caret" type="button">)";
                        out += "<svg aria-hidden=\"true\" fill=\"none\" height=\"12\" stroke=\"currentColor\" "
                                "stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"2\" "
                                "viewBox=\"0 0 24 24\" width=\"12\"><path d=\"m9 18 6-6-6-6\"/></svg>";
                        out += "</button>";
                    }
                    out += "</div>";

                    if (has_children) {
                        out += "\n<ul class=\"nav-children\"";
                        if (!expanded) {
                            out += " hidden";
                        }
                        out += ">\n";

                        for (const auto &child: node.children) {
                            walk(child, depth + 1);
                        }

                        out += "</ul>\n";
                    }
                    out += "</li>\n";
                };

        for (const auto &child: site.nav_root().children) {
            walk(child, 0);
        }

        out += "</ul>";
        return out;
    }

    namespace {
        // Returns the first page in this subtree, giving a section a link target.
        const mapper::Page *first_page(const mapper::SiteNode &node) {
            if (node.page) {
                return node.page;
            }

            for (const auto &child: node.children) {
                if (const mapper::Page *p = first_page(child)) {
                    return p;
                }
            }

            return nullptr;
        }
    }

    std::string Theme::render_top_nav(const mapper::SiteTree &site,
                                      const mapper::Page &current) {
        std::string items;

        for (const auto &child: site.nav_root().children) {
            const mapper::Page *target = first_page(child);
            if (!target) {
                continue;
            }

            const bool active = contains_current(child, current.url);
            items += "<li class=\"header-nav-item\">";
            items += "<a href=\"";
            items += esc(mapper::LinkResolver::url_to_url(current.url, target->url));
            items += '"';
            if (active) {
                items += R"( class="active" aria-current="page")";
            }
            items += '>';
            items += esc(child.title);
            items += "</a></li>";
        }

        if (items.empty()) {
            return {};
        }

        return "<ul class=\"header-nav-list\">" + items + "</ul>";
    }
}
