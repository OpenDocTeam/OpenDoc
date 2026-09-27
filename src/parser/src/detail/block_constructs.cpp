#include "block_parser.hpp"

#include "text_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>

namespace opendoc::parser::detail {
    // True for a definition marker line: at most three leading spaces, then ':',
    // then whitespace or end of line, so plain prose with a colon is unaffected.
    static bool is_definition_line(const std::string_view line) {
        std::size_t i = 0;

        while (i < line.size() && line[i] == ' ') {
            ++i;
        }

        if (i > 3 || i >= line.size() || line[i] != ':') {
            return false;
        }

        return i + 1 == line.size() || line[i + 1] == ' ' || line[i + 1] == '\t';
    }

    // Text following the ':' of a definition marker line, or empty when there is none.
    static std::string_view definition_body(const std::string_view line) {
        std::size_t i = 0;

        while (i < line.size() && line[i] == ' ') {
            ++i;
        }

        return i + 1 <= line.size() ? line.substr(i + 1) : std::string_view{};
    }

    bool BlockParser::try_definition_list(NodeList &out) {
        if (pos_ + 1 >= lines_.size()) {
            return false;
        }

        if (is_blank(lines_[pos_]) || is_block_start(lines_[pos_], opts_)) {
            return false;
        }

        if (!is_definition_line(lines_[pos_ + 1])) {
            return false;
        }

        auto dl = std::make_unique<DefinitionListNode>();
        // Nested parser for description bodies, seeded with the shared refs so
        // links defined earlier still resolve inside them.
        auto sub = BlockParser(opts_);
        sub.set_refs(refs_);

        while (pos_ < lines_.size()) {
            if (is_blank(lines_[pos_])) {
                // A blank line ends the list unless the next non-blank pair is
                // itself term plus definition, which lets lists span paragraphs.
                std::size_t p = pos_;

                while (p < lines_.size() && is_blank(lines_[p])) {
                    ++p;
                }

                if (p + 1 < lines_.size() && !is_block_start(lines_[p], opts_) &&
                    !is_definition_line(lines_[p]) && is_definition_line(lines_[p + 1])) {
                    pos_ = p;
                    continue;
                }

                break;
            }

            if (is_definition_line(lines_[pos_])) {
                while (pos_ < lines_.size() && is_definition_line(lines_[pos_])) {
                    std::string content(trim(definition_body(lines_[pos_])));
                    ++pos_;

                    // Continuation lines extend the description until a blank,
                    // another ':' marker, or a line that opens a new block.
                    while (pos_ < lines_.size() && !is_blank(lines_[pos_]) &&
                           !is_definition_line(lines_[pos_]) &&
                           !is_block_start(lines_[pos_], opts_)) {
                        content.push_back('\n');
                        content.append(lines_[pos_]);
                        ++pos_;
                    }

                    auto dd = std::make_unique<DefinitionDescNode>();

                    for (DocumentNode inner = sub.parse(content); auto &child: inner.children) {
                        dd->children.push_back(std::move(child));
                    }

                    dl->items.push_back(std::move(dd));
                }

                continue;
            }

            if (is_block_start(lines_[pos_], opts_)) {
                break;
            }

            // Anything left that is not a marker or a block start is a term line.
            auto dt = std::make_unique<DefinitionTermNode>();
            dt->inlines = inline_parser_.parse(trim(lines_[pos_]));
            dl->items.push_back(std::move(dt));
            ++pos_;
        }

        if (dl->items.empty()) {
            return false;
        }

        out.push_back(std::move(dl));

        return true;
    }

