#pragma once

#include "opendoc/parser/ast.hpp"
#include "opendoc/parser/markdown_parser.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace opendoc::parser::detail {
    // Reference definition map: normalized label -> (destination, title).
    using RefMap = std::map<std::string, std::pair<std::string, std::string> >;

    // Inline stage that scans a text run and builds inline AST nodes.
    class InlineParser {
    public:
        // Keeps a reference to the shared options; the ref map is injected later.
        explicit InlineParser(const ParseOptions &opts) : opts_(opts) {
        }

        // Parses one text run into inline nodes; constructs that contain other
        // constructs re-enter this up to kMaxInlineDepth levels deep.
        NodeList parse(std::string_view text);

        // Points at the block parser's map; it must outlive this parser.
        void set_refs(const RefMap *refs) { refs_ = refs; }

    private:
        const ParseOptions &opts_;
        const RefMap *refs_ = nullptr;

        // Nesting level of the run currently in parse(). Link labels and emphasis
        // content are re-parsed recursively, so without a cap an input like
        // "[[...[x](u)...](u)](u)" recurses once per level until the stack
        // overflows (0xC00000FD).
        static constexpr std::size_t kMaxInlineDepth = 64;
        std::size_t depth_ = 0;

        // Core scanner: buffers literal text and dispatches on special characters.
        void parse_into(NodeList &out, std::string_view text);

        // Appends to a trailing text node when possible so adjacent literals merge.
        static void append_text(NodeList &out, std::string_view chunk);

        // Tries link or image syntax at i, returning the index just past it or i
        // unchanged when the syntax does not parse, which the caller treats as a miss.
        std::size_t try_link_image(NodeList &out, std::string_view text, std::size_t i);

        // Tries a [^label] footnote reference at i, else returns i.
        std::size_t try_footnote_ref(NodeList &out, std::string_view text, std::size_t i);

        // Tries $..$ or $$..$$ math at i, else returns i.
        std::size_t try_math(NodeList &out, std::string_view text, std::size_t i);

        // Tries an autolink, bare email, or raw HTML tag at i, else returns i.
        std::size_t try_autolink_html(NodeList &out, std::string_view text, std::size_t i) const;

        // Tries emphasis or strikethrough runs at i, else returns i.
        std::size_t try_delims(NodeList &out, std::string &buf, std::string_view text,
                               std::size_t i) const;

        // Builds a fresh sub-parser sharing options, refs, and the current depth,
        // for recursively parsing the content found inside a delimiter pair.
        [[nodiscard]] InlineParser make_sub() const {
            InlineParser sub(opts_);
            sub.set_refs(refs_);
            sub.depth_ = depth_;
            return sub;
        }
    };

    // Lowercases and collapses whitespace so reference labels match case-insensitively.
    [[nodiscard]] std::string normalize_ref_key(std::string_view label);
}
