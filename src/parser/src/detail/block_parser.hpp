#pragma once

#include "inline_parser.hpp"
#include "opendoc/parser/ast.hpp"
#include "opendoc/parser/markdown_parser.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opendoc::parser::detail {
    // Line-oriented block stage: owns the line cursor, the shared reference map,
    // and a nested inline parser used for every text run it produces.
    class BlockParser {
    public:
        // Wires the inline sub-parser to this parser's reference map so both stages
        // see the same definitions.
        explicit BlockParser(const ParseOptions &opts)
            : opts_(opts), inline_parser_(opts) {
            inline_parser_.set_refs(&refs_);
        }

        // Parses all lines into a document; the reference/footnote pre-pass runs on
        // the first call only.
        DocumentNode parse(std::string_view markdown);

        // Seeds the reference map for a nested parser and marks the pre-pass as
        // already done, since the parent has effectively run it.
        void set_refs(std::map<std::string, std::pair<std::string, std::string> > refs) {
            refs_ = std::move(refs);
            inline_parser_.set_refs(&refs_);
            prepass_done_ = true;
        }

    private:
        const ParseOptions &opts_;
        InlineParser inline_parser_;
        std::vector<std::string_view> lines_;
        std::size_t pos_ = 0;
        RefMap refs_;
        std::vector<std::pair<std::string, std::string> > footnotes_;
        bool prepass_done_ = false;

        // Dispatches the current line to the first block construct that claims it.
        void parse_blocks(NodeList &out);

        // Consumes an ATX heading line, if that is what the cursor sits on.
        bool try_heading(NodeList &out);

        // Consumes a ---, *** or ___ rule line.
        bool try_thematic_break(NodeList &out);

        // Consumes a fenced code block up to a matching closing fence.
        bool try_fenced_code(NodeList &out);

        // Consumes a run of >-prefixed lines and parses them recursively.
        bool try_blockquote(NodeList &out);

        // Consumes a whole list, splitting items by marker type and indentation.
        bool try_list(NodeList &out);

        // Consumes a header row, a delimiter row, and the body rows that follow.
        bool try_table(NodeList &out);

        // Consumes raw HTML, preferring a matched closing tag when one is known.
        bool try_html_block(NodeList &out);

        // Consumes term and description pairs into a definition list.
        bool try_definition_list(NodeList &out);

        // Consumes 4-space or tab indented lines as an unfenced code block.
        void parse_indented_code(NodeList &out);

        // Consumes consecutive lines until a blank line or a new block start.
        void parse_paragraph(NodeList &out);

        // True for a line of at least three identical -, * or _ markers.
        static bool is_hr(std::string_view line);

        // Level and text of a "# heading", or nullopt when the line is not one.
        static std::optional<std::pair<int, std::string_view> > atx_heading(
            std::string_view line);

        // Detects an opening ``` or ~~~ fence, reporting its character, run length,
        // and info string through the out parameters.
        static bool is_fence(std::string_view line, char &ch, int &len, std::string &info);

        // Marker character and remaining text of a list item line, or nullopt.
        static std::optional<std::pair<char, std::string_view> > list_marker(
            std::string_view line);

        // Column at which item content begins, used to decide whether following
        // lines belong to the item.
        static int marker_content_indent(std::string_view line, bool ordered);

        // True when the line opens a construct that can interrupt a paragraph.
        static bool is_block_start(std::string_view line, const ParseOptions &opts);
    };
}
