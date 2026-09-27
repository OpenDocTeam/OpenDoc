#pragma once

#include "opendoc/parser/ast.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace opendoc::parser::detail {
    // True when the view is empty or made up entirely of whitespace.
    inline bool is_blank(const std::string_view s) {
        return std::ranges::all_of(s, [](const unsigned char c) {
            return std::isspace(c);
        });
    }

    // Drops trailing whitespace without allocating.
    inline std::string_view rtrim(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
            s.remove_suffix(1);
        }
        return s;
    }

    // Drops leading whitespace without allocating.
    inline std::string_view ltrim(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
            s.remove_prefix(1);
        }
        return s;
    }

    // Drops whitespace from both ends of the view.
    inline std::string_view trim(const std::string_view s) {
        return rtrim(ltrim(s));
    }

    // Splits on '\n' and strips one trailing '\r' per line, so CRLF input yields
    // the same line set as LF input; a trailing newline does not create an extra line.
    inline std::vector<std::string_view> split_lines(std::string_view text) {
        std::vector<std::string_view> lines;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            if (nl == std::string_view::npos) {
                if (start < text.size()) {
                    std::string_view line = text.substr(start);

                    if (!line.empty() && line.back() == '\r') {
                        line.remove_suffix(1);
                    }

                    lines.push_back(line);
                }
                break;
            }

            std::string_view line = text.substr(start, nl - start);

            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }

            lines.push_back(line);
            start = nl + 1;
        }
        return lines;
    }

    // Characters that may be neutralised by a preceding backslash.
    inline bool is_escapable(char c) {
        return std::string_view("\\`*_{}[]()#+-.!<>~|\"'&$%@^=:;/?,").find(c) !=
               std::string_view::npos;
    }

    // Strips backslashes that escape an escapable character, leaving the rest alone.
    inline std::string resolve_escapes(std::string_view s) {
        std::string out;
        out.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size() && is_escapable(s[i + 1])) {
                out.push_back(s[i + 1]);
                ++i;
            } else {
                out.push_back(s[i]);
            }
        }
        return out;
    }

    // True when two markers can continue the same list: both ordered (digits carry
    // no identity) or the exact same bullet character.
    inline bool same_list_marker(char a, char b) {
        const bool a_ord = std::isdigit(static_cast<unsigned char>(a)) != 0;
        const bool b_ord = std::isdigit(static_cast<unsigned char>(b)) != 0;

        if (a_ord != b_ord) {
            return false;
        }
        if (a_ord) {
            return true;
        }
        return a == b;
    }
}
