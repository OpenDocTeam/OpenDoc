#pragma once

#include <string>
#include <string_view>

namespace opendoc::parser {
    // Highlights a code block, returning HTML with the input already escaped;
    // unknown or empty languages fall back to plain escaped text.
    [[nodiscard]] std::string highlight_code(std::string_view code,
                                             std::string_view lang);

    // Escapes the HTML metacharacters so a fragment is safe to inline in markup.
    [[nodiscard]] std::string highlight_escape(std::string_view input);
}
