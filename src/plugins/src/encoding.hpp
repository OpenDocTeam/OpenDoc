#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace opendoc::plugins::encoding {
    // Replaces each malformed byte run with U+FFFD so the result is always valid UTF-8.
    [[nodiscard]] std::string sanitize_utf8(std::string_view s);

    // Escapes a value for use inside a JSON string literal, sanitizing UTF-8 first.
    [[nodiscard]] std::string json_escape(std::string_view s);

    // Escapes the five XML metacharacters so text can be embedded in element content.
    [[nodiscard]] std::string xml_escape(std::string_view s);

    // Cuts a string down to max_bytes without splitting a code point, trimming to a
    // word boundary when possible, then appending an ellipsis.
    [[nodiscard]] std::string utf8_truncate(std::string in, std::size_t max_bytes);
}
