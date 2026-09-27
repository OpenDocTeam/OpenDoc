#include "builtin.hpp"

#include "encoding.hpp"

#include "opendoc/plugins/build_context.hpp"

namespace opendoc::plugins::builtin {
    namespace {
        // Writes a sitemap.xml covering every page once the site tree is complete.
        class SitemapPlugin final : public Plugin {
        public:
            // Identifier used in the site config's plugin list.
            [[nodiscard]] std::string name() const override {
                return "sitemap";
            }

            // <loc> entries must be absolute URLs, so a config without site_url gets
            // no sitemap at all rather than a file full of relative links.
            void on_post_build(BuildContext &ctx) override {
                const auto &base = ctx.config().site_url;
                if (base.empty()) {
                    return;
                }

                // Normalize to a trailing slash so joining with page.url never doubles
                // or drops a separator.
                std::string prefix = base;
                if (prefix.back() != '/') {
                    prefix.push_back('/');
                }

                std::string xml =
                        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<urlset xmlns=\"http://www.sitemaps.org/ns/sitemap/0.9\">\n";

                // One <url> per page; xml_escape matters because URLs may contain
                // query strings with '&' or other markup-hostile characters.
                for (const auto &page: ctx.site().pages()) {
                    xml += "  <url><loc>";
                    xml += encoding::xml_escape(prefix + page.url);
                    xml += "</loc></url>\n";
                }

                xml += "</urlset>\n";
                ctx.write_artifact("sitemap.xml", xml);
            }
        };
    }

    PluginFactory sitemap_factory() {
        return [] {
            return std::make_unique<SitemapPlugin>();
        };
    }
}
