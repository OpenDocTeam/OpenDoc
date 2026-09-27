#include "encoding.hpp"

#include <cstdio>

namespace opendoc::plugins::encoding {
    namespace {
        // U+FFFD REPLACEMENT CHARACTER, the standard stand-in for invalid input.
        constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

        // Returns the byte length of the well-formed UTF-8 sequence starting at i,
        // or 0 when the bytes there are truncated, malformed, or not a lead byte.
        std::size_t decode_utf8(const std::string_view s, const std::size_t i) {
            const auto byte = [&](const std::size_t k) {
                return static_cast<unsigned char>(s[k]);
            };

            const unsigned char c = byte(i);
            std::size_t len;
            unsigned int cp;

            if (c < 0x80) {
                return 1;
            }
            if ((c & 0xE0) == 0xC0) {
                len = 2;
                cp = c & 0x1FU;
            } else if ((c & 0xF0) == 0xE0) {
                len = 3;
                cp = c & 0x0FU;
            } else if ((c & 0xF8) == 0xF0) {
                len = 4;
                cp = c & 0x07U;
            } else {
                return 0;
            }

            if (i + len > s.size()) {
                return 0;
            }

            for (std::size_t k = 1; k < len; ++k) {
                const unsigned char cc = byte(i + k);
                if ((cc & 0xC0) != 0x80) {
                    return 0;
                }
                cp = (cp << 6) | (cc & 0x3FU);
            }

            // Reject overlong encodings, code points past U+10FFFF, and surrogates,
            // all of which are legal byte patterns but forbidden Unicode.
            const bool overlong = (len == 2 && cp < 0x80) ||
                                  (len == 3 && cp < 0x800) ||
                                  (len == 4 && cp < 0x10000);
            if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                return 0;
            }

            return len;
        }
    }

    std::string sanitize_utf8(const std::string_view s) {
        std::string out;
        out.reserve(s.size());

        std::size_t i = 0;
        while (i < s.size()) {
            const std::size_t len = decode_utf8(s, i);
            // A failure consumes exactly one byte, so one bad lead byte cannot make
            // the scan swallow the rest of the string.
            if (len == 0) {
                out.append(kReplacement);
                ++i;
                continue;
            }

            out.append(s.substr(i, len));
            i += len;
        }

        return out;
    }

    std::string json_escape(const std::string_view s) {
        const std::string clean = sanitize_utf8(s);
        std::string out;
        out.reserve(clean.size() + 8);

        for (const unsigned char c: clean) {
            switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
            }
        }

        return out;
    }

    std::string xml_escape(const std::string_view s) {
        const std::string clean = sanitize_utf8(s);
        std::string out;
        out.reserve(clean.size());

        for (const char c: clean) {
            switch (c) {
                case '&':
                    out += "&amp;";
                    break;
                case '<':
                    out += "&lt;";
                    break;
                case '>':
                    out += "&gt;";
                    break;
                case '"':
                    out += "&quot;";
                    break;
                case '\'':
                    out += "&apos;";
                    break;
                default:
                    out.push_back(c);
                    break;
            }
        }

        return out;
    }

    std::string utf8_truncate(std::string in, const std::size_t max_bytes) {
        if (in.size() <= max_bytes) {
            return in;
        }

        std::size_t cut = max_bytes;

        // Walk back over continuation bytes so the cut lands on a code point start
        // instead of leaving a partial sequence at the end.
        while (cut > 0 && (static_cast<unsigned char>(in[cut]) & 0xC0) == 0x80) {
            --cut;
        }

        in.resize(cut);

        // Prefer a word break when the cut falls in the second half of the budget;
        // earlier whitespace would waste too much of the allowance.
        if (const auto sp = in.find_last_of(" \t\n\r");
            sp != std::string::npos && sp > max_bytes / 2) {
            in.resize(sp);
        }

        // Drop trailing whitespace exposed by the word-boundary trim.
        while (!in.empty() &&
               (in.back() == ' ' || in.back() == '\t' || in.back() == '\n' ||
                in.back() == '\r')) {
            in.pop_back();
        }

        in += "…";
        return in;
    }
}
