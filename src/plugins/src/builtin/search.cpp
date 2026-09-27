#include "builtin.hpp"

#include "encoding.hpp"

#include "opendoc/mapper/site_tree.hpp"
#include "opendoc/parser/html_renderer.hpp"
#include "opendoc/parser/markdown_parser.hpp"
#include "opendoc/parser/plain_text.hpp"
#include "opendoc/plugins/build_context.hpp"

#include <set>
#include <utility>

namespace opendoc::plugins::builtin {
    namespace {
        // One searchable slice of a page: everything from a heading up to the next one.
        struct SearchSection {
            // Heading identity and label, used to deep-link into the page.
            std::string anchor;
            std::string heading;
            int level = 0;
            // Flattened plain text of the section body, truncated before indexing.
            std::string text;
        };

        // Turns any run of whitespace into a single space, so indexed text is one
        // line and matches do not depend on source formatting.
        std::string collapse_ws(const std::string &in) {
            std::string out;
            out.reserve(in.size());
            bool prev_space = true;

            for (char c: in) {
                if (c == '\n' || c == '\r' || c == '\t') {
                    c = ' ';
                }

                if (c == ' ') {
                    if (!prev_space) {
                        out.push_back(' ');
                    }
                    prev_space = true;
                } else {
                    out.push_back(c);
                    prev_space = false;
                }
            }

            while (!out.empty() && out.back() == ' ') {
                out.pop_back();
            }

            return out;
        }

        // Walks the document tree and cuts it at every heading. Containers are
        // recursed into so headings nested in lists or quotes still start a section,
        // while plain content is appended to whichever section is open. `current`
        // is passed by reference and survives between calls, which is what lets the
        // pieces of one heading's content arrive in several top-level visits.
        void split_sections(const parser::NodeList &nodes, std::vector<SearchSection> &out,
                            SearchSection &current) {
            for (const auto &n: nodes) {
                // dynamic_cast works across the plugin boundary only because the host
                // and the plugin share one C++ runtime and its RTTI.
                if (const auto *h = dynamic_cast<const parser::HeadingNode *>(n.get())) {
                    if (!current.heading.empty() || !current.text.empty()) {
                        out.push_back(std::move(current));
                        current = SearchSection{};
                    }

                    current.anchor = h->anchor_id;
                    current.heading = parser::PlainTextExtractor::from_inlines(h->inlines);
                    current.level = h->level;
                } else if (const auto *doc =
                        dynamic_cast<const parser::DocumentNode *>(n.get())) {
                    split_sections(doc->children, out, current);
                } else if (const auto *q =
                        dynamic_cast<const parser::BlockQuoteNode *>(n.get())) {
                    split_sections(q->children, out, current);
                } else if (const auto *list = dynamic_cast<const parser::ListNode *>(n.get())) {
                    split_sections(list->items, out, current);
                } else if (const auto *li =
                        dynamic_cast<const parser::ListItemNode *>(n.get())) {
                    split_sections(li->children, out, current);
                } else {
                    parser::PlainTextExtractor::append_node(*n, current.text);
                }
            }
        }

        // Location shown with each hit: every breadcrumb but the last (which is the
        // page itself), or failing that the parent directory of the URL.
        std::string parent_section(const mapper::Page &page) {
            if (page.breadcrumbs.size() > 1) {
                std::string out;

                for (std::size_t i = 0; i + 1 < page.breadcrumbs.size(); ++i) {
                    if (i > 0) {
                        out += " / ";
                    }
                    out += page.breadcrumbs[i];
                }

                return out;
            }

            std::string url = page.url;
            if (!url.empty() && url.back() == '/') {
                url.pop_back();
            }

            const auto slash = url.find_last_of('/');
            if (slash == std::string::npos || slash == 0) {
                return {};
            }

            return url.substr(0, slash);
        }

        // Harvests search terms from the "keywords" and "tags" front matter fields.
        // Separators are blanked first so YAML lists and comma strings tokenize the
        // same way; duplicates are dropped and the list is capped so one page with
        // hundreds of tags cannot bloat every entry.
        std::vector<std::string> front_matter_terms(const mapper::Page &page) {
            std::vector<std::string> terms;
            std::set<std::string> seen;

            for (const char *key: {"keywords", "tags"}) {
                const auto it = page.front_matter.find(key);
                if (it == page.front_matter.end()) {
                    continue;
                }

                std::string normalized;
                normalized.reserve(it->second.size());

                for (const char c: it->second) {
                    normalized.push_back(
                        (c == ',' || c == ';' || c == '[' || c == ']' || c == '"') ? ' ' : c);
                }

                std::string word;
                for (const char c: normalized + " ") {
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                        if (!word.empty() && seen.insert(word).second && terms.size() < 24) {
                            terms.push_back(word);
                        }
                        word.clear();
                    } else {
                        word.push_back(c);
                    }
                }
            }

            return terms;
        }

