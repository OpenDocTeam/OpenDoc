#include "builder.hpp"

#include "link_check.hpp"
#include "log.hpp"
#include "page_renderer.hpp"
#include "opendoc/mapper/link_resolver.hpp"
#include "opendoc/mapper/scanner.hpp"
#include "opendoc/parser/html_renderer.hpp"
#include "opendoc/plugins/build_context.hpp"
#include "opendoc/plugins/plugin_manager.hpp"
#include "opendoc/theme/theme.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>

namespace opendoc::app {
    namespace {
        // Reads a whole file as bytes; throws BuildError if it cannot be opened.
        std::string read_file(const std::filesystem::path &p) {
            std::ifstream ifs(p, std::ios::binary);
            if (!ifs) {
                throw BuildError("Cannot read file: " + p.string());
            }

            std::ostringstream ss;
            ss << ifs.rdbuf();

            std::string content = ss.str();
            // Editors sometimes save a UTF-8 BOM; it is not page content.
            if (content.starts_with("\xEF\xBB\xBF")) {
                content.erase(0, 3);
            }
            return content;
        }

        // Writes via a temp file plus rename so readers never observe a half-written page.
        void write_file_atomic(const std::filesystem::path &p, const std::string_view content) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
            const auto tmp = p.string() + ".tmp";
            {
                std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
                if (!ofs) {
                    throw BuildError("Cannot write file: " + p.string());
                }

                ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
                if (!ofs) {
                    throw BuildError("Write failed: " + p.string());
                }
            }

