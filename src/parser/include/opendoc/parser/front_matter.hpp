#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace opendoc::parser {
    // Parse failure that carries the 1-based source line when one is known.
    class ParseError : public std::runtime_error {
    public:
        // Keeps the message and records the offending line, 0 when unknown.
        ParseError(std::string message, const std::size_t line = 0)
            : std::runtime_error(std::move(message)), line_(line) {
        }

        // Line number the error points at, or 0 if none was supplied.
        [[nodiscard]] std::size_t line() const noexcept { return line_; }

    private:
        std::size_t line_ = 0;
    };

    // Key/value metadata parsed out of the leading YAML block.
    struct FrontMatter {
        std::map<std::string, std::string> fields;

        // True when the key exists, even if its value is empty.
        [[nodiscard]] bool has(std::string_view key) const;

        // Value for the key, or def when the key is absent.
        [[nodiscard]] std::string get(std::string_view key, std::string_view def = {}) const;
    };

    // Result of a split: parsed metadata plus the untouched document body.
    struct SplitResult {
        FrontMatter front;
        std::string_view body;
    };

    // Splits an optional leading "---" YAML block off source; body aliases source,
    // so the caller must keep source alive for as long as the view is used.
    SplitResult split_front_matter(std::string_view source);
}
