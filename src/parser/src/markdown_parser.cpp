#include "opendoc/parser/markdown_parser.hpp"

#include "detail/block_parser.hpp"

namespace opendoc::parser {
    MarkdownParser::MarkdownParser(const ParseOptions opts) : opts_(opts) {
    }

    ParsedPage MarkdownParser::parse(const std::string_view markdown) const {
        ParsedPage page;
        auto [front, body] = split_front_matter(markdown);

        page.front_matter = std::move(front);
        page.document = parse_body(body);
        return page;
    }

    DocumentNode MarkdownParser::parse_body(const std::string_view markdown) const {
        detail::BlockParser parser(opts_);
        return parser.parse(markdown);
    }
}