            std::filesystem::rename(tmp, p, ec);
            if (ec) {
                std::error_code rm_ec;
                std::filesystem::remove(p, rm_ec);
                std::filesystem::rename(tmp, p, ec);
                if (ec) {
                    std::filesystem::remove(tmp, rm_ec);
                    throw BuildError("Cannot replace file: " + p.string() + " (" +
                                     ec.message() + ")");
                }
            }
        }
    }

    Builder::Builder(mapper::SiteConfig config) : config_(std::move(config)) {
    }

    // Runs the whole build pipeline; every failure is reported as a BuildError.
    BuildStats Builder::build() {
        const auto t0 = std::chrono::steady_clock::now();
        BuildStats stats;

        log_info("Preparing to build docs... (%s)",
                 config_.docs_root().string().c_str());

        try {
            plugins_.discover(plugins::PluginManager::default_plugins_directory());
            for (const auto &w: plugins_.warnings()) {
                log_warn("plugin: %s", w.c_str());
            }

            plugins_.load(config_);

            std::string available_plugins;
            int available_plugins_count = 0;

            for (const auto &n: plugins_.available_names()) {
                if (!available_plugins.empty()) {
                    available_plugins += ", ";
                }
                available_plugins += n;
                available_plugins_count++;
            }

            log_info("Loaded %d plugins: %s", available_plugins_count, available_plugins.c_str());
        } catch (const plugins::PluginError &e) {
            throw BuildError(e.what());
        }

        try {
            plugins_.fire_on_config(config_);
        } catch (const plugins::PluginError &e) {
            throw BuildError(std::string("plugin on_config: ") + e.what());
        }

        mapper::ScanResult scan;
        try {
            scan = mapper::DocsScanner::scan(config_.docs_root());
        } catch (const std::filesystem::filesystem_error &e) {
            throw BuildError(std::string("scan failed: ") + e.what());
        }

        log_info("Found %llu markdown file(s), %llu asset(s)",
                 static_cast<unsigned long long>(scan.markdown.size()),
                 static_cast<unsigned long long>(scan.static_files.size()));

        if (scan.markdown.empty()) {
            stats.warnings.push_back("no markdown files found under " +
                                     config_.docs_root().string());
            if (config_.strict) {
                throw BuildError("strict mode: " + stats.warnings.back());
            }

            log_warn("%s", stats.warnings.back().c_str());
        }

        auto site = mapper::SiteTree::build(config_, scan);
        try {
            site.validate(config_);
        } catch (const mapper::ValidationError &e) {
            throw BuildError(e.what());
        }

        const auto site_root = config_.site_root();
        plugins::BuildContext ctx(config_, site, site_root);

        try {
            plugins_.fire_on_pre_build(ctx);
        } catch (const plugins::PluginError &e) {
            throw BuildError(std::string("plugin on_pre_build: ") + e.what());
        }

        theme::Theme th;
        try {
            th = theme::Theme::load(config_.theme,
                                    plugins_.theme_directory(config_.theme.name));
        } catch (const theme::ThemeError &e) {
            throw BuildError(e.what());
        }

        {
            std::error_code ec;
            if (std::filesystem::exists(site_root, ec)) {
                // Guard: never wipe a filesystem root or a tree that contains the docs
                // source, since remove_all below would destroy the user's work.
                const auto canon = std::filesystem::weakly_canonical(site_root, ec);
                const auto docs_canon =
                        std::filesystem::weakly_canonical(config_.docs_root(), ec);

                const bool is_root = canon.root_path() == canon;
                const bool contains_docs = [&] {
                    if (docs_canon.empty() || canon.empty() ||
                        docs_canon == canon) {
                        return false;
                    }
                    const auto d = docs_canon.generic_string();
                    const auto s = canon.generic_string();
                    return d.size() > s.size() &&
                           d.compare(0, s.size(), s) == 0 &&
                           (d[s.size()] == '/' || d[s.size()] == '\\');
                }();

                if (is_root || contains_docs) {
                    throw BuildError("refusing to clean site_dir: " +
                                     site_root.string());
                }

                std::filesystem::remove_all(site_root, ec);
                if (ec) {
                    throw BuildError("cannot clean site_dir " + site_root.string() +
                                     ": " + ec.message());
                }
            }

            std::filesystem::create_directories(site_root, ec);
            if (ec) {
                throw BuildError("cannot create site_dir " + site_root.string() +
                                 ": " + ec.message());
            }
        }

        mapper::LinkResolver resolver(site);

        const auto page_order = nav_page_order(site);
        const auto valid_targets =
                collect_valid_link_targets(site, scan.static_files, th.assets());

        const bool strict = config_.strict;

        std::string versions_html;
        if (!config_.versions.empty()) {
            // Version labels are matched case-insensitively; tolower needs unsigned char.
            auto labels_equal = [](const std::string &a, const std::string &b) {
                if (a.size() != b.size()) {
                    return false;
                }

                for (std::size_t i = 0; i < a.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(a[i])) !=
                        std::tolower(static_cast<unsigned char>(b[i]))) {
                        return false;
                    }
                }

                return true;
            };

            std::size_t current_idx = 0;
            bool found = false;
            if (!config_.current_version.empty()) {
                for (std::size_t i = 0; i < config_.versions.size(); ++i) {
                    if (labels_equal(config_.versions[i].label, config_.current_version)) {
                        current_idx = i;
                        found = true;
                        break;
                    }
                }
            }
            const std::string &current_label = config_.versions[current_idx].label;

            if (!config_.current_version.empty() && !found) {
                log_warn("current_version '%s' matches no entry in `versions`; showing '%s'",
                         config_.current_version.c_str(), current_label.c_str());
            }

            versions_html =
                    "<div class=\"version-dropdown\" id=\"version-dropdown\">"
                    "<button aria-expanded=\"false\" aria-haspopup=\"listbox\" "
                    "class=\"version-trigger\" type=\"button\" id=\"version-trigger\">"
                    "<span class=\"version-label\">";
            versions_html += parser::HtmlRenderer::escape_html(current_label);
            versions_html +=
                    "</span>"
                    "<svg aria-hidden=\"true\" fill=\"none\" height=\"12\" stroke=\"currentColor\" "
                    "stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"2\" "
                    "viewBox=\"0 0 24 24\" width=\"12\"><path d=\"m6 9 6 6 6-6\"/></svg>"
                    "</button>"
                    "<ul aria-label=\"Switch docs version\" class=\"version-menu\" "
                    "hidden id=\"version-menu\" role=\"listbox\">";
            for (std::size_t vi = 0; vi < config_.versions.size(); ++vi) {
                const auto &[label, url] = config_.versions[vi];
                const bool selected = vi == current_idx;
                versions_html += "<li role=\"option\">";
                versions_html += "<button aria-selected=\"";
                versions_html += selected ? "true" : "false";
                versions_html += "\" class=\"version-option";
                if (selected) {
                    versions_html += " is-current";
                }
                versions_html += "\" data-url=\"";
                versions_html += parser::HtmlRenderer::escape_html(url);
                versions_html += R"(" type="button">)";
                versions_html += "<span>";
                versions_html += parser::HtmlRenderer::escape_html(label);
                versions_html += "</span>";
                versions_html +=
                        "<svg aria-hidden=\"true\" class=\"check\" fill=\"none\" height=\"12\" "
                        "stroke=\"currentColor\" stroke-linecap=\"round\" stroke-linejoin=\"round\" "
                        "stroke-width=\"2\" viewBox=\"0 0 24 24\" width=\"12\">"
                        "<path d=\"M20 6 9 17l-5-5\"/></svg>";
                versions_html += "</button></li>";
            }
            versions_html += "</ul></div>";
        }

        log_info("Converting...");

        PageRenderContext rc{
            .config = config_,
            .site = site,
            .theme = th,
            .plugins = plugins_,
            .plugin_ctx = ctx,
            .resolver = resolver,
            .page_order = page_order,
            .valid_targets = valid_targets,
            .versions_html = versions_html,
        };

        for (auto &page: site.pages()) {
            std::string raw;
            try {
                raw = read_file(page.abs_source);
            } catch (const BuildError &e) {
                throw BuildError(std::string(e.what()) + " (page " +
                                 page.rel_source.string() + ")");
            }

            const auto rendered = render_page(rc, page, std::move(raw));

            for (const auto &issue: rendered.dead_links) {
                stats.warnings.push_back(issue);
                if (strict) {
                    throw BuildError("strict mode: " + issue);
                }

                log_warn("%s", issue.c_str());
            }

            write_file_atomic(page.abs_output, rendered.html);
            ++stats.pages_written;
        }

        for (const auto &[rel_path, abs_path]: scan.static_files) {
            const auto out = site_root / rel_path;
            std::error_code ec;
            std::filesystem::create_directories(out.parent_path(), ec);
            std::filesystem::copy_file(abs_path, out,
                                       std::filesystem::copy_options::overwrite_existing,
                                       ec);
            if (ec) {
                stats.warnings.push_back("failed to copy static file " +
                                         rel_path.string() + ": " + ec.message());

                if (config_.strict) {
                    throw BuildError("strict mode: " + stats.warnings.back());
                }

                log_warn("%s", stats.warnings.back().c_str());
                continue;
            }

            ++stats.assets_written;
        }

        for (const auto &asset: th.assets()) {
            const auto out = site_root / asset.out_rel_path;
            write_file_atomic(out, asset.content);
            ++stats.assets_written;
        }

        try {
            plugins_.fire_on_post_build(ctx);
        } catch (const plugins::PluginError &e) {
            throw BuildError(std::string("plugin on_post_build: ") + e.what());
        }

        const auto t1 = std::chrono::steady_clock::now();
        stats.elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);
        log_info("Built %llu page(s), %llu asset(s) in %lld ms",
                 static_cast<unsigned long long>(stats.pages_written),
                 static_cast<unsigned long long>(stats.assets_written),
                 static_cast<long long>(stats.elapsed.count()));
        return stats;
    }
}
