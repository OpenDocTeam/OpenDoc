#pragma once

#include "opendoc/parser/ast.hpp"

#include <map>
#include <string>
#include <string_view>

namespace opendoc::parser {
    // Knobs that shape anchor generation and code block markup.
    struct HtmlRenderOptions {
        bool heading_anchors = true;
        std::string code_class_prefix = "language-";
    };

    // Visitor that serializes an AST into HTML, appending into an internal buffer.
    class HtmlRenderer final : public AstVisitor {
    public:
        // Stores the options used for anchors and code block classes.
        explicit HtmlRenderer(HtmlRenderOptions opts = {});

        // Renders a whole document, resetting per-document slug and footnote state.
        std::string render(const DocumentNode &doc);

        // Emits the body blocks followed by the collected footnote list.
        void visit(DocumentNode &n) override;

        // Wraps the paragraph in <p> tags.
        void visit(ParagraphNode &n) override;

        // Emits <hN> with a deduplicated id and a self link for levels 2 to 4.
        void visit(HeadingNode &n) override;

        // Emits the text with HTML entities escaped.
        void visit(TextNode &n) override;

        // Wraps the content in <em> tags.
        void visit(EmphasisNode &n) override;

        // Wraps the content in <strong> tags.
        void visit(StrongNode &n) override;

        // Wraps the content in <del> tags.
        void visit(StrikethroughNode &n) override;

        // Emits escaped code inside a <code> element.
        void visit(CodeSpanNode &n) override;

        // Emits a self-closing <br />.
        void visit(LineBreakNode &n) override;

        // Emits an <a> with escaped href and optional title.
        void visit(LinkNode &n) override;

        // Emits an <img> with escaped src, alt, and optional title.
        void visit(ImageNode &n) override;

        // Emits <pre><code> with the syntax highlighted and escaped body.
        void visit(CodeBlockNode &n) override;

        // Wraps the quoted blocks in <blockquote> tags.
        void visit(BlockQuoteNode &n) override;

        // Emits <ol> with a start attribute, or <ul> for bullets.
        void visit(ListNode &n) override;

        // Emits <li>, plus a disabled checkbox when the item is a task.
        void visit(ListItemNode &n) override;

        // Emits a self-closing <hr />.
        void visit(ThematicBreakNode &n) override;

        // Copies the raw inline HTML through without escaping.
        void visit(HtmlInlineNode &n) override;

        // Copies the raw block HTML through, followed by a newline.
        void visit(HtmlBlockNode &n) override;

        // Emits <table> with the first row as <thead> and the rest as <tbody>.
        void visit(TableNode &n) override;

        // Row wrapper used while emitting table markup.
        void visit(TableRowNode &n) override;

        // Emits a <td> cell with any column alignment style applied.
        void visit(TableCellNode &n) override;

        // Numbers the reference on first use and links it to the footnote section.
        void visit(FootnoteRefNode &n) override;

        // Emits MathJax-style delimiters around escaped TeX.
        void visit(MathNode &n) override;

        // Wraps the terms and descriptions in <dl> tags.
        void visit(DefinitionListNode &n) override;

        // Emits a <dt> term.
        void visit(DefinitionTermNode &n) override;

        // Emits a <dd> description.
        void visit(DefinitionDescNode &n) override;

        // Escapes the five HTML-significant characters for text and attributes.
        [[nodiscard]] static std::string escape_html(std::string_view input);

        // Lowercases and hyphenates heading text into a URL-safe anchor slug.
        [[nodiscard]] static std::string slugify(std::string_view input);

        // Visits each child in order, appending to the output buffer.
        void render_children(const NodeList &list);

    private:
        HtmlRenderOptions opts_;
        std::string out_;
        std::map<std::string, int> slug_counts_;
        std::map<std::string, int> footnote_numbers_;
        const DocumentNode *doc_ = nullptr;
        int next_footnote_ = 1;
        bool in_heading_ = false;

        // Emits the footnote section, ordered by first reference rather than document order.
        void render_footnotes();
    };
}
