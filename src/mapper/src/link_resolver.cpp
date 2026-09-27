#include "opendoc/mapper/link_resolver.hpp"

#include <vector>

namespace opendoc::mapper {
    bool LinkResolver::is_external(const std::string_view href) {
        return href.starts_with("http://") || href.starts_with("https://") ||
               href.starts_with("//") || href.starts_with("mailto:") ||
               href.starts_with("ftp://") || href.starts_with("tel:") ||
               href.starts_with("javascript:") || href.starts_with("data:");
    }

    namespace {
        // Splits on '/', dropping empty segments and "." so paths compare segment-wise.
        std::vector<std::string> split_path(const std::string_view path) {
            std::vector<std::string> parts;
            std::string cur;

            for (const char c: path) {
                if (c == '/') {
                    if (!cur.empty() && cur != ".") {
                        parts.push_back(cur);
                    }

                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }

            if (!cur.empty() && cur != ".") {
                parts.push_back(cur);
            }

            return parts;
        }

        // Builds the path from from_dir to to, emitting "../" for each segment not shared.
        std::string rel_path(const std::vector<std::string> &from_dir,
                             const std::vector<std::string> &to, const bool to_is_dir) {
            std::size_t common = 0;
            while (common < from_dir.size() && common < to.size() &&
                   from_dir[common] == to[common]) {
                ++common;
            }

            std::string out;
            for (std::size_t i = common; i < from_dir.size(); ++i) {
                out += "../";
            }
            for (std::size_t i = common; i < to.size(); ++i) {
                out += to[i];
                if (i + 1 < to.size() || to_is_dir) {
                    out.push_back('/');
                }
            }

            // Nothing left to append means both sides landed in the same directory:
            // "." for a directory target, the bare file name otherwise.
            if (out.empty()) {
                if (to_is_dir) {
                    return "./";
                }

                return to.empty() ? "" : to.back();
            }

            // Target is the site root, so only the climb-ups apply (e.g. "../").
            if (to.empty() && to_is_dir) {
                out.clear();

                for (std::size_t i = common; i < from_dir.size(); ++i) {
                    out += "../";
                }

                if (out.empty()) {
                    out = "./";
                }

                return out;
            }

            return out;
        }

        // Splits off the "#fragment"; frag is empty when the href carries none.
        std::string_view split_fragment(const std::string_view href, std::string_view &frag) {
            const auto hash = href.find('#');
            if (hash == std::string_view::npos) {
                frag = {};
                return href;
            }
            frag = href.substr(hash + 1);
            return href.substr(0, hash);
        }
    }

    LinkResolver::LinkResolver(const SiteTree &site) : site_(site) {
    }

    std::string LinkResolver::url_dir(const Page &page) {
        const auto &u = page.url;
        if (u.empty()) {
            return "";
        }
        if (u.back() == '/') {
            return u;
        }

        const auto slash = u.find_last_of('/');
        if (slash == std::string::npos) {
            return "";
        }

        return u.substr(0, slash + 1);
    }

    std::string LinkResolver::base_prefix(const Page &page) {
        const auto dir = url_dir(page);
        if (dir.empty()) {
            return "";
        }

        const auto parts = split_path(dir);
        std::string out;

        for ([[maybe_unused]] const auto &p: parts) {
            out += "../";
        }

        return out;
    }

    std::string LinkResolver::url_to_url(const std::string &from_url,
                                         const std::string &to_url) {
        if (is_external(to_url)) {
            return to_url;
        }

        const auto from_parts = split_path(from_url);
        auto to_parts = split_path(to_url);
        const bool to_is_dir = to_url.empty() || to_url.back() == '/';

        std::size_t common = 0;
        while (common < from_parts.size() && common < to_parts.size() &&
               from_parts[common] == to_parts[common]) {
            ++common;
        }

        std::string out;
        for (std::size_t i = common; i < from_parts.size(); ++i) {
            out += "../";
        }
        for (std::size_t i = common; i < to_parts.size(); ++i) {
            out += to_parts[i];
            if (i + 1 < to_parts.size() || to_is_dir) {
                out.push_back('/');
            }
        }

        if (out.empty()) {
            return to_is_dir ? "./" : to_parts.back();
        }

        return out;
    }

