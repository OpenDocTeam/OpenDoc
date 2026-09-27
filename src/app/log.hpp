#pragma once

// Enables printf format checking of the log arguments on GCC/Clang; no-op elsewhere.
#if defined(__GNUC__)
#define OPENDOC_PRINTF(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))
#else
#define OPENDOC_PRINTF(fmt_idx, first_arg)
#endif

namespace opendoc::app {
    // Turns informational output on or off; warnings and errors are never suppressed.
    void set_quiet(bool quiet) noexcept;

    // Prints a timestamped INFO line to stdout, unless quiet mode is enabled.
    void log_info(const char *fmt, ...) OPENDOC_PRINTF(1, 2);

    // Prints a timestamped WARN line to stderr regardless of quiet mode.
    void log_warn(const char *fmt, ...) OPENDOC_PRINTF(1, 2);

    // Prints a timestamped ERROR line to stderr regardless of quiet mode.
    void log_error(const char *fmt, ...) OPENDOC_PRINTF(1, 2);
}