    bool BlockParser::try_list(NodeList &out) {
        auto marker = list_marker(lines_[pos_]);

        if (!marker) {
            return false;
        }

        const char first_marker = marker->first;
        std::string_view first_line_trimmed = trim(lines_[pos_]);
        const bool is_ordered =
                !first_line_trimmed.empty() &&
                std::isdigit(static_cast<unsigned char>(first_line_trimmed.front())) != 0;
        int start_num = 1;

        if (is_ordered) {
            auto t = trim(lines_[pos_]);
            std::size_t d = 0;
            while (d < t.size() && std::isdigit(static_cast<unsigned char>(t[d]))) {
                ++d;
            }

            start_num = std::atoi(std::string(t.substr(0, d)).c_str());

            if (start_num <= 0) {
                start_num = 1;
            }
        }

        auto list = std::make_unique<ListNode>();
        list->ordered = is_ordered;
        list->start = start_num;
        list->tight = true;

        // Tracks blank lines around items: any of them makes the list loose,
        // which is what stops paragraphs from being unwrapped later.
        bool blank_between = false;
        bool pending_blank = false;

        while (pos_ < lines_.size()) {
            const std::string_view cur = lines_[pos_];

            if (is_blank(cur)) {
                // Peek past the blank: a sibling marker of the same kind keeps the
                // list going, as does content still indented under the last item.
                if (pos_ + 1 < lines_.size() && !is_blank(lines_[pos_ + 1])) {
                    const std::string_view next = lines_[pos_ + 1];
                    int ni = 0;
                    while (ni < static_cast<int>(next.size()) && next[ni] == ' ') {
                        ++ni;
                    }

                    if (auto nm = list_marker(next); nm &&
                                                     same_list_marker(nm->first, first_marker) && ni < 4) {
                        blank_between = true;
                        pending_blank = true;
                        list->tight = false;
                        ++pos_;
                        continue;
                    }

                    if (const int content_indent = marker_content_indent(lines_[pos_], is_ordered);
                        ni >= content_indent) {
                        pending_blank = true;
                        list->tight = false;
                        ++pos_;
                        continue;
                    }
                }

                break;
            }

            int indent = 0;
            while (indent < static_cast<int>(cur.size()) && cur[indent] == ' ') {
                ++indent;
            }

            auto m = list_marker(cur);
            const bool new_item =
                    m && indent < 4 && same_list_marker(m->first, first_marker);

            if (new_item) {
                if (blank_between || pending_blank) {
                    list->tight = false;
                }

                const int content_indent = marker_content_indent(cur, is_ordered);
                // Lines indented past this column belong to the item; shallower
                // lines either start a sibling item or end the list.
                std::string item_content(m->second);
                ++pos_;

                bool first_line = item_content.empty();

                while (pos_ < lines_.size()) {
                    const std::string_view cl = lines_[pos_];

                    if (is_blank(cl)) {
                        if (pos_ + 1 >= lines_.size()) {
                            break;
                        }

                        const std::string_view nxt = lines_[pos_ + 1];

                        if (is_blank(nxt)) {
                            int ni = 0;
                            while (ni < static_cast<int>(nxt.size()) && nxt[ni] == ' ') {
                                ++ni;
                            }

                            if (ni >= content_indent) {
                                item_content.push_back('\n');
                                list->tight = false;
                                ++pos_;
                                continue;
                            }

                            break;
                        }

                        int ni = 0;
                        while (ni < static_cast<int>(nxt.size()) && nxt[ni] == ' ') {
                            ++ni;
                        }

                        if (auto nm = list_marker(nxt); nm &&
                                                        same_list_marker(nm->first, first_marker) && ni < 4) {
                            break;
                        }

                        if (ni >= content_indent) {
                            item_content.push_back('\n');
                            list->tight = false;
                            ++pos_;
                            continue;
                        }

                        break;
                    }

                    int ci = 0;
                    while (ci < static_cast<int>(cl.size()) && cl[ci] == ' ') {
                        ++ci;
                    }

                    if (auto nm = list_marker(cl); nm && ci < 4 &&
                                                   same_list_marker(nm->first, first_marker)) {
                        break;
                    }

                    if (ci < content_indent) {
                        if (ci >= 1 && !is_block_start(cl, opts_)) {
                            item_content.push_back('\n');
                            item_content.append(cl);
                            ++pos_;
                            continue;
                        }

                        break;
                    }

                    if (!first_line) {
                        item_content.push_back('\n');
                    }

                    item_content.append(cl.substr(
                        std::min(static_cast<std::size_t>(content_indent), cl.size())));
                    first_line = false;
                    ++pos_;
                }

                std::optional<bool> task;

                // A "[ ]" or "[x]" right after the marker becomes the task state
                // and is stripped from the content so it is not rendered as text.
                if (opts_.gfm_task_lists) {
                    std::string_view ic(item_content);
                    auto tt = trim(ic);

                    if (tt.size() >= 3 && tt.front() == '[' &&
                        (tt[1] == ' ' || tt[1] == 'x' || tt[1] == 'X') && tt[2] == ']') {
                        task = (tt[1] != ' ');
                        const auto indent_len =
                                static_cast<std::size_t>(tt.data() - ic.data());

                        std::string rebuilt;
                        rebuilt.append(ic.substr(0, indent_len));
                        std::string_view rest = tt.substr(3);

                        if (!rest.empty() && rest.front() == ' ') {
                            rest.remove_prefix(1);
                        }

                        rebuilt.append(rest);
                        item_content = std::move(rebuilt);
                    }
                }

                auto item = std::make_unique<ListItemNode>();
                item->task = task;

                // The item body is re-parsed by a nested parser so it can hold
                // arbitrary blocks, not just a single paragraph.
                BlockParser sub(opts_);
                sub.set_refs(refs_);

                for (DocumentNode inner = sub.parse(item_content);
                     auto &child: inner.children) {
                    item->children.push_back(std::move(child));
                }

                list->items.push_back(std::move(item));
                pending_blank = false;
                continue;
            }

            break;
        }

        // Tight lists keep item content bare, so paragraphs written by the nested
        // parse are dissolved and their inlines lifted straight into the item.
        if (list->tight) {
            for (auto &item_ptr: list->items) {
                auto *item = dynamic_cast<ListItemNode *>(item_ptr.get());
                NodeList unwrapped;

                for (auto &ch: item->children) {
                    if (auto *p = dynamic_cast<ParagraphNode *>(ch.get())) {
                        for (auto &inl: p->inlines) {
                            unwrapped.push_back(std::move(inl));
                        }
                    } else {
                        unwrapped.push_back(std::move(ch));
                    }
                }

                item->children = std::move(unwrapped);
            }
        }

        out.push_back(std::move(list));

        return true;
    }

