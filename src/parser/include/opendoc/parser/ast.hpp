#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opendoc::parser {
    // Double-dispatch target implemented by every renderer and extractor.
    class AstVisitor;

    // Base of all AST nodes; children are owned through unique_ptr.
    class Node {
    public:
        // Virtual so nodes delete correctly through a Node pointer.
        virtual ~Node() = default;

        // Dispatches to the matching AstVisitor::visit overload for this node type.
        virtual void accept(AstVisitor &visitor) = 0;

        // Stable lowercase tag such as "heading", used by tests and debug dumps.
        [[nodiscard]] virtual const char *type_name() const noexcept = 0;
    };

    // Owning handle to a single node.
    using NodePtr = std::unique_ptr<Node>;
    // Owned sequence of sibling nodes in document order.
    using NodeList = std::vector<NodePtr>;

    // One pure overload per node type; concrete visitors must implement all of them.
    class AstVisitor {
    public:
        virtual ~AstVisitor() = default;

        // Handles the root, which carries children plus collected footnotes.
        virtual void visit(struct DocumentNode &) = 0;

        // Handles a block of inline content.
        virtual void visit(struct ParagraphNode &) = 0;

        // Handles an ATX or setext heading.
        virtual void visit(struct HeadingNode &) = 0;

        // Handles a run of literal text.
        virtual void visit(struct TextNode &) = 0;

        // Handles single-delimiter emphasis such as *em* or _em_.
        virtual void visit(struct EmphasisNode &) = 0;

        // Handles double-delimiter emphasis such as **strong**.
        virtual void visit(struct StrongNode &) = 0;

        // Handles a GFM ~~strikethrough~~ span.
        virtual void visit(struct StrikethroughNode &) = 0;

        // Handles a backtick-delimited code span.
        virtual void visit(struct CodeSpanNode &) = 0;

        // Handles a soft or hard break between lines.
        virtual void visit(struct LineBreakNode &) = 0;

        // Handles an inline or reference link.
        virtual void visit(struct LinkNode &) = 0;

        // Handles an image with alt text and an optional title.
        virtual void visit(struct ImageNode &) = 0;

        // Handles a fenced or indented code block.
        virtual void visit(struct CodeBlockNode &) = 0;

        // Handles a quote containing nested block content.
        virtual void visit(struct BlockQuoteNode &) = 0;

        // Handles an ordered or unordered list.
        virtual void visit(struct ListNode &) = 0;

        // Handles one item, possibly carrying a task checkbox state.
        virtual void visit(struct ListItemNode &) = 0;

        // Handles a thematic break (horizontal rule).
        virtual void visit(struct ThematicBreakNode &) = 0;

        // Handles raw HTML in inline position.
        virtual void visit(struct HtmlInlineNode &) = 0;

        // Handles raw HTML in block position.
        virtual void visit(struct HtmlBlockNode &) = 0;

        // Handles a GFM table including column alignments.
        virtual void visit(struct TableNode &) = 0;

        // Handles one row of a table; the first row is the header.
        virtual void visit(struct TableRowNode &) = 0;

        // Handles one cell's inline content.
        virtual void visit(struct TableCellNode &) = 0;

        // Handles a [^label] marker that points at a footnote definition.
        virtual void visit(struct FootnoteRefNode &) = 0;

        // Handles inline or display TeX math.
        virtual void visit(struct MathNode &) = 0;

        // Handles a definition list made of terms and descriptions.
        virtual void visit(struct DefinitionListNode &) = 0;

        // Handles a single term of a definition list.
        virtual void visit(struct DefinitionTermNode &) = 0;

        // Handles a single description body of a definition list.
        virtual void visit(struct DefinitionDescNode &) = 0;
    };

    // A [^label]: definition captured by the block pre-pass, stored on the document.
    struct FootnoteDefinition {
        std::string label;
        NodeList inlines;
    };

    // Root node: top-level blocks plus the footnote definitions found while parsing.
    struct DocumentNode : Node {
        NodeList children;
        std::vector<FootnoteDefinition> footnotes;

        // Forwards to the visitor's DocumentNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "document" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "document"; }
    };

    // Block of inline children, the default container for prose.
    struct ParagraphNode : Node {
        NodeList inlines;

        // Forwards to the visitor's ParagraphNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "paragraph" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "paragraph"; }
    };

    // Heading with a 1..6 level, inline content, and a slug anchor id.
    struct HeadingNode : Node {
        int level = 1;
        NodeList inlines;
        std::string anchor_id;

        // Forwards to the visitor's HeadingNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "heading" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "heading"; }
    };

    // Leaf holding a literal text run with escapes already resolved.
    struct TextNode : Node {
        std::string text;

        // Forwards to the visitor's TextNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "text" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "text"; }
    };

    // Inline wrapper for single-delimiter emphasis.
    struct EmphasisNode : Node {
        NodeList inlines;

        // Forwards to the visitor's EmphasisNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "emphasis" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "emphasis"; }
    };

    // Inline wrapper for double-delimiter strong emphasis.
    struct StrongNode : Node {
        NodeList inlines;

        // Forwards to the visitor's StrongNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "strong" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "strong"; }
    };

    // Inline wrapper for a GFM strikethrough span.
    struct StrikethroughNode : Node {
        NodeList inlines;

        // Forwards to the visitor's StrikethroughNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "strikethrough" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "strikethrough"; }
    };

    // Leaf holding the verbatim contents of a backtick code span.
    struct CodeSpanNode : Node {
        std::string code;

        // Forwards to the visitor's CodeSpanNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "code_span" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "code_span"; }
    };

    // Break between lines; hard breaks render as <br /> while soft ones do not.
    struct LineBreakNode : Node {
        bool hard = false;

        // Forwards to the visitor's LineBreakNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "line_break" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "line_break"; }
    };

    // Link with destination, optional title, and the label as inline children.
    struct LinkNode : Node {
        std::string href;
        std::string title;
        NodeList inlines;

        // Forwards to the visitor's LinkNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "link" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "link"; }
    };

    // Image with source, alt text, and optional title; alt is a plain string.
    struct ImageNode : Node {
        std::string src;
        std::string alt;
        std::string title;

        // Forwards to the visitor's ImageNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "image" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "image"; }
    };

    // Code block with an info-string language; fenced distinguishes ``` from indented.
    struct CodeBlockNode : Node {
        std::string language;
        std::string code;
        bool fenced = true;

        // Forwards to the visitor's CodeBlockNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "code_block" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "code_block"; }
    };

    // Container for the blocks parsed out of >-prefixed lines.
    struct BlockQuoteNode : Node {
        NodeList children;

        // Forwards to the visitor's BlockQuoteNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "block_quote" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "block_quote"; }
    };

    // List with ordered flag, starting number, tightness, and item children.
    struct ListNode : Node {
        bool ordered = false;
        int start = 1;
        bool tight = true;
        NodeList items;

        // Forwards to the visitor's ListNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "list" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "list"; }
    };

    // One list item; task is unset, unchecked, or checked for GFM task lists.
    struct ListItemNode : Node {
        NodeList children;
        std::optional<bool> task;

        // Forwards to the visitor's ListItemNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "list_item" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "list_item"; }
    };

    // Leaf representing a horizontal rule with no content.
    struct ThematicBreakNode : Node {
        // Forwards to the visitor's ThematicBreakNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "thematic_break" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "thematic_break"; }
    };

    // Leaf holding raw HTML in inline position, copied through verbatim.
    struct HtmlInlineNode : Node {
        std::string html;

        // Forwards to the visitor's HtmlInlineNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "html_inline" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "html_inline"; }
    };

    // Leaf holding raw HTML in block position, copied through verbatim.
    struct HtmlBlockNode : Node {
        std::string html;

        // Forwards to the visitor's HtmlBlockNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "html_block" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "html_block"; }
    };

    // Table with per-column alignment taken from the delimiter row and row children.
    struct TableNode : Node {
        // Column alignment decoded from the colons around the delimiter dashes.
        enum class Align { None, Left, Right, Center };

        std::vector<Align> column_alignments;
        NodeList rows;

        // Forwards to the visitor's TableNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "table" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "table"; }
    };

    // One table row; cell children are TableCellNode instances.
    struct TableRowNode : Node {
        NodeList cells;

        // Forwards to the visitor's TableRowNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "table_row" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "table_row"; }
    };

    // One table cell holding inline children.
    struct TableCellNode : Node {
        NodeList inlines;

        // Forwards to the visitor's TableCellNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "table_cell" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "table_cell"; }
    };

    // Marker linking a reference to the footnote definition with the same label.
    struct FootnoteRefNode : Node {
        std::string label;

        // Forwards to the visitor's FootnoteRefNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "footnote_ref" type tag.
        [[nodiscard]] const char *type_name() const noexcept override {
            return "footnote_ref";
        }
    };

    // TeX source; display selects between block ($$) and inline ($) rendering.
    struct MathNode : Node {
        std::string tex;
        bool display = false;

        // Forwards to the visitor's MathNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "math" type tag.
        [[nodiscard]] const char *type_name() const noexcept override { return "math"; }
    };

    // Definition list holding alternating terms and descriptions.
    struct DefinitionListNode : Node {
        NodeList items;

        // Forwards to the visitor's DefinitionListNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "definition_list" type tag.
        [[nodiscard]] const char *type_name() const noexcept override {
            return "definition_list";
        }
    };

    // Term line of a definition list, stored as inline children.
    struct DefinitionTermNode : Node {
        NodeList inlines;

        // Forwards to the visitor's DefinitionTermNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "definition_term" type tag.
        [[nodiscard]] const char *type_name() const noexcept override {
            return "definition_term";
        }
    };

    // Description body of a definition list; may contain nested blocks.
    struct DefinitionDescNode : Node {
        NodeList children;

        // Forwards to the visitor's DefinitionDescNode overload.
        void accept(AstVisitor &v) override { v.visit(*this); }

        // Returns the "definition_desc" type tag.
        [[nodiscard]] const char *type_name() const noexcept override {
            return "definition_desc";
        }
    };
}