    std::string LinkResolver::resolve(const Page &from, const std::string_view href) const {
        if (href.empty()) {
            return std::string(href);
        }

        if (is_external(href)) {
            return std::string(href);
        }

        std::string_view frag;
        const auto path_part = split_fragment(href, frag);
        std::string frag_str;

        if (!frag.empty()) {
            frag_str = "#" + std::string(frag);
        }

        if (path_part.empty()) {
            return std::string(href);
        }

        // The query string is dropped for lookup and never re-emitted; only the
        // fragment survives, because it refers to anchors in the final document.
        std::string_view path = path_part;
        if (const auto q = path.find('?'); q != std::string_view::npos) {
            path = path.substr(0, q);
        }

        // Relative hrefs are anchored on the linking file's own directory.
        const std::string src_rel = from.rel_source.generic_string();
        std::string src_dir;
        if (const auto slash = src_rel.find_last_of('/'); slash != std::string::npos) {
            src_dir = src_rel.substr(0, slash);
        }

        // Folds "." and ".." onto base; a base naming a file drops its last segment first.
        auto resolve_rel = [](std::vector<std::string> base, const bool base_is_dir,
                              const std::vector<std::string> &segs) {
            if (!base_is_dir && !base.empty()) {
                base.pop_back();
            }

            for (const auto &s: segs) {
                if (s == ".") {
                    continue;
                }

                if (s == "..") {
                    if (!base.empty()) {
                        base.pop_back();
                    }
                } else {
                    base.push_back(s);
                }
            }

            return base;
        };

        // Resolve against markdown sources first so hrefs can omit the extension and
        // still end up at the output URL that source actually maps to.
        {
            // Rejoins segments with '/' and never emits a trailing slash.
            auto join = [](const std::vector<std::string> &parts) {
                std::string out;

                for (std::size_t i = 0; i < parts.size(); ++i) {
                    if (i) {
                        out.push_back('/');
                    }

                    out += parts[i];
                }

                return out;
            };
            const auto segs = split_path(path);
            const auto resolved_src = resolve_rel(split_path(src_dir), true, segs);
            std::string joined = join(resolved_src);

            // Order matters: the page itself, then its directory index, then a root
            // index, then the path verbatim for links that already name an output file.
            std::vector<std::string> candidates;
            {
                std::string c = joined;
                if (!c.empty() && c.back() == '/') {
                    c.pop_back();
                }

                std::string with_md = c;
                if (!with_md.ends_with(".md") && !with_md.ends_with(".markdown")) {
                    with_md += ".md";
                }

                candidates.push_back(with_md);
                candidates.push_back(c + "/index.md");

                if (c.empty()) {
                    candidates.emplace_back("index.md");
                }

                candidates.push_back(joined);
            }

            for (const auto &cand: candidates) {
                if (const auto *target = site_.find_by_rel_source(cand)) {
                    const std::string &to_url = target->url;
                    const bool to_is_dir = to_url.empty() || to_url.back() == '/';
                    const auto from_url_dir_parts = split_path(url_dir(from));
                    const auto to_url_parts = split_path(to_url);
                    const auto rel = rel_path(from_url_dir_parts, to_url_parts, to_is_dir);

                    return rel + frag_str;
                }
            }
        }

        // No markdown source matched, so treat the href as an output path already
        // relative to the docs root (static assets, generated files, or "." / "..").
        {
            const auto segs = split_path(path);
            const auto resolved_out = resolve_rel(split_path(src_dir), true, segs);
            const auto from_url_dir_parts = split_path(url_dir(from));
            const bool was_dir = !path.empty() && path.back() == '/';
            auto rel = rel_path(from_url_dir_parts, resolved_out,
                                was_dir || resolved_out.empty());

            if (rel.empty() && resolved_out.empty()) {
                rel = "./";
            }

            return rel + frag_str;
        }
    }
}