    bool BlockParser::try_table(NodeList &out) {
        if (pos_ + 1 >= lines_.size()) {
            return false;
        }

        const std::string_view header = lines_[pos_];
        const std::string_view delim = lines_[pos_ + 1];

        if (header.find('|') == std::string_view::npos) {
            return false;
        }

        // Splits a row on unescaped pipes, dropping optional outer pipes and
        // turning \| back into a literal pipe.
        auto split_row = [](const std::string_view row) {
            std::vector<std::string> cells;
            auto r = row;

            if (!r.empty() && r.front() == '|') {
                r.remove_prefix(1);
            }

            if (!r.empty() && r.back() == '|' && (r.size() < 2 || r[r.size() - 2] != '\\')) {
                r.remove_suffix(1);
            }

            std::string cur;

            for (std::size_t i = 0; i < r.size(); ++i) {
                if (r[i] == '\\' && i + 1 < r.size() && r[i + 1] == '|') {
                    cur.push_back('|');
                    ++i;
                    continue;
                }

                if (r[i] == '|') {
                    cells.emplace_back(trim(cur));
                    cur.clear();
                    continue;
                }

                cur.push_back(r[i]);
            }

            cells.emplace_back(trim(cur));

            return cells;
        };

        const auto delim_cells = split_row(delim);

        if (delim_cells.empty()) {
            return false;
        }

        // Each delimiter cell must be dashes with optional ':' colons; anything
        // else means this is not a delimiter row and there is no table.
        for (const auto &c: delim_cells) {
            if (c.empty()) {
                return false;
            }

            std::size_t i = 0;
            if (c.front() == ':') {
                ++i;
            }
            std::size_t end = c.size();
            if (end > 0 && c.back() == ':') {
                --end;
            }
            if (i >= end) {
                return false;
            }

            bool all_dash = true;

            for (; i < end; ++i) {
                if (c[i] != '-') {
                    all_dash = false;
                    break;
                }
            }

            if (!all_dash) {
                return false;
            }
        }

        std::vector<TableNode::Align> aligns;
        aligns.reserve(delim_cells.size());

        // Colons on either side of the dashes select the alignment for that column.
        for (const auto &c: delim_cells) {
            const bool left = c.front() == ':';
            if (const bool right = c.back() == ':'; left && right) {
                aligns.push_back(TableNode::Align::Center);
            } else if (right) {
                aligns.push_back(TableNode::Align::Right);
            } else if (left) {
                aligns.push_back(TableNode::Align::Left);
            } else {
                aligns.push_back(TableNode::Align::None);
            }
        }

        auto header_cells = split_row(header);

        // Header and delimiter column counts should match; pad or trim rather
        // than reject the table, since extra header cells are common in the wild.
        if (header_cells.size() != aligns.size()) {
            if (header_cells.size() > aligns.size()) {
                header_cells.resize(aligns.size());
            } else {
                aligns.resize(header_cells.size(), TableNode::Align::None);
            }
        }

        auto table = std::make_unique<TableNode>();
        table->column_alignments = aligns;

        // Builds a row node, parsing every cell's text through the inline parser.
        auto make_row = [&](const std::vector<std::string> &cells) {
            auto row = std::make_unique<TableRowNode>();

            for (const auto &c: cells) {
                auto cell = std::make_unique<TableCellNode>();
                cell->inlines = inline_parser_.parse(c);
                row->cells.push_back(std::move(cell));
            }

            return row;
        };

        table->rows.push_back(make_row(header_cells));
        pos_ += 2;

        // Body rows run until a blank line, a line with no pipe, or a line that
        // opens some other block; short rows are padded, long rows truncated.
        while (pos_ < lines_.size() && !is_blank(lines_[pos_]) &&
               lines_[pos_].find('|') != std::string_view::npos &&
               !is_block_start(lines_[pos_], opts_)) {
            auto cells = split_row(lines_[pos_]);

            if (cells.size() < aligns.size()) {
                cells.resize(aligns.size(), "");
            }
            if (cells.size() > aligns.size()) {
                cells.resize(aligns.size());
            }

            table->rows.push_back(make_row(cells));
            ++pos_;
        }

        out.push_back(std::move(table));

        return true;
    }

