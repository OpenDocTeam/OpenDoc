#pragma once

#include "opendoc/parser/ast.hpp"

#include <string>

namespace opendoc::parser {
    // Visitor that flattens a tree into plain text, dropping markup and URLs;
    // used for search indexing, previews, and page descriptions.
    class PlainTextExtractor final : public AstVisitor {
    public:
        // All readable text of a whole document, blocks separated by spaces.
        [[nodiscard]] static std::string from_document(const DocumentNode &doc);

        // Text of a bare list of inline nodes, without any block framing.
        [[nodiscard]] static std::string from_inlines(const NodeList &nodes);

        // Text of a single node of any type.
        [[nodiscard]] static std::string from_node(const Node &node);

        // Appends a node's text to an existing buffer instead of returning a new string.
        static void append_node(const Node &node, std::string &out);

        // Emits the text of every top-level block.
        void visit(DocumentNode &n) override;

        // Emits the paragraph text followed by a block separator.
        void visit(ParagraphNode &n) override;

        // Emits the heading text followed by a block separator.
        void visit(HeadingNode &n) override;

        // Copies the literal text verbatim.
        void visit(TextNode &n) override;

        // Contributes only the inner text of the emphasis.
        void visit(EmphasisNode &n) override;

        // Contributes only the inner text of the strong span.
        void visit(StrongNode &n) override;

        // Contributes only the inner text of the struck span.
        void visit(StrikethroughNode &n) override;

        // Contributes the literal code of the span.
        void visit(CodeSpanNode &n) override;

        // Separates the surrounding text with a space.
        void visit(LineBreakNode &n) override;

        // Keeps the link label but drops the destination and title.
        void visit(LinkNode &n) override;

        // Uses the alt text as the image's textual stand-in.
        void visit(ImageNode &n) override;

        // Emits the code body followed by a block separator.
        void visit(CodeBlockNode &n) override;

        // Recurses into the quoted blocks; the quote markers themselves are dropped.
        void visit(BlockQuoteNode &n) override;

        // Recurses into the items without adding bullets or numbers.
        void visit(ListNode &n) override;

        // Emits the item text followed by a block separator.
        void visit(ListItemNode &n) override;

        // Adds a block separator; the rule itself carries no text.
        void visit(ThematicBreakNode &n) override;

        // Skipped entirely, since raw markup is not readable text.
        void visit(HtmlInlineNode &n) override;

        // Skips the markup but still separates the block from its neighbours.
        void visit(HtmlBlockNode &n) override;

        // Recurses into the rows.
        void visit(TableNode &n) override;

        // Emits the row's cells followed by a block separator.
        void visit(TableRowNode &n) override;

        // Emits the cell content followed by a block separator.
        void visit(TableCellNode &n) override;

        // Reference markers contribute no text of their own.
        void visit(FootnoteRefNode &n) override;

        // Emits the TeX source so formulas stay searchable.
        void visit(MathNode &n) override;

        // Recurses into the terms and descriptions.
        void visit(DefinitionListNode &n) override;

        // Emits the term text followed by a block separator.
        void visit(DefinitionTermNode &n) override;

        // Emits the description text followed by a block separator.
        void visit(DefinitionDescNode &n) override;

    private:
        std::string out_;

        // Visits each child in order, appending to the shared buffer.
        void walk(const NodeList &nodes);

        // Appends one space after a block unless the text already ends with one.
        void end_block();
    };
}
