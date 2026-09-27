#include "cli.hpp"

#include "log.hpp"

#include <cstdio>
#include <string>

namespace opendoc::app {
    void print_help() {
        std::printf(
            "OpenDoc: A high-performance static site generator written in C++.\n"
            "\n"
            "Usage:\n"
            "  opendoc build [options]     Build the site\n"
            "  opendoc serve [options]     Build and serve locally\n"
            "  opendoc --help              Show help\n"
            "  opendoc --version           Show version\n"
            "\n"
            "Options:\n"
            "  -c, --config <file>         global: Configuration path (default: opendoc.yaml)\n"
            "  --strict                    global: Treat build warnings as errors\n"
            "  --quiet                     global: Suppress informational output\n"
            "  --host <addr>               serve: Bind address (default: 127.0.0.1)\n"
            "  --port <port>               serve: Port (default: 8000)\n"
            "  --no-build                  serve: Skip initial build\n"
            "  --watch                     build/serve: Rebuild when docs/ or config change\n"
            "\n"
            "This project is licensed under the MIT license.\n");
    }

    void print_version() {
        std::printf("OpenDoc %s\nBuilt on %s\n", OPENDOC_VERSION, OPENDOC_BUILD_DATE);
    }

    int parse_cli(const std::span<const std::string_view> args, CliOptions &opts) {
        if (args.empty()) {
            opts.command = CliOptions::Command::Help;
            return 0;
        }

        std::size_t i = 0;

        // Only help, version, build, and serve are valid as the first argument.
        if (args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
            opts.command = CliOptions::Command::Help;
            return 0;
        }

        if (args[0] == "--version" || args[0] == "-V") {
            opts.command = CliOptions::Command::Version;
            return 0;
        }

        if (args[0] == "build") {
            opts.command = CliOptions::Command::Build;
            i = 1;
        } else if (args[0] == "serve") {
            opts.command = CliOptions::Command::Serve;
            i = 1;
        } else {
            log_error("Unknown command '%s'", std::string(args[0]).c_str());
            std::fprintf(stderr, "Run 'opendoc --help' for usage.\n");
            return 2;
        }

        // Takes the argument after the flag, or logs an error and yields an empty value.
        auto need_value = [&](const std::string_view flag, std::size_t &idx)
            -> std::string_view {
            if (idx + 1 >= args.size()) {
                log_error("Missing value for %s", std::string(flag).c_str());
                return {};
            }
            ++idx;
            return args[idx];
        };

        for (; i < args.size(); ++i) {
            if (const auto a = args[i]; a == "--help" || a == "-h") {
                opts.command = CliOptions::Command::Help;
                return 0;
            } else if (a == "--version" || a == "-V") {
                opts.command = CliOptions::Command::Version;
                return 0;
            } else if (a == "-c" || a == "--config") {
                const auto v = need_value(a, i);

                if (v.empty() && i + 1 >= args.size()) {
                    return 2;
                }

                opts.config_path = std::string(v);
            } else if (a == "--strict") {
                opts.strict = true;
            } else if (a == "--quiet" || a == "-q") {
                opts.quiet = true;
            } else if (a == "--host") {
                const auto v = need_value(a, i);

                if (v.empty() && i + 1 >= args.size()) {
                    return 2;
                }

                opts.host = std::string(v);
            } else if (a == "--port") {
                const auto v = need_value(a, i);

                if (v.empty() && i + 1 >= args.size()) {
                    return 2;
                }

                // stoi throws on garbage or out-of-range input; both give one message.
                try {
                    const int p = std::stoi(std::string(v));

                    if (p < 0 || p > 65535) {
                        throw std::out_of_range("port");
                    }

                    opts.port = static_cast<std::uint16_t>(p);
                } catch (...) {
                    log_error("Invalid port '%s'", std::string(v).c_str());
                    return 2;
                }
            } else if (a == "--no-build") {
                opts.no_build = true;
            } else if (a == "--watch") {
                opts.watch = true;
            } else {
                log_error("Unknown option '%s'", std::string(a).c_str());
                return 2;
            }
        }

        return 0;
    }
}
