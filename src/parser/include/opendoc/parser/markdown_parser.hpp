#pragma once

#include "opendoc/parser/ast.hpp"
#include "opendoc/parser/front_matter.hpp"

#include <string_view>

namespace opendoc::parser {
    // Feature switches for GFM extensions and raw HTML passthrough.
    struct ParseOptions {
        bool gfm_tables = true;
        bool gfm_task_lists = true;
        bool gfm_strikethrough = true;
        bool raw_html = true;
    };

    // One full parse: YAML metadata plus the document tree.
    struct ParsedPage {
        FrontMatter front_matter;
        DocumentNode document;
    };

    // Entry point that turns Markdown text into an AST.
    class MarkdownParser {
    public:
        // Freezes the option set used by every parse this object performs.
        explicit MarkdownParser(ParseOptions opts = {});

        // Splits off front matter first, then parses the remaining body.
        [[nodiscard]] ParsedPage parse(std::string_view markdown) const;

        // Parses a body directly, without any front matter handling.
        [[nodiscard]] DocumentNode parse_body(std::string_view markdown) const;

    private:
        ParseOptions opts_;
    };
}
