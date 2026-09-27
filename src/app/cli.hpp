#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace opendoc::app {
    // Everything the command line can configure, with defaults for bare `opendoc`.
    struct CliOptions {
        // Which subcommand was requested; defaults to Help when nothing parses.
        enum class Command { Build, Serve, Help, Version };

        Command command = Command::Help;
        std::filesystem::path config_path = "opendoc.yaml";
        std::string host = "127.0.0.1";
        std::uint16_t port = 8000;
        bool strict = false;
        bool no_build = false;
        bool quiet = false;
        bool watch = false;
    };

    // Parses args (argv minus the program name) into opts; returns a process exit code.
    int parse_cli(std::span<const std::string_view> args, CliOptions &opts);

    // Prints the usage and options text to stdout.
    void print_help();

    // Prints the version and build date to stdout.
    void print_version();
}
