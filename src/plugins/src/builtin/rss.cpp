#include "builtin.hpp"

#include "encoding.hpp"

#include "opendoc/plugins/build_context.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace opendoc::plugins::builtin {
    namespace {
        // Formats a file timestamp as an RFC 822 date, the format RSS pubDate needs.
        // file_time_type uses an implementation-defined clock, so it is converted to
        // system_clock first: clock_cast where available, otherwise a now-relative
        // duration offset for libc++. Times are rendered in GMT, as the format demands.
        std::string rss_date(const std::filesystem::file_time_type &ftime) {
            using namespace std::chrono;
#if defined(_LIBCPP_VERSION)
            const auto sys_time = time_point_cast<system_clock::duration>(
                system_clock::now()
                + (ftime - std::filesystem::file_time_type::clock::now()));
#else
            const auto sys_time = clock_cast<system_clock>(ftime);
#endif
            const std::time_t t = system_clock::to_time_t(sys_time);
            std::tm tm{};
#ifdef _WIN32
            gmtime_s(&tm, &t);
#else
            gmtime_r(&t, &tm);
#endif

            char buf[64];
            if (std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm) == 0) {
                return {};
            }

            return buf;
        }

        // Emits rss.xml after the build, from page metadata the mapper already
        // collected, so no page content has to be re-rendered here.
        class RssPlugin final : public Plugin {
        public:
            // Identifier used in the site config's plugin list.
            [[nodiscard]] std::string name() const override {
                return "rss";
            }

            // Channel links and item guids must be absolute, so without site_url
            // there is nothing valid to emit and the plugin stays silent.
            void on_post_build(BuildContext &ctx) override {
                const auto &base = ctx.config().site_url;
                if (base.empty()) {
                    return;
                }

                // Trailing slash so prefix + page.url always joins cleanly.
                std::string prefix = base;
                if (prefix.back() != '/') {
                    prefix.push_back('/');
                }

                // The feed has no source date of its own, so reuse the newest source
                // file's mtime; pages written by the build itself all share it.
                std::string pub_date;
                std::error_code ec;
                if (!ctx.site().pages().empty()) {
                    const auto ft = std::filesystem::last_write_time(
                        ctx.site().pages().front().abs_source, ec);
                    if (!ec) {
                        pub_date = rss_date(ft);
                    }
                }

                std::string xml =
                        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<rss version=\"2.0\">\n<channel>\n";
                xml += "  <title>" + encoding::xml_escape(ctx.config().site_name) + "</title>\n";
                xml += "  <link>" + encoding::xml_escape(base) + "</link>\n";
                // Fall back to the site name so the channel is never description-less.
                xml += "  <description>" +
                        encoding::xml_escape(ctx.config().site_description.empty()
                                                 ? ctx.config().site_name
                                                 : ctx.config().site_description) +
                        "</description>\n";

                if (!pub_date.empty()) {
                    xml += "  <lastBuildDate>" + encoding::xml_escape(pub_date) +
                            "</lastBuildDate>\n";
                }

                // One item per page; the absolute URL is also used as a permalink guid.
                for (const auto &page: ctx.site().pages()) {
                    const auto link = prefix + page.url;
                    xml += "  <item>\n";
                    xml += "    <title>" + encoding::xml_escape(page.title) + "</title>\n";
                    xml += "    <link>" + encoding::xml_escape(link) + "</link>\n";
                    xml += "    <guid isPermaLink=\"true\">" + encoding::xml_escape(link) +
                            "</guid>\n";

                    // Explicit description first; otherwise quote the page text,
                    // truncated on a code point boundary so feeds stay valid UTF-8.
                    std::string desc = page.description;
                    if (desc.empty() && !page.plain_text.empty()) {
                        desc = encoding::utf8_truncate(page.plain_text, 280);
                    }

                    if (!desc.empty()) {
                        xml += "    <description>" + encoding::xml_escape(desc) +
                                "</description>\n";
                    }
                    if (!pub_date.empty()) {
                        xml += "    <pubDate>" + encoding::xml_escape(pub_date) + "</pubDate>\n";
                    }
                    xml += "  </item>\n";
                }

                xml += "</channel>\n</rss>\n";
                ctx.write_artifact("rss.xml", xml);
            }
        };
    }

    PluginFactory rss_factory() {
        return [] {
            return std::make_unique<RssPlugin>();
        };
    }
}