        // Page title for the index: the declared title, else the first heading, else
        // the last URL segment, so a hit is never shown with an empty label.
        std::string fallback_title(const mapper::Page &page,
                                   const std::vector<SearchSection> &sections) {
            if (!page.title.empty()) {
                return page.title;
            }

            for (const auto &s: sections) {
                if (!s.heading.empty()) {
                    return s.heading;
                }
            }

            std::string url = page.url;
            if (!url.empty() && url.back() == '/') {
                url.pop_back();
            }

            const auto slash = url.find_last_of('/');
            url = slash == std::string::npos ? url : url.substr(slash + 1);

            return url.empty() ? "index" : url;
        }

        // Collects one entry per page while pages are parsed, then serializes them
        // into search_index.json at the end of the build.
        class SearchIndexPlugin final : public Plugin {
        public:
            // Identifier used in the site config's plugin list.
            [[nodiscard]] std::string name() const override {
                return "search";
            }

            // Rendering once here makes the HtmlRenderer assign heading anchor ids
            // as a side effect, so every section gets a linkable anchor; the actual
            // page HTML is rendered later by the builder.
            void on_page_content(BuildContext &ctx, const mapper::Page &page,
                                 parser::ParsedPage &parsed) override {
                (void) ctx;

                parser::HtmlRenderer anchorer;
                (void) anchorer.render(parsed.document);

                std::vector<SearchSection> sections;
                SearchSection current;
                split_sections(parsed.document.children, sections, current);

                if (!current.heading.empty() || !current.text.empty()) {
                    sections.push_back(std::move(current));
                }

                Entry e;
                // Root-relative URL, matching how the site's own links are written.
                e.url = "/" + page.url;
                e.description = page.description;
                e.section = parent_section(page);
                e.keywords = front_matter_terms(page);

                for (auto &s: sections) {
                    s.heading = collapse_ws(s.heading);
                    s.text = encoding::utf8_truncate(collapse_ws(s.text), kMaxSectionChars);

                    if (s.heading.empty() && s.text.empty()) {
                        continue;
                    }

                    e.sections.push_back(std::move(s));
                }

                // A page with no headings still needs a record, otherwise it would
                // never appear in results at all.
                if (e.sections.empty()) {
                    e.sections.push_back(SearchSection{});
                }

                e.title = fallback_title(page, e.sections);
                entries_.push_back(std::move(e));
            }

            // Emits search_index.json: a hand-rolled array of entry objects with
            // every string passed through json_escape, since page text can contain
            // quotes, backslashes, and control characters.
            void on_post_build(BuildContext &ctx) override {
                std::string json = "[\n";

                for (std::size_t i = 0; i < entries_.size(); ++i) {
                    const auto &e = entries_[i];
                    json += "  {\n";
                    json += R"(    "url": ")" + encoding::json_escape(e.url) + "\",\n";
                    json += R"(    "title": ")" + encoding::json_escape(e.title) + "\",\n";
                    json += R"(    "section": ")" + encoding::json_escape(e.section) + "\",\n";
                    json += R"(    "description": ")" + encoding::json_escape(e.description) +
                            "\",\n";
                    json += "    \"keywords\": [";

                    for (std::size_t k = 0; k < e.keywords.size(); ++k) {
                        if (k > 0) {
                            json += ", ";
                        }
                        json += "\"" + encoding::json_escape(e.keywords[k]) + "\"";
                    }

                    json += "],\n";
                    json += "    \"sections\": [";

                    for (std::size_t s = 0; s < e.sections.size(); ++s) {
                        const auto &sec = e.sections[s];
                        json += s == 0 ? "\n" : ",\n";
                        json += R"(      {"anchor": ")" + encoding::json_escape(sec.anchor) + "\"";
                        json += R"(, "heading": ")" + encoding::json_escape(sec.heading) + "\"";
                        json += ", \"level\": " + std::to_string(sec.level);
                        json += R"(, "text": ")" + encoding::json_escape(sec.text) + "\"}";
                    }

                    if (!e.sections.empty()) {
                        json += "\n    ";
                    }

                    json += "]}\n";
                    if (i + 1 < entries_.size()) {
                        json += ',';
                    }
                }

                json += "]\n";
                ctx.write_artifact("search_index.json", json);
            }

        private:
            // Keeps one runaway page from dominating the index file.
            static constexpr std::size_t kMaxSectionChars = 4000;

            // One index record, i.e. one row of search_index.json.
            struct Entry {
                // Page identity as shown in results.
                std::string url;
                std::string title;
                std::string section;
                std::string description;
                // Front matter terms folded into this page's match set.
                std::vector<std::string> keywords;
                // Heading-delimited chunks the client highlights against.
                std::vector<SearchSection> sections;
            };

            // One entry per page, in the order the builder visited them.
            std::vector<Entry> entries_;
        };
    }

    PluginFactory search_factory() {
        return [] {
            return std::make_unique<SearchIndexPlugin>();
        };
    }
}
