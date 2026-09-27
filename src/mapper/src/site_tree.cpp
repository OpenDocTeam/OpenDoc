#include "opendoc/mapper/site_tree.hpp"

#include <functional>
#include <set>

namespace opendoc::mapper {
    namespace {
        // Drops a trailing .md/.markdown (case-insensitive) to get the page's URL stem.
        std::string strip_markdown_ext(const std::string_view rel) {
            std::string s(rel);
            // True when v ends with suffix, compared without regard to case.
            auto ends_with_ci = [](const std::string_view v, const std::string_view suffix) {
                if (v.size() < suffix.size()) {
                    return false;
                }

                const auto tail = v.substr(v.size() - suffix.size());
                for (std::size_t i = 0; i < suffix.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(tail[i])) !=
                        std::tolower(static_cast<unsigned char>(suffix[i]))) {
                        return false;
                    }
                }

                return true;
            };

            if (ends_with_ci(s, ".markdown")) {
                s.resize(s.size() - 9);
            } else if (ends_with_ci(s, ".md")) {
                s.resize(s.size() - 3);
            }

            return s;
        }

        // Maps a source stem to its output URL, honoring the use_directory_urls flag.
        std::string rel_to_url(const std::string_view rel_no_ext, const bool directory_urls) {
            std::string s(rel_no_ext);
            const bool is_index = (s == "index") ||
                                  (s.size() > 6 && s.compare(s.size() - 6, 6, "/index") == 0);
            if (!directory_urls) {
                return s + ".html";
            }
            // An index page owns its directory URL instead of getting "index/".
            if (is_index) {
                if (s == "index") {
                    return "";
                }

                s.resize(s.size() - 6);

                return s + "/";
            }
            return s + "/";
        }

        // Derives a display title from a source path: "index" pages borrow their folder
        // name (falling back to "Home"), and dashes/underscores become spaces.
        std::string title_from_filename(const std::string_view rel_no_ext) {
            std::string base(rel_no_ext);
            if (const auto slash = base.find_last_of('/'); slash != std::string::npos) {
                base = base.substr(slash + 1);
            }

            if (base == "index") {
                if (const auto slash = std::string(rel_no_ext).find_last_of('/');
                    slash != std::string::npos) {
                    base = std::string(rel_no_ext).substr(0, slash);
                } else {
                    base = "Home";
                }

                if (const auto s2 = base.find_last_of('/');
                    s2 != std::string::npos) {
                    base = base.substr(s2 + 1);
                }

                if (base.empty()) {
                    base = "Home";
                }
            }

            for (auto &c: base) {
                if (c == '-' || c == '_') {
                    c = ' ';
                }
            }

            if (!base.empty()) {
                base[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(base[0])));
            }

            return base;
        }

        // Turns a site URL into the file it writes to; directory URLs get index.html.
        std::string output_rel_from_url(const std::string_view url) {
            if (url.empty()) {
                return "index.html";
            }

            std::string s(url);
            if (s.back() == '/') {
                return s + "index.html";
            }

            return s;
        }

        // Convenience wrapper: source path straight to output path, bypassing a Page.
        [[maybe_unused]] std::filesystem::path output_from_rel_source(
            const std::filesystem::path &rel_source, const bool directory_urls) {
            const auto no_ext = strip_markdown_ext(rel_source.generic_string());
            const auto url = rel_to_url(no_ext, directory_urls);
            return output_rel_from_url(url);
        }
    }

    SiteTree SiteTree::build(const SiteConfig &config, const ScanResult &scan) {
        SiteTree tree;
        tree.nav_root_.title = config.site_name;

        const bool dir_urls = config.use_directory_urls;

        // Each scanned markdown file becomes exactly one Page, with its URL, output
        // path, and a filename-derived fallback title.
        for (const auto &[rel_path, abs_path]: scan.markdown) {
            Page page;
            page.abs_source = abs_path;
            page.rel_source = rel_path.generic_string();
            const auto no_ext = strip_markdown_ext(page.rel_source.generic_string());
            page.url = rel_to_url(no_ext, dir_urls);
            const auto out_rel = output_rel_from_url(page.url);
            page.abs_output = config.site_root() / out_rel;
            page.title = title_from_filename(no_ext);

            // On a URL collision the later page wins here; validate() reports it.
            tree.by_url_[page.url] = tree.pages_.size();
            tree.by_rel_[page.rel_source.generic_string()] = tree.pages_.size();
            tree.pages_.push_back(std::move(page));
        }

        if (!config.nav.empty()) {
            SiteNode root;
            root.title = config.site_name;

            // Recursively turns NavEntry into SiteNode, resolving each href to a page
            // and maintaining the breadcrumb stack as it descends and returns.
            std::function < void(const std::vector<NavEntry> &, std::vector<SiteNode> &,
                                 std::vector<std::string> &) >
                    build_nav;
            build_nav = [&](const std::vector<NavEntry> &entries,
                            std::vector<SiteNode> &out,
                            std::vector<std::string> &crumbs) {
                for (const auto &[title, href, children]: entries) {
                    SiteNode node;
                    node.title = title;

                    if (!href.empty()) {
                        // Nav hrefs are source paths, and configs often omit ".md".
                        std::string target = href;
                        auto idx = tree.by_rel_.find(target);
                        if (idx == tree.by_rel_.end()) {
                            if (!target.ends_with(".md")) {
                                idx = tree.by_rel_.find(target + ".md");
                                if (idx != tree.by_rel_.end()) {
                                    target = target + ".md";
                                }
                            }
                        }

                        if (idx != tree.by_rel_.end()) {
                            node.page = &tree.pages_[idx->second];

                            if (node.title.empty()) {
                                node.title = node.page->title;
                            }
                        }
                    }

                    crumbs.push_back(node.title);

                    if (!children.empty()) {
                        build_nav(children, node.children, crumbs);
                    }

                    if (node.page) {
                        // Breadcrumb write-back: build() uniquely owns pages_, so the cast is safe.
                        auto &p = *const_cast<Page *>(node.page);
                        p.breadcrumbs = crumbs;

                        if (title.empty()) {
                            p.title = node.title;
                        } else {
                            p.title = title;
                        }
                    }

                    crumbs.pop_back();
                    out.push_back(std::move(node));
                }
            };

            std::vector<std::string> crumbs;
            build_nav(config.nav, root.children, crumbs);

            tree.nav_root_ = std::move(root);
        } else {
            // No nav configured: derive the menu from the docs directory layout.
            SiteNode root;
            root.title = config.site_name;

            auto &root_children = root.children;
            // Index-based scratch node used to group sources into directories first.
            struct TmpNav {
                std::string title;
                std::size_t page_idx = static_cast<std::size_t>(-1);
                std::vector<TmpNav> children;
                bool is_dir = false;
            };
            TmpNav tmp_root;

            for (std::size_t i = 0; i < tree.pages_.size(); ++i) {
                const auto &page = tree.pages_[i];
                const auto no_ext = strip_markdown_ext(page.rel_source.generic_string());
                // Split the source path so each directory can become a nav node.
                std::vector<std::string> segs;
                std::string cur;
                for (const char c: no_ext) {
                    if (c == '/') {
                        if (!cur.empty()) {
                            segs.push_back(cur);
                        }

                        cur.clear();
                    } else {
                        cur.push_back(c);
                    }
                }

                if (!cur.empty()) {
                    segs.push_back(cur);
                }

                const bool is_index =
                        (no_ext == "index") ||
                        (no_ext.size() > 6 && no_ext.compare(no_ext.size() - 6, 6, "/index") == 0);

                // An index page stands for its own directory, so its last segment is
                // not descended into; it instead fills that directory's node.
                TmpNav *node = &tmp_root;
                std::size_t last_dir = segs.size();
                if (is_index && !segs.empty()) {
                    last_dir = segs.size() - 1;
                }

                // Create or reuse the directory node for this segment, then descend.
                for (std::size_t s = 0; s < last_dir; ++s) {
                    auto it = std::ranges::find_if(node->children,
                                                   [&](const TmpNav &c) {
                                                       return c.is_dir && c.title == segs[s];
                                                   });
                    if (it == node->children.end()) {
                        TmpNav dir;
                        dir.title = segs[s];
                        dir.is_dir = true;
                        node->children.push_back(std::move(dir));
                        it = std::prev(node->children.end());
                    }
                    node = &(*it);
                }

                // A root index fills the site root; a nested index fills its folder
                // node; anything else becomes a leaf child.
                if (is_index) {
                    if (segs.size() == 1 && segs[0] == "index") {
                        tmp_root.page_idx = i;
                    } else {
                        node->page_idx = i;
                    }
                } else {
                    TmpNav leaf;
                    leaf.title = page.title;
                    leaf.page_idx = i;
                    node->children.push_back(std::move(leaf));
                }
            }

            // Copies the scratch tree into SiteNode form, preferring the linked page's
            // own title and writing breadcrumbs back into each page it visits.
            std::function < void(TmpNav &, SiteNode &, std::vector<std::string> &) > convert;
            convert = [&](TmpNav &src, SiteNode &dst, std::vector<std::string> &crumbs) {
                if (src.page_idx != static_cast<std::size_t>(-1)) {
                    dst.page = &tree.pages_[src.page_idx];

                    if (!src.is_dir) {
                        dst.title = tree.pages_[src.page_idx].title;
                    } else {
                        dst.title = src.title;
                    }
                } else {
                    dst.title = src.title;
                }

                if (dst.title.empty()) {
                    dst.title = src.title;
                }

                crumbs.push_back(dst.title);

                if (dst.page) {
                    auto &p = *const_cast<Page *>(dst.page);
                    p.breadcrumbs = crumbs;
                }

                for (auto &child: src.children) {
                    SiteNode cn;
                    convert(child, cn, crumbs);
                    dst.children.push_back(std::move(cn));
                }

                crumbs.pop_back();
            };

            std::vector<std::string> crumbs;
            for (auto &child: tmp_root.children) {
                SiteNode cn;
                convert(child, cn, crumbs);
                root_children.push_back(std::move(cn));
            }

            // Promote the root index to the first entry so the home page stays reachable.
            if (tmp_root.page_idx != static_cast<std::size_t>(-1)) {
                SiteNode home;
                home.page = &tree.pages_[tmp_root.page_idx];
                home.title = tree.pages_[tmp_root.page_idx].title;
                root_children.insert(root_children.begin(), std::move(home));
            }

            tree.nav_root_ = std::move(root);
        }

        return tree;
    }

    const Page *SiteTree::find_by_rel_source(const std::string_view rel) const noexcept {
        const auto it = by_rel_.find(std::string(rel));
        if (it == by_rel_.end()) {
            return nullptr;
        }

        return &pages_[it->second];
    }

    const Page *SiteTree::find_by_url(const std::string_view url) const noexcept {
        const auto it = by_url_.find(std::string(url));
        if (it == by_url_.end()) {
            return nullptr;
        }

        return &pages_[it->second];
    }

    void SiteTree::validate(const SiteConfig &config) const {
        std::vector<std::string> errors;

        // Two sources mapping to one URL would silently overwrite each other on disk.
        std::set<std::string> seen_urls;
        for (const auto &p: pages_) {
            if (!seen_urls.insert(p.url).second) {
                errors.push_back("duplicate output URL '" + p.url + "' from " +
                                 p.rel_source.generic_string());
            }
        }

        if (!config.nav.empty()) {
            // Every configured nav href must resolve to a scanned source file.
            std::function < void(const std::vector<NavEntry> &) > check_nav;
            check_nav = [&](const std::vector<NavEntry> &entries) {
                for (const auto &[title, href, children]: entries) {
                    if (!href.empty()) {
                        if (const std::string target = href;
                            !by_rel_.contains(target) && !by_rel_.contains(target + ".md")) {
                            errors.push_back("nav entry '" + title +
                                             "' references missing file: " + target);
                        }
                    }

                    check_nav(children);
                }
            };

            check_nav(config.nav);
        }

        if (config.strict && !config.nav.empty()) {
            // Strict mode: any page the nav tree cannot reach is reported as an error.
            std::set<const Page *> reachable;
            std::function < void(const std::vector<SiteNode> &) > walk =
                    [&](const std::vector<SiteNode> &nodes) {
                        for (const auto &n: nodes) {
                            if (n.page) {
                                reachable.insert(n.page);
                            }

                            walk(n.children);
                        }
                    };

            walk(nav_root_.children);

            for (const auto &p: pages_) {
                if (!reachable.contains(&p)) {
                    errors.push_back("page not reachable from nav: " +
                                     p.rel_source.generic_string());
                }
            }
        }

        // Everything is gathered first so the user can fix all problems at once.
        if (!errors.empty()) {
            std::string msg = "site validation failed:\n";
            for (const auto &e: errors) {
                msg += "  - " + e + "\n";
            }

            throw ValidationError(msg);
        }
    }
}
