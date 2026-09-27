#include "page_renderer.hpp"

#include "builder.hpp"
#include "link_check.hpp"

#include "opendoc/parser/html_renderer.hpp"
#include "opendoc/parser/markdown_parser.hpp"
#include "opendoc/parser/plain_text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <ranges>
#include <utility>

namespace opendoc::app {
    std::vector<const mapper::Page *> nav_page_order(const mapper::SiteTree &site) {
        std::vector<const mapper::Page *> order;
        std::function < void(const mapper::SiteNode &) > walk =
                [&](const mapper::SiteNode &node) {
                    if (node.page) {
                        order.push_back(node.page);
                    }

                    for (const auto &child: node.children) {
                        walk(child);
                    }
                };
        walk(site.nav_root());
        return order;
    }

    namespace {
        using parser::HtmlRenderer;

        // Reads a boolean theme option, accepting common truthy/falsy spellings and
        // falling back to default_value when the key is missing or unrecognized.
        bool theme_option_bool(const mapper::SiteConfig &cfg, const std::string &key,
                               const bool default_value) {
            const auto it = cfg.theme.options.find(key);
            if (it == cfg.theme.options.end()) {
                return default_value;
            }

            std::string v = it->second;
            std::ranges::transform(v, v.begin(),
                                   [](const unsigned char c) { return std::tolower(c); });

            if (v == "true" || v == "1" || v == "yes" || v == "on") {
                return true;
            }

            if (v == "false" || v == "0" || v == "no" || v == "off") {
                return false;
            }

            return default_value;
        }

        // Builds the sidebar TOC from h2/h3 headings; returns empty when there are
        // fewer than two entries, since a one-item TOC adds no navigation value.
        std::string build_toc(const parser::DocumentNode &doc) {
            // One TOC row: heading depth, anchor id, and its plain-text label.
            struct Entry {
                int level;
                std::string id;
                std::string text;
            };
            std::vector<Entry> headings;
            std::function < void(const parser::NodeList &) > walk =
                    [&](const parser::NodeList &nodes) {
                        for (const auto &n: nodes) {
                            if (const auto *h = dynamic_cast<const parser::HeadingNode *>(n.get())) {
                                if ((h->level == 2 || h->level == 3) && !h->anchor_id.empty()) {
                                    headings.push_back({
                                        .level = h->level, .id = h->anchor_id,
                                        .text = parser::PlainTextExtractor::from_inlines(h->inlines)
                                    });
                                }
                            } else if (const auto *bq =
                                    dynamic_cast<const parser::BlockQuoteNode *>(n.get())) {
                                walk(bq->children);
                            } else if (const auto *li =
                                    dynamic_cast<const parser::ListItemNode *>(n.get())) {
                                walk(li->children);
                            }
                        }
                    };
            walk(doc.children);

            if (headings.size() < 2) {
                return {};
            }

            std::string out = "<ul class=\"toc-list\">\n";
            for (const auto &[level, id, text]: headings) {
                out += "<li class=\"toc-item toc-h";
                out += std::to_string(level);
                out += "\"><a href=\"#";
                out += HtmlRenderer::escape_html(id);
                out += "\">";
                out += HtmlRenderer::escape_html(text);
                out += "</a></li>\n";
            }
            out += "</ul>";
            return out;
        }

        // Assumed reading speed for the "min read" badge.
        constexpr int kReadingWpm = 200;

        // Counts whitespace-separated words and rounds the estimate up, never below 1 min.
        std::string reading_time_for(const std::string &plain) {
            std::size_t words = 0;
            bool in_word = false;
            for (const unsigned char c: plain) {
                if (std::isspace(c)) {
                    in_word = false;
                } else if (!in_word) {
                    in_word = true;
                    ++words;
                }
            }

            const auto minutes = std::max<std::size_t>(1, (words + kReadingWpm - 1) / kReadingWpm);
            return std::to_string(minutes) + " min read";
        }

