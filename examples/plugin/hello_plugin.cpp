#include <opendoc/plugins/build_context.hpp>
#include <opendoc/plugins/export.hpp>
#include <opendoc/plugins/plugin.hpp>

#include <string>

namespace {
    // Example plugin that touches every hook so hosts can be smoke tested.
    class HelloPlugin final : public opendoc::plugins::Plugin {
    public:
        // Stable identifier used in config files and scratch data.
        [[nodiscard]] std::string name() const override {
            return "hello";
        }

        // Fills in a missing site URL so absolute links resolve.
        void on_config(opendoc::mapper::SiteConfig &config) override {
            // Require a base URL for absolute links.
            if (config.site_url.empty()) {
                config.site_url = "https://example.com";
            }
        }

        // Records a marker in per-build scratch storage before any page work.
        void on_pre_build(opendoc::plugins::BuildContext &ctx) override {
            ctx.scratch(name())["stage"] = "pre_build";
            ++pre_build_count_;
        }

        // Counts pages after Markdown is read, before it is parsed.
        void on_page_markdown(opendoc::plugins::BuildContext &,
                              const opendoc::mapper::Page &page,
                              std::string &markdown) override {
            // Tag every page's source so the transform is visible in debugging.
            ++markdown_pages_;
            (void) page;
            (void) markdown;
        }

        // Counts pages once their Markdown has been parsed into a ParsedPage.
        void on_page_content(opendoc::plugins::BuildContext &,
                             const opendoc::mapper::Page &,
                             opendoc::parser::ParsedPage &) override {
            ++content_pages_;
        }

        // Exposes a template variable so layouts can show the plugin ran.
        void on_page_context(opendoc::plugins::BuildContext &,
                             const opendoc::mapper::Page &,
                             opendoc::theme::TemplateContext &tctx) override {
            tctx.set("hello_plugin", "Hello from the OpenDoc plugin");
        }

        // Inserts a marker comment just before </body>, if the page has one.
        void on_post_page(opendoc::plugins::BuildContext &,
                          const opendoc::mapper::Page &page,
                          std::string &html) override {
            const auto pos = html.find("</body>");
            if (pos == std::string::npos) {
                return;
            }

            html.insert(pos, "<!-- hello-plugin: " + page.url + " -->\n");
        }

        // Writes a build report artifact summarizing how often hooks fired.
        void on_post_build(opendoc::plugins::BuildContext &ctx) override {
            std::string report = "hello plugin report\n";
            report += "pre_build_calls: " + std::to_string(pre_build_count_) + "\n";
            report += "markdown_pages: " + std::to_string(markdown_pages_) + "\n";
            report += "content_pages: " + std::to_string(content_pages_) + "\n";
            report += "pages: " + std::to_string(ctx.site().pages().size()) + "\n";

            ctx.write_artifact("hello.txt", report);
        }

    private:
        // Hook invocation counters reported by on_post_build.
        int pre_build_count_ = 0;
        int markdown_pages_ = 0;
        int content_pages_ = 0;
    };
}

extern "C" {
// Reports the plugin ABI version so the host can reject mismatches.
OPENDOC_PLUGIN_EXPORT int opendoc_plugin_api_version() {
    return OPENDOC_PLUGIN_API_VERSION;
}

// Factory called by the host; ownership passes to opendoc_destroy_plugin.
OPENDOC_PLUGIN_EXPORT opendoc::plugins::Plugin *opendoc_create_plugin() {
    return new HelloPlugin();
}

// Releases a plugin instance created by opendoc_create_plugin.
OPENDOC_PLUGIN_EXPORT void opendoc_destroy_plugin(opendoc::plugins::Plugin *plugin) {
    delete plugin;
}
}
