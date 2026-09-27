#include "link_check.hpp"

#include "opendoc/mapper/link_resolver.hpp"

#include <regex>
#include <string_view>

namespace opendoc::app {
    namespace {
        // Normalizes an href to a site-relative path key: strips ?query and #fragment,
        // resolves "." and ".." segments against the page's directory, and drops a
        // leading slash so the result can be looked up in collect_valid_link_targets().
        std::string resolve_href_path(const std::string &base_dir, const std::string_view href) {
            std::string path(href);
            if (const auto hash = path.find('#'); hash != std::string::npos) {
                path = path.substr(0, hash);
            }
            if (const auto q = path.find('?'); q != std::string::npos) {
                path = path.substr(0, q);
            }

            std::vector<std::string> segs;
            auto split = [&](const std::string &s) {
                std::string cur;
                for (const char c: s) {
                    if (c == '/') {
                        if (!cur.empty() && cur != ".") {
                            segs.push_back(cur);
                        }
                        cur.clear();
                    } else {
                        cur.push_back(c);
                    }
                }
                if (!cur.empty() && cur != ".") {
                    segs.push_back(cur);
                }
            };

            // A leading slash means "site root", so the page directory is discarded.
            if (!path.empty() && path.front() == '/') {
                segs.clear();
                split(path);
            } else {
                split(base_dir);
                split(path);
            }

            std::vector<std::string> out;
            for (const auto &s: segs) {
                if (s == "..") {
                    // Popping stops at the top, so ".." cannot escape the site root.
                    if (!out.empty()) {
                        out.pop_back();
                    }
                } else {
                    out.push_back(s);
                }
            }

            std::string joined;
            for (std::size_t i = 0; i < out.size(); ++i) {
                if (i) {
                    joined.push_back('/');
                }
                joined += out[i];
            }

            return joined;
        }
    }

    std::unordered_set<std::string> collect_valid_link_targets(
        const mapper::SiteTree &site, const std::vector<mapper::ScannedStatic> &static_files,
        const std::vector<theme::Theme::Asset> &theme_assets) {
        std::unordered_set<std::string> valid;

        for (const auto &page: site.pages()) {
            valid.insert(page.url);
            if (!page.url.empty() && page.url.back() == '/') {
                valid.insert(page.url + "index.html");
            }
        }

        for (const auto &[rel_path, abs_path]: static_files) {
            valid.insert(rel_path.generic_string());
        }

        for (const auto &a: theme_assets) {
            valid.insert(a.out_rel_path);
        }

        valid.insert("search_index.json");
        valid.insert("sitemap.xml");
        valid.insert("rss.xml");
        return valid;
    }

    std::vector<std::string> check_dead_links(
        const std::string &html, const mapper::Page &page,
        const std::unordered_set<std::string> &valid_targets) {
        std::vector<std::string> issues;
        const std::regex attr_re(R"re((?:href|src)\s*=\s*"([^"]*)")re");
        const std::string base_dir = mapper::LinkResolver::url_dir(page);
        const auto begin = std::sregex_iterator(html.begin(), html.end(), attr_re);
        const auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            const std::string attr = (*it)[1].str();
            if (attr.empty() || attr.front() == '#' ||
                mapper::LinkResolver::is_external(attr)) {
                continue;
            }

            const auto resolved = resolve_href_path(base_dir, attr);

            // Compare both with and without a trailing slash so "/docs/" and "/docs"
            // are treated as the same target.
            std::string key = resolved;
            if (!key.empty() && key.back() == '/') {
                key.pop_back();
            }

            const bool ok = valid_targets.contains(key) ||
                            valid_targets.contains(key + "/") ||
                            valid_targets.contains(key + "/index.html") ||
                            valid_targets.contains(resolved);

            if (!ok) {
                issues.push_back("dead link '" + attr + "' in " +
                                 page.rel_source.generic_string() + " (resolved: '" +
                                 key + "')");
            }
        }

        return issues;
    }
}
