#include "builder.hpp"
#include "cli.hpp"
#include "log.hpp"
#include "serve/http_server.hpp"

#include "opendoc/mapper/config.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
    // Loads the config, applies a --strict override, and generates the site once.
    int run_build(const opendoc::app::CliOptions &opts) {
        using opendoc::app::Builder;

        auto config = opendoc::mapper::SiteConfig::load(opts.config_path);

        if (opts.strict) {
            config.strict = true;
        }

        Builder builder(std::move(config));
        builder.build();

        return 0;
    }

    // Modification times of the config plus every regular file under docs_dir.
    struct WatchSnapshot {
        std::filesystem::file_time_type config_time{};
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type> > files;

        // True when nothing watched changed between the two snapshots.
        bool operator==(const WatchSnapshot &o) const {
            return config_time == o.config_time && files == o.files;
        }
    };

    // Records mtimes for the config and all docs files; errors are tolerated because
    // a watch loop must keep running even while a file is temporarily unreadable.
    WatchSnapshot take_snapshot(const std::filesystem::path &config_path) {
        WatchSnapshot snap;
        std::error_code ec;
        snap.config_time = std::filesystem::last_write_time(config_path, ec);

        std::filesystem::path docs = "docs";
        {
            if (std::error_code yec; std::filesystem::exists(config_path, yec)) {
                try {
                    // Resolve docs via the config so a non-default docs_dir is watched too.
                    const auto cfg = opendoc::mapper::SiteConfig::load(config_path);
                    docs = cfg.docs_root();
                } catch (...) {
                    // Unusable config: assume docs/ sits beside it.
                    docs = config_path.parent_path() / "docs";
                }
            }
        }

        if (std::filesystem::exists(docs, ec)) {
            std::error_code iter_ec;
            std::filesystem::recursive_directory_iterator it(
                docs, std::filesystem::directory_options::skip_permission_denied, iter_ec);
            for (const std::filesystem::recursive_directory_iterator end;
                 it != end; it.increment(iter_ec)) {
                if (iter_ec) {
                    iter_ec.clear();
                    continue;
                }

                std::error_code fec;
                if (!it->is_regular_file(fec)) {
                    continue;
                }

                auto t = std::filesystem::last_write_time(it->path(), fec);

                if (fec) {
                    continue;
                }

                snap.files.emplace_back(it->path(), t);
            }
        }

        // Sorting by path keeps snapshot comparison independent of listing order.
        std::ranges::sort(snap.files, [](const auto &a, const auto &b) {
            return a.first < b.first;
        });

        return snap;
    }

    // Builds once, then rebuilds whenever a snapshot differs; runs until interrupted.
    int run_build_watch(const opendoc::app::CliOptions &opts) {
        int rc = run_build(opts);
        auto prev = take_snapshot(opts.config_path);
        opendoc::app::log_info("Watching for changes... (Ctrl+C to stop)");

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            auto cur = take_snapshot(opts.config_path);

            if (cur == prev) {
                continue;
            }

            prev = std::move(cur);

            opendoc::app::log_info("Change detected, rebuilding...");

            try {
                rc = run_build(opts);
            } catch (const std::exception &e) {
                opendoc::app::log_error("Build error: %s", e.what());
                rc = 1;
            }

            opendoc::app::log_info("Watching for changes... (Ctrl+C to stop)");
        }

        return rc;
    }

    // Builds unless --no-build, then serves site_root; with --watch it also rebuilds
    // on a background thread whenever docs or the config change.
    int run_serve(const opendoc::app::CliOptions &opts) {
        if (!opts.no_build) {
            if (const int rc = run_build(opts); rc != 0) {
                return rc;
            }
        }

        const auto config = opendoc::mapper::SiteConfig::load(opts.config_path);
        const auto site_root = config.site_root();

        opendoc::app::log_info("Preparing to serve docs... (%s)",
                               site_root.string().c_str());

        if (!std::filesystem::exists(site_root)) {
            opendoc::app::log_error(
                "Site directory missing: %s (run without --no-build)",
                site_root.string().c_str());
            return 1;
        }

        opendoc::app::log_info("Preparing http server...");

        opendoc::serve::ServerOptions so;
        so.host = opts.host;
        so.port = opts.port;
        so.root = site_root;

        try {
            opendoc::serve::HttpServer server(so);
            server.start();
            opendoc::serve::install_stop_signal(server);

            std::atomic<bool> stop_watch{false};
            std::thread watcher;
            // Rebuilds run off the main thread so the server never blocks on a build.
            if (opts.watch) {
                watcher = std::thread([&, cfg_path = opts.config_path] {
                    auto prev = take_snapshot(cfg_path);

                    while (!stop_watch.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));

                        auto cur = take_snapshot(cfg_path);

                        if (cur == prev) {
                            continue;
                        }

                        prev = std::move(cur);

                        opendoc::app::log_info("Change detected, rebuilding...");

                        try {
                            run_build(opts);
                        } catch (const std::exception &e) {
                            opendoc::app::log_error("Build error: %s", e.what());
                        }
                    }
                });
            }

            opendoc::app::log_info("Serving at http://%s:%u/. (Ctrl+C to stop)",
                                   opts.host.c_str(),
                                   static_cast<unsigned>(server.bound_port()));

            server.wait_until_stopped();
            // Signal the watcher only once the server is down, then join it so no
            // rebuild can start against a torn-down server.
            stop_watch.store(true);

            if (watcher.joinable()) {
                watcher.join();
            }

            opendoc::app::log_info("Server stopped.");
            return 0;
        } catch (const opendoc::serve::ServerError &e) {
            opendoc::app::log_error("Server error: %s", e.what());
            return 1;
        }
    }
}

// Parses argv, applies the global flags, and dispatches to the requested command.
int main(const int argc, char **argv) {
    std::vector<std::string_view> args;
    args.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);

    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    opendoc::app::CliOptions opts;
    if (const int parse_rc = opendoc::app::parse_cli(args, opts); parse_rc != 0) {
        return parse_rc;
    }

    opendoc::app::set_quiet(opts.quiet);

    switch (opts.command) {
        case opendoc::app::CliOptions::Command::Help:
            opendoc::app::print_help();
            return 0;
        case opendoc::app::CliOptions::Command::Version:
            opendoc::app::print_version();
            return 0;
        case opendoc::app::CliOptions::Command::Build:
        case opendoc::app::CliOptions::Command::Serve:
            break;
    }

    try {
        if (opts.command == opendoc::app::CliOptions::Command::Build) {
            if (opts.watch) {
                return run_build_watch(opts);
            }
            return run_build(opts);
        }

        return run_serve(opts);
    } catch (const opendoc::mapper::ConfigError &e) {
        opendoc::app::log_error("Config error: %s", e.what());
        return 1;
    } catch (const opendoc::mapper::ValidationError &e) {
        opendoc::app::log_error("%s", e.what());
        return 1;
    } catch (const opendoc::app::BuildError &e) {
        opendoc::app::log_error("Build error: %s", e.what());
        return 1;
    } catch (const std::exception &e) {
        opendoc::app::log_error("Unexpected error: %s", e.what());
        return 1;
    }
}