        // Quotes s as a JavaScript string literal; also escapes <, >, & and control
        // chars so the value is safe to embed directly inside an inline <script>.
        std::string js_string_literal(const std::string_view s) {
            std::string out;
            out.reserve(s.size() + 2);
            out.push_back('"');

            for (const unsigned char c: s) {
                switch (c) {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    case '<':
                        out += "\\u003c";
                        break;
                    case '>':
                        out += "\\u003e";
                        break;
                    case '&':
                        out += "\\u0026";
                        break;
                    default:
                        if (c < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                            out += buf;
                        } else {
                            out.push_back(static_cast<char>(c));
                        }
                }
            }

            out.push_back('"');
            return out;
        }

        // Front matter keys that would shadow template variables the renderer sets itself.
        bool is_reserved_template_key(const std::string_view key) {
            static const std::unordered_set<std::string> reserved = {
                "site_name", "site_description", "site_url",
                "title", "description", "content",
                "has_math", "nav", "top_nav",
                "github_url", "base", "base_js",
                "url", "generator", "logo",
                "favicon", "social_card", "footer_text",
                "footer_columns", "versions", "breadcrumbs",
                "toc", "reading_time", "edit_url",
                "prev_title", "prev_url", "next_title",
                "next_url", "palette_primary",
            };

            return reserved.contains(std::string(key));
        }

        // Prefixes a relative path with the base; external and root-absolute paths pass through.
        std::string with_base(const std::string &base, const std::string_view path) {
            if (path.empty()) {
                return {};
            }

            const std::string p(path);
            if (mapper::LinkResolver::is_external(p) || p.front() == '/') {
                return p;
            }

            return base + p;
        }

        // Renders the configured footer link columns as escaped HTML, or empty when none.
        std::string render_footer_columns(const mapper::SiteConfig &config,
                                          const std::string &base) {
            if (config.footer_columns.empty()) {
                return {};
            }

            std::string out = "<div class=\"footer-cols\">";
            for (const auto &[title, links]: config.footer_columns) {
                out += "<div class=\"footer-col\">";
                if (!title.empty()) {
                    out += "<h4>" + HtmlRenderer::escape_html(title) + "</h4>";
                }
                out += "<ul>";
                for (const auto &[text, href]: links) {
                    out += "<li><a href=\"";
                    out += HtmlRenderer::escape_html(with_base(base, href));
                    out += "\">";
                    out += HtmlRenderer::escape_html(text);
                    out += "</a></li>";
                }
                out += "</ul></div>";
            }
            out += "</div>";
            return out;
        }
    }