    bool BlockParser::try_html_block(NodeList &out) {
        const std::string_view line = trim(lines_[pos_]);

        if (line.empty() || line.front() != '<') {
            return false;
        }

        // Block-level tags that may open an HTML block when they lead the line;
        // kept as prefixes so "<div>" and "<div class=...>" both match.
        static const char *tags[] = {
            "<div", "<p>", "<p ", "<table", "<ul", "<ol",
            "<pre", "<blockquote", "<h1", "<h2", "<h3", "<h4",
            "<h5", "<h6", "<section", "<article", "<header", "<footer",
            "<nav", "<aside", "<details", "<!--", "<!doctype", "<html",
            "<head", "<body", "<script", "<style", "<figure", "<figcaption",
            "<dl", "<dt", "<dd", "<form", "<fieldset", "<address",
            "<hr", "<br", "<img", "<input", "<video", "<audio",
            "<picture", "<main", "<center", "<font", "<table>"
        };

        bool match = false;
        std::string lower_prefix;
        const std::size_t probe = std::min<std::size_t>(line.size(), 24);
        lower_prefix.assign(line.substr(0, probe));

        for (auto &ch: lower_prefix) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        // A tag matches when the lowercased prefix equals it or continues with
        // whitespace or '>', so "<hr" does not match the word "<hours>".
        for (const char *t: tags) {
            const std::string tag(t);

            if (lower_prefix == tag ||
                (lower_prefix.size() > tag.size() &&
                 lower_prefix.compare(0, tag.size(), tag) == 0 &&
                 (std::isspace(static_cast<unsigned char>(lower_prefix[tag.size()])) ||
                  lower_prefix[tag.size()] == '>'))) {
                match = true;
                break;
            }

            if (tag.size() > 1 && tag.back() == '>' && lower_prefix.rfind(tag, 0) == 0) {
                match = true;
                break;
            }
        }

        if (!match) {
            return false;
        }

        std::string tag_name;
        {
            std::size_t k = 1;
            while (k < line.size() &&
                   std::isalpha(static_cast<unsigned char>(line[k])) != 0) {
                tag_name.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(line[k]))));
                ++k;
            }
        }

        std::string html;
        bool first = true;

        // Joins lines with newlines while suppressing a leading separator.
        auto append_line = [&](const std::string_view l) {
            if (!first) {
                html.push_back('\n');
            }
            html.append(l);
            first = false;
        };

        if (!tag_name.empty() && line.find("</") == std::string_view::npos &&
            (tag_name == "details" || tag_name == "div" || tag_name == "section" ||
             tag_name == "article" || tag_name == "figure" || tag_name == "main" ||
             tag_name == "header" || tag_name == "footer" || tag_name == "nav" ||
             tag_name == "aside" || tag_name == "form" || tag_name == "table" ||
             tag_name == "ul" || tag_name == "ol" || tag_name == "dl" ||
             tag_name == "blockquote" || tag_name == "pre" || tag_name == "script" ||
             tag_name == "style")) {
            // Scan forward for the matching close tag, bounded to 2000 lines so a
            // single unclosed tag cannot swallow the rest of the document.
            const std::string close_tag = "</" + tag_name + ">";
            std::size_t end = pos_;

            while (end < lines_.size() && end - pos_ < 2000) {
                std::string probe_line(lines_[end]);
                for (auto &ch: probe_line) {
                    ch = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch)));
                }

                if (probe_line.find(close_tag) != std::string::npos) {
                    break;
                }
                ++end;
            }

            if (end < lines_.size()) {
                for (; pos_ <= end; ++pos_) {
                    append_line(lines_[pos_]);
                }

                auto node = std::make_unique<HtmlBlockNode>();
                node->html = std::move(html);
                out.push_back(std::move(node));

                return true;
            }
        }

        // No closing tag found (or a void element): the block ends at the first
        // blank line instead.
        while (pos_ < lines_.size() && !is_blank(lines_[pos_])) {
            append_line(lines_[pos_]);
            ++pos_;
        }

        auto node = std::make_unique<HtmlBlockNode>();
        node->html = std::move(html);
        out.push_back(std::move(node));

        return true;
    }
}
