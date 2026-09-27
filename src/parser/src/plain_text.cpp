#include "opendoc/parser/plain_text.hpp"

#include <utility>

namespace opendoc::parser {
    namespace {
        // The visitor API is non-const while extraction never mutates the tree, so
        // const nodes are cast back rather than duplicated.
        Node &mutable_node(const Node &n) {
            return const_cast<Node &>(n);
        }
    }

    std::string PlainTextExtractor::from_document(const DocumentNode &doc) {
        PlainTextExtractor extractor;
        extractor.visit(const_cast<DocumentNode &>(doc));
        return std::move(extractor.out_);
    }

    std::string PlainTextExtractor::from_inlines(const NodeList &nodes) {
        PlainTextExtractor extractor;
        extractor.walk(nodes);
        return std::move(extractor.out_);
    }

    std::string PlainTextExtractor::from_node(const Node &node) {
        PlainTextExtractor extractor;
        mutable_node(node).accept(extractor);
        return std::move(extractor.out_);
    }

    void PlainTextExtractor::append_node(const Node &node, std::string &out) {
        PlainTextExtractor extractor;
        mutable_node(node).accept(extractor);
        out += extractor.out_;
    }

    void PlainTextExtractor::walk(const NodeList &nodes) {
        for (const auto &n: nodes) {
            mutable_node(*n).accept(*this);
        }
    }

    void PlainTextExtractor::end_block() {
        if (!out_.empty() && out_.back() != ' ') {
            out_.push_back(' ');
        }
    }

    void PlainTextExtractor::visit(DocumentNode &n) { walk(n.children); }

    void PlainTextExtractor::visit(ParagraphNode &n) {
        walk(n.inlines);
        end_block();
    }

    void PlainTextExtractor::visit(HeadingNode &n) {
        walk(n.inlines);
        end_block();
    }

    void PlainTextExtractor::visit(TextNode &n) { out_ += n.text; }

    void PlainTextExtractor::visit(EmphasisNode &n) { walk(n.inlines); }

    void PlainTextExtractor::visit(StrongNode &n) { walk(n.inlines); }

    void PlainTextExtractor::visit(StrikethroughNode &n) { walk(n.inlines); }

    void PlainTextExtractor::visit(CodeSpanNode &n) { out_ += n.code; }

    void PlainTextExtractor::visit(LineBreakNode &) { end_block(); }

    void PlainTextExtractor::visit(LinkNode &n) { walk(n.inlines); }

    void PlainTextExtractor::visit(ImageNode &n) { out_ += n.alt; }

    void PlainTextExtractor::visit(CodeBlockNode &n) {
        out_ += n.code;
        end_block();
    }

    void PlainTextExtractor::visit(BlockQuoteNode &n) { walk(n.children); }

    void PlainTextExtractor::visit(ListNode &n) { walk(n.items); }

    void PlainTextExtractor::visit(ListItemNode &n) {
        walk(n.children);
        end_block();
    }

    void PlainTextExtractor::visit(ThematicBreakNode &) { end_block(); }

    void PlainTextExtractor::visit(HtmlInlineNode &) {
    }

    void PlainTextExtractor::visit(HtmlBlockNode &) { end_block(); }

    void PlainTextExtractor::visit(TableNode &n) { walk(n.rows); }

    void PlainTextExtractor::visit(TableRowNode &n) {
        walk(n.cells);
        end_block();
    }

    void PlainTextExtractor::visit(TableCellNode &n) {
        walk(n.inlines);
        end_block();
    }

    void PlainTextExtractor::visit(FootnoteRefNode &) {
    }

    void PlainTextExtractor::visit(MathNode &n) { out_ += n.tex; }

    void PlainTextExtractor::visit(DefinitionListNode &n) { walk(n.items); }

    void PlainTextExtractor::visit(DefinitionTermNode &n) {
        walk(n.inlines);
        end_block();
    }

    void PlainTextExtractor::visit(DefinitionDescNode &n) {
        walk(n.children);
        end_block();
    }
}