    PageRenderResult render_page(const PageRenderContext &rc, mapper::Page &page,
                                 std::string markdown) {
        const auto &config = rc.config;

        try {
            rc.plugins.fire_on_page_markdown(rc.plugin_ctx, page, markdown);
        } catch (const plugins::PluginError &e) {
            throw BuildError(std::string("plugin on_page_markdown: ") + e.what() +
                             " [" + page.rel_source.string() + "]");
        }

        parser::ParsedPage parsed;
        try {
            parser::MarkdownParser md;
            parsed = md.parse(markdown);
        } catch (const parser::ParseError &e) {
            throw BuildError("parse error in " + page.rel_source.string() + ": " +
                             e.what());
        }

        page.front_matter = parsed.front_matter.fields;
        if (const auto it = page.front_matter.find("title"); it != page.front_matter.end()) {
            page.title = it->second;
        }
        if (const auto it = page.front_matter.find("description");
            it != page.front_matter.end()) {
            page.description = it->second;
        }

        try {
            rc.plugins.fire_on_page_content(rc.plugin_ctx, page, parsed);
        } catch (const plugins::PluginError &e) {
            throw BuildError(std::string("plugin on_page_content: ") + e.what() +
                             " [" + page.rel_source.string() + "]");
        }

        {
            // Walks the whole tree rewriting every link/image target to a site-relative
            // URL, recursing into inline containers so nested links are handled too.
            std::function < void(parser::NodeList &) > rewrite =
                    [&](const parser::NodeList &nodes) {
                        for (auto &n: nodes) {
                            if (auto *link = dynamic_cast<parser::LinkNode *>(n.get())) {
                                link->href = rc.resolver.resolve(page, link->href);
                                rewrite(link->inlines);
                            } else if (auto *img = dynamic_cast<parser::ImageNode *>(n.get())) {
                                img->src = rc.resolver.resolve(page, img->src);
                            } else if (auto *p = dynamic_cast<parser::ParagraphNode *>(n.get())) {
                                rewrite(p->inlines);
                            } else if (auto *h = dynamic_cast<parser::HeadingNode *>(n.get())) {
                                rewrite(h->inlines);
                            } else if (auto *e = dynamic_cast<parser::EmphasisNode *>(n.get())) {
                                rewrite(e->inlines);
                            } else if (auto *s = dynamic_cast<parser::StrongNode *>(n.get())) {
                                rewrite(s->inlines);
                            } else if (auto *sk = dynamic_cast<parser::StrikethroughNode *>(n.get())) {
                                rewrite(sk->inlines);
                            } else if (auto *q = dynamic_cast<parser::BlockQuoteNode *>(n.get())) {
                                rewrite(q->children);
                            } else if (auto *li = dynamic_cast<parser::ListNode *>(n.get())) {
                                rewrite(li->items);
                            } else if (auto *item = dynamic_cast<parser::ListItemNode *>(n.get())) {
                                rewrite(item->children);
                            } else if (auto *t = dynamic_cast<parser::TableNode *>(n.get())) {
                                rewrite(t->rows);
                            } else if (auto *row = dynamic_cast<parser::TableRowNode *>(n.get())) {
                                rewrite(row->cells);
                            } else if (auto *cell = dynamic_cast<parser::TableCellNode *>(n.get())) {
                                rewrite(cell->inlines);
                            }
                        }
                    };
            rewrite(parsed.document.children);
        }

        parser::HtmlRenderer renderer;
        page.html_fragment = renderer.render(parsed.document);
        page.plain_text = parser::PlainTextExtractor::from_document(parsed.document);

        const bool show_toc = theme_option_bool(config, "show_toc", true);
        const bool show_reading_time = theme_option_bool(config, "show_reading_time", true);
        const bool show_edit_link = theme_option_bool(config, "show_edit_link", true);
        const bool show_prev_next = theme_option_bool(config, "show_prev_next", true);

        theme::TemplateContext tctx;
        const std::string base = mapper::LinkResolver::base_prefix(page);
        tctx.set("site_name", config.site_name);
        tctx.set("site_description", config.site_description);
        {
            std::string site_url = config.site_url;
            if (!site_url.empty() && site_url.back() != '/') {
                site_url.push_back('/');
            }
            tctx.set("site_url", site_url);
        }
        tctx.set("title", page.title);
        if (!page.description.empty()) {
            tctx.set("description", page.description);
        }
        tctx.set("content", page.html_fragment);
        tctx.set_bool("has_math",
                      page.html_fragment.find("class=\"math") != std::string::npos);
        tctx.set("nav", theme::Theme::render_nav(rc.site, page));
        tctx.set("top_nav", theme::Theme::render_top_nav(rc.site, page));
        if (!config.github_url.empty()) {
            tctx.set("github_url", config.github_url);
        }
        tctx.set("base", base);
        tctx.set("base_js", js_string_literal(base));
        tctx.set("url", page.url);
        tctx.set("generator", std::string("OpenDoc ") + OPENDOC_VERSION);

        if (!config.logo.empty()) {
            std::string logo_url = config.logo;
            if (!logo_url.starts_with("http://") && !logo_url.starts_with("https://") &&
                !logo_url.starts_with("//") && !logo_url.starts_with('/')) {
                logo_url = base + logo_url;
            }
            tctx.set("logo", logo_url);
        }

        if (!config.favicon.empty()) {
            std::string fav = config.favicon;
            if (!fav.starts_with("http://") && !fav.starts_with("https://") &&
                !fav.starts_with("//") && !fav.starts_with('/')) {
                fav = base + fav;
            }
            tctx.set("favicon", fav);
        }

        // The social card must be absolute for Open Graph scrapers; resolve a relative
        // path against site_url first, then against the page's base prefix.
        if (!config.social_card.empty()) {
            std::string card = config.social_card;
            if (!card.starts_with("http://") && !card.starts_with("https://")) {
                if (!config.site_url.empty()) {
                    std::string abs = config.site_url;
                    if (abs.back() != '/') {
                        abs.push_back('/');
                    }
                    abs += card.starts_with('/') ? card.substr(1) : card;
                    card = std::move(abs);
                } else if (!card.starts_with('/')) {
                    card = base + card;
                }
            }
            tctx.set("social_card", card);
        }

        if (!config.footer.empty()) {
            tctx.set("footer_text", config.footer);
        }
        if (auto cols = render_footer_columns(config, base); !cols.empty()) {
            tctx.set("footer_columns", cols);
        }
        if (!rc.versions_html.empty()) {
            tctx.set("versions", rc.versions_html);
        }

        if (!page.breadcrumbs.empty()) {
            std::string crumbs;
            for (std::size_t bi = 0; bi < page.breadcrumbs.size(); ++bi) {
                if (bi) {
                    crumbs += " / ";
                }
                crumbs += page.breadcrumbs[bi];
            }
            tctx.set("breadcrumbs", crumbs);
        }

        if (show_toc) {
            if (auto toc = build_toc(parsed.document); !toc.empty()) {
                tctx.set("toc", toc);
            }
        }

        if (show_reading_time) {
            tctx.set("reading_time", reading_time_for(page.plain_text));
        }

        if (show_edit_link && !config.edit_url.empty()) {
            std::string edit = config.edit_url;
            if (!edit.empty() && edit.back() != '/') {
                edit.push_back('/');
            }
            edit += page.rel_source.generic_string();
            tctx.set("edit_url", edit);
        }

        if (show_prev_next && !rc.page_order.empty()) {
            const auto *current = &page;
            if (auto it = std::ranges::find(rc.page_order, current);
                it != rc.page_order.end()) {
                const auto idx = static_cast<std::size_t>(it - rc.page_order.begin());
                if (idx > 0) {
                    const auto *prev = rc.page_order[idx - 1];
                    tctx.set("prev_title", prev->title);
                    tctx.set("prev_url",
                             mapper::LinkResolver::url_to_url(page.url, prev->url));
                }
                if (idx + 1 < rc.page_order.size()) {
                    const auto *next = rc.page_order[idx + 1];
                    tctx.set("next_title", next->title);
                    tctx.set("next_url",
                             mapper::LinkResolver::url_to_url(page.url, next->url));
                }
            }
        }

        if (const auto pit = config.theme.options.find("palette_primary");
            pit != config.theme.options.end() && !pit->second.empty()) {
            tctx.set("palette_primary", pit->second);
        }

        // Expose front matter to the template, but never let it overwrite renderer-owned keys.
        for (const auto &[k, v]: page.front_matter) {
            if (is_reserved_template_key(k)) {
                continue;
            }

            tctx.set(k, v);
        }

        try {
            rc.plugins.fire_on_page_context(rc.plugin_ctx, page, tctx);
        } catch (const plugins::PluginError &e) {
            throw BuildError(std::string("plugin on_page_context: ") + e.what() +
                             " [" + page.rel_source.string() + "]");
        }

        PageRenderResult out;
        try {
            out.html = rc.theme.render_page(tctx);
        } catch (const theme::TemplateError &e) {
            throw BuildError("template error for " + page.rel_source.string() + ": " +
                             e.what());
        }

        try {
            rc.plugins.fire_on_post_page(rc.plugin_ctx, page, out.html);
        } catch (const plugins::PluginError &e) {
            throw BuildError(std::string("plugin on_post_page: ") + e.what());
        }

        out.dead_links = check_dead_links(out.html, page, rc.valid_targets);
        return out;
    }
}
