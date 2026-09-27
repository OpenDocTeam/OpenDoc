#pragma once

#include "opendoc/mapper/site_tree.hpp"

#include <string>
#include <string_view>

namespace opendoc::mapper {
    // Rewrites site-internal hrefs into paths that stay valid from any page depth.
    class LinkResolver {
    public:
        // Binds the resolver to the site index used to look up link targets.
        explicit LinkResolver(const SiteTree &site);

        // Rewrites href into a path relative to `from`, keeping any #fragment.
        [[nodiscard]] std::string resolve(const Page &from, std::string_view href) const;

        // Number of "../" segments needed to climb from the page back to the site root.
        [[nodiscard]] static std::string base_prefix(const Page &page);

        // Directory part of page.url, ending in '/' or empty when url has no slash.
        [[nodiscard]] static std::string url_dir(const Page &page);

        // Relative path leading from from_url to to_url; assumes both are site URLs.
        [[nodiscard]] static std::string url_to_url(const std::string &from_url,
                                                    const std::string &to_url);

        // True for http(s), mailto, protocol-relative, and other scheme-qualified hrefs.
        [[nodiscard]] static bool is_external(std::string_view href);

    private:
        const SiteTree &site_;
    };
}
