#include "log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace opendoc::app {
    namespace {
        // Read from the log entry points on other threads, hence atomic.
        std::atomic<bool> quiet_mode{false};

        // ANSI-colored tags plus the plain fallbacks used when color is off.
        constexpr auto info_tag = "\033[1;32mINFO\033[0m";
        constexpr auto warn_tag = "\033[1;33mWARN\033[0m";
        constexpr auto error_tag = "\033[1;31mERROR\033[0m";
        constexpr auto plain_info = "INFO";
        constexpr auto plain_warn = "WARN";
        constexpr auto plain_error = "ERROR";

        // Enables VT100 escape processing on the Windows console; a no-op elsewhere.
        void enable_windows_vt() {
#ifdef _WIN32
            const HANDLE handles[] = {
                GetStdHandle(STD_OUTPUT_HANDLE),
                GetStdHandle(STD_ERROR_HANDLE)
            };
            for (const auto h: handles) {
                DWORD mode = 0;
                if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
                    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
                }
            }
#endif
        }

        // Decides once per stream whether to colorize: NO_COLOR and FORCE_COLOR win
        // over the isatty check, and the answer is cached so getenv is not repeated.
        bool color_enabled(FILE *stream) {
            static std::atomic<bool> initialized_out{false}, cached_out{false};
            static std::atomic<bool> initialized_err{false}, cached_err{false};

            auto &initialized = (stream == stdout) ? initialized_out : initialized_err;
            auto &cached = (stream == stdout) ? cached_out : cached_err;

            if (!initialized.load(std::memory_order_acquire)) {
                enable_windows_vt();

                bool value;
                if (std::getenv("NO_COLOR")) {
                    value = false;
                } else if (std::getenv("FORCE_COLOR")) {
                    value = true;
                } else {
                    const int fd = fileno(stream);
                    value = fd >= 0 && isatty(fd) != 0;
                }

                cached.store(value, std::memory_order_release);
                initialized.store(true, std::memory_order_release);
            }

            return cached.load(std::memory_order_acquire);
        }

        // Maps a colored tag back to its unstyled text; anything unknown means ERROR.
        const char *plain_tag_for(const char *tag) {
            if (std::strcmp(tag, info_tag) == 0) {
                return plain_info;
            }
            if (std::strcmp(tag, warn_tag) == 0) {
                return plain_warn;
            }
            return plain_error;
        }

        // Fills time_buf with the current local time as HH:MM:SS.
        void get_timestamp(char *time_buf, const std::size_t max_size) {
            const auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);

            std::tm bt{};
#ifdef _WIN32
            localtime_s(&bt, &in_time_t);
#else
            localtime_r(&in_time_t, &bt);
#endif
            std::strftime(time_buf, max_size, "%H:%M:%S", &bt);
        }

        // Formats "[time TAG] message" into a fixed buffer, strips trailing newlines so
        // the record stays on one line, then flushes so stdout and stderr interleave cleanly.
        void emit_tagged(FILE *stream, const char *tag, const char *fmt, va_list ap) {
            char time_str[16];
            get_timestamp(time_str, sizeof(time_str));

            char buf[4096];
            const int written = std::vsnprintf(buf, sizeof(buf), fmt, ap);
            const std::size_t len =
                    written > 0
                        ? std::min(static_cast<std::size_t>(written),
                                   sizeof(buf) - 1U)
                        : 0U;
            std::string_view msg(buf, len);

            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
                msg.remove_suffix(1);
            }

            const char *active_tag = color_enabled(stream) ? tag : plain_tag_for(tag);

            std::fprintf(stream, "[%s %s] %.*s\n", time_str, active_tag,
                         static_cast<int>(msg.size()), msg.data());

            std::fflush(stream);
        }
    }

    void set_quiet(const bool quiet) noexcept {
        quiet_mode = quiet;
    }

    void log_info(const char *fmt, ...) {
        if (quiet_mode) {
            return;
        }

        va_list ap;
        va_start(ap, fmt);
        emit_tagged(stdout, info_tag, fmt, ap);
        va_end(ap);
    }

    void log_warn(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        emit_tagged(stderr, warn_tag, fmt, ap);
        va_end(ap);
    }

    void log_error(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        emit_tagged(stderr, error_tag, fmt, ap);
        va_end(ap);
    }
}
