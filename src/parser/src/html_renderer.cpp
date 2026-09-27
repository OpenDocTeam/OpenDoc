#include "opendoc/parser/html_renderer.hpp"

#include "opendoc/parser/syntax_highlight.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <utility>
#include <vector>

namespace opendoc::parser {
    HtmlRenderer::HtmlRenderer(HtmlRenderOptions opts) : opts_(std::move(opts)) {
    }

    std::string HtmlRenderer::escape_html(const std::string_view input) {
        std::string out;
        out.reserve(input.size());
        for (const char c: input) {
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
                    out += "&#39;";
                    break;
                default:
                    out.push_back(c);
                    break;
            }
        }
        return out;
    }

    std::string HtmlRenderer::slugify(const std::string_view input) {
        std::string out;
        out.reserve(input.size());
        bool prev_dash = true;
        for (const unsigned char uc: input) {
            const char c = static_cast<char>(uc);
            if (std::isalnum(uc)) {
                out.push_back(static_cast<char>(std::tolower(uc)));
                prev_dash = false;
            } else if (!prev_dash && (c == ' ' || c == '-' || c == '_')) {
                out.push_back('-');
                prev_dash = true;
            }
        }
        while (!out.empty() && out.back() == '-') {
            out.pop_back();
        }

        return out;
    }

    std::string HtmlRenderer::render(const DocumentNode &doc) {
        out_.clear();
        slug_counts_.clear();
        footnote_numbers_.clear();
        next_footnote_ = 1;
        doc_ = &doc;

        auto &mutable_doc = const_cast<DocumentNode &>(doc);
        mutable_doc.accept(*this);
        doc_ = nullptr;
        return out_;
    }

    void HtmlRenderer::render_children(const NodeList &list) {
        for (auto &child: list) {
            child->accept(*this);
        }
    }

    void HtmlRenderer::visit(DocumentNode &n) {
        render_children(n.children);
        render_footnotes();
    }

    void HtmlRenderer::visit(ParagraphNode &n) {
        out_ += "<p>";
        render_children(n.inlines);
        out_ += "</p>";
    }

    void HtmlRenderer::visit(HeadingNode &n) {
        out_ += "<h";
        out_ += std::to_string(n.level);

        // The slug is derived once from the visible text, then repeated slugs are
        // disambiguated with -1, -2 suffixes so every heading id stays unique.
        if (opts_.heading_anchors) {
            if (n.anchor_id.empty()) {
                std::string text;

                for (auto &inl: n.inlines) {
                    if (const auto *t = dynamic_cast<TextNode *>(inl.get())) {
                        text += t->text;
                    } else if (const auto *c = dynamic_cast<CodeSpanNode *>(inl.get())) {
                        text += c->code;
                    }
                }

                n.anchor_id = slugify(text);
            }

            std::string id = n.anchor_id;
            if (id.empty()) {
                id = "section";
            }

            if (const auto it = slug_counts_.find(id); it != slug_counts_.end()) {
                ++it->second;
                id = id + "-" + std::to_string(it->second);
            } else {
                slug_counts_[id] = 0;
            }

            n.anchor_id = id;
            out_ += " id=\"";
            out_ += escape_html(id);
            out_ += '"';
        }

        out_ += '>';
        render_children(n.inlines);

        if (opts_.heading_anchors && n.level >= 2 && n.level <= 4) {
            std::string id = n.anchor_id;
            if (id.empty()) {
                id = "section";
            }

            out_ += R"(<a aria-label="Link to this section" class="heading-anchor" href="#)";
            out_ += escape_html(id);
            out_ += "\">#</a>";
        }

        out_ += "</h";
        out_ += std::to_string(n.level);
        out_ += '>';
    }

    void HtmlRenderer::visit(TextNode &n) { out_ += escape_html(n.text); }

    void HtmlRenderer::visit(EmphasisNode &n) {
        out_ += "<em>";
        render_children(n.inlines);
        out_ += "</em>";
    }

    void HtmlRenderer::visit(StrongNode &n) {
        out_ += "<strong>";
        render_children(n.inlines);
        out_ += "</strong>";
    }

    void HtmlRenderer::visit(StrikethroughNode &n) {
        out_ += "<del>";
        render_children(n.inlines);
        out_ += "</del>";
    }

    void HtmlRenderer::visit(CodeSpanNode &n) {
        out_ += "<code>";
        out_ += escape_html(n.code);
        out_ += "</code>";
    }

    void HtmlRenderer::visit(LineBreakNode &) { out_ += "<br />"; }

    void HtmlRenderer::visit(LinkNode &n) {
        out_ += "<a href=\"";
        out_ += escape_html(n.href);
        if (!n.title.empty()) {
            out_ += "\" title=\"";
            out_ += escape_html(n.title);
        }
        out_ += "\">";
        render_children(n.inlines);
        out_ += "</a>";
    }

    void HtmlRenderer::visit(ImageNode &n) {
        out_ += "<img src=\"";
        out_ += escape_html(n.src);
        out_ += "\" alt=\"";
        out_ += escape_html(n.alt);
        if (!n.title.empty()) {
            out_ += "\" title=\"";
            out_ += escape_html(n.title);
        }
        out_ += "\" />";
    }

    void HtmlRenderer::visit(CodeBlockNode &n) {
        out_ += "<pre><code";
        if (!n.language.empty()) {
            out_ += " class=\"";
            out_ += escape_html(opts_.code_class_prefix);
            out_ += escape_html(n.language);
            out_ += '"';
        }
        out_ += '>';

        out_ += highlight_code(n.code, n.language);
        if (!n.code.empty() && n.code.back() != '\n') {
            out_ += '\n';
        }
        out_ += "</code></pre>";
    }

    void HtmlRenderer::visit(BlockQuoteNode &n) {
        out_ += "<blockquote>\n";
        render_children(n.children);
        out_ += "\n</blockquote>";
    }

    void HtmlRenderer::visit(ListNode &n) {
        if (n.ordered) {
            out_ += "<ol";
            if (n.start != 1) {
                out_ += " start=\"";
                out_ += std::to_string(n.start);
                out_ += '"';
            }
            out_ += ">\n";
        } else {
            out_ += "<ul>\n";
        }

        render_children(n.items);
        out_ += n.ordered ? "</ol>" : "</ul>";
    }

    void HtmlRenderer::visit(ListItemNode &n) {
        out_ += "<li";
        if (n.task.has_value()) {
            out_ += " class=\"task-list-item\"";
        }
        out_ += '>';

        if (n.task.has_value()) {
            out_ += "<input type=\"checkbox\" disabled";
            if (*n.task) {
                out_ += " checked";
            }
            out_ += " /> ";
        }

        render_children(n.children);
        out_ += "</li>";
    }

    void HtmlRenderer::visit(ThematicBreakNode &) { out_ += "<hr />"; }

    void HtmlRenderer::visit(HtmlInlineNode &n) { out_ += n.html; }

    void HtmlRenderer::visit(HtmlBlockNode &n) {
        out_ += n.html;
        out_ += '\n';
    }

    void HtmlRenderer::visit(TableNode &n) {
        out_ += "<table>\n";

        if (!n.rows.empty()) {
            // Row zero is the header, so it becomes <th> cells carrying the
            // alignment styles read from the delimiter row.
            out_ += "<thead>\n<tr>";

            if (const auto *header_row = dynamic_cast<TableRowNode *>(n.rows.front().get())) {
                std::size_t col = 0;

                for (auto &cell_ptr: header_row->cells) {
                    const auto *cell = dynamic_cast<TableCellNode *>(cell_ptr.get());

                    if (!cell) {
                        continue;
                    }

                    out_ += "<th";
                    if (col < n.column_alignments.size()) {
                        switch (n.column_alignments[col]) {
                            case TableNode::Align::Left:
                                out_ += " style=\"text-align: left\"";
                                break;
                            case TableNode::Align::Right:
                                out_ += " style=\"text-align: right\"";
                                break;
                            case TableNode::Align::Center:
                                out_ += " style=\"text-align: center\"";
                                break;
                            case TableNode::Align::None:
                                break;
                        }
                    }
                    out_ += '>';
                    render_children(cell->inlines);
                    out_ += "</th>";
                    ++col;
                }
            }

            out_ += "</tr>\n</thead>\n";
        }

        if (n.rows.size() > 1) {
            out_ += "<tbody>\n";

            for (std::size_t r = 1; r < n.rows.size(); ++r) {
                const auto *row = dynamic_cast<TableRowNode *>(n.rows[r].get());

                if (!row) {
                    continue;
                }

                out_ += "<tr>";
                std::size_t col = 0;

                for (auto &cell_ptr: row->cells) {
                    const auto *cell = dynamic_cast<TableCellNode *>(cell_ptr.get());

                    if (!cell) {
                        continue;
                    }

                    out_ += "<td";
                    if (col < n.column_alignments.size()) {
                        switch (n.column_alignments[col]) {
                            case TableNode::Align::Left:
                                out_ += " style=\"text-align: left\"";
                                break;
                            case TableNode::Align::Right:
                                out_ += " style=\"text-align: right\"";
                                break;
                            case TableNode::Align::Center:
                                out_ += " style=\"text-align: center\"";
                                break;
                            case TableNode::Align::None:
                                break;
                        }
                    }
                    out_ += '>';
                    render_children(cell->inlines);
                    out_ += "</td>";
                    ++col;
                }

                out_ += "</tr>\n";
            }

            out_ += "</tbody>\n";
        }

        out_ += "</table>";
    }

    void HtmlRenderer::visit(TableRowNode &n) { render_children(n.cells); }

    void HtmlRenderer::visit(TableCellNode &n) {
        out_ += "<td>";
        render_children(n.inlines);
        out_ += "</td>";
    }

    void HtmlRenderer::visit(FootnoteRefNode &n) {
        auto it = footnote_numbers_.find(n.label);

        // The first reference fixes the number, so repeated markers agree.
        if (it == footnote_numbers_.end()) {
            it = footnote_numbers_.emplace(n.label, next_footnote_++).first;
        }

        const std::string num = std::to_string(it->second);

        out_ += "<sup class=\"footnote-ref\"><a href=\"#fn-" + escape_html(n.label) +
                "\" id=\"fnref-" + escape_html(n.label) + "\">[" + num + "]</a></sup>";
    }

    void HtmlRenderer::visit(MathNode &n) {
        if (n.display) {
            out_ += "<div class=\"math math-display\">\\[" + escape_html(n.tex) + "\\]</div>";
        } else {
            out_ += "<span class=\"math math-inline\">\\(" + escape_html(n.tex) + "\\)</span>";
        }
    }

    void HtmlRenderer::visit(DefinitionListNode &n) {
        out_ += "<dl class=\"definition-list\">\n";
        render_children(n.items);
        out_ += "</dl>";
    }

    void HtmlRenderer::visit(DefinitionTermNode &n) {
        out_ += "<dt>";
        render_children(n.inlines);
        out_ += "</dt>\n";
    }

    void HtmlRenderer::visit(DefinitionDescNode &n) {
        out_ += "<dd>";
        render_children(n.children);
        out_ += "</dd>\n";
    }

    void HtmlRenderer::render_footnotes() {
        if (!doc_ || doc_->footnotes.empty() || footnote_numbers_.empty()) {
            return;
        }

        std::vector<std::pair<int, const FootnoteDefinition *> > ordered;

        for (const auto &def: doc_->footnotes) {
            if (const auto it = footnote_numbers_.find(def.label);
                it != footnote_numbers_.end()) {
                ordered.emplace_back(it->second, &def);
            }
        }

        if (ordered.empty()) {
            return;
        }

        // Only definitions that were actually referenced are listed, sorted by
        // first-reference number rather than by their position in the document.
        std::ranges::sort(ordered, {}, [](const auto &p) { return p.first; });

        out_ += "<section class=\"footnotes\">\n<ol class=\"footnotes-list\">\n";

        for (const auto &[num, def]: ordered) {
            const std::string label = escape_html(def->label);

            out_ += "<li class=\"footnote-item\" id=\"fn-" + label + "\">";
            render_children(def->inlines);
            out_ += " <a class=\"footnote-backref\" href=\"#fnref-" + label +
                    "\" aria-label=\"Back to reference " + std::to_string(num) +
                    "\">&#8617;</a></li>\n";
        }

        out_ += "</ol>\n</section>";
    }
}
