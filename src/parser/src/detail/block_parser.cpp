#include "block_parser.hpp"

#include "text_utils.hpp"

#include <algorithm>
#include <cctype>

namespace opendoc::parser::detail {
    bool BlockParser::is_hr(const std::string_view line) {
        const auto t = trim(line);

        if (t.size() < 3) {
            return false;
        }

        const char c = t[0];

        if (c != '-' && c != '*' && c != '_') {
            return false;
        }

        int count = 0;
        for (const char ch: t) {
            if (ch == c) {
                ++count;
            } else if (!std::isspace(static_cast<unsigned char>(ch))) {
                return false;
            }
        }

        return count >= 3;
    }

    std::optional<std::pair<int, std::string_view> > BlockParser::atx_heading(
        const std::string_view line) {
        int hashes = 0;
        while (hashes < static_cast<int>(line.size()) && line[hashes] == '#') {
            ++hashes;
        }

        if (hashes < 1 || hashes > 6) {
            return std::nullopt;
        }

        if (static_cast<int>(line.size()) > hashes &&
            !std::isspace(static_cast<unsigned char>(line[hashes]))) {
            return std::nullopt;
        }

        std::string_view rest = ltrim(line.substr(hashes));
        rest = rtrim(rest);

        if (!rest.empty() && rest.back() == '#') {
            std::size_t end = rest.size();
            while (end > 0 && rest[end - 1] == '#') {
                --end;
            }

            if (end == 0) {
                rest = {};
            } else if (std::isspace(static_cast<unsigned char>(rest[end - 1]))) {
                rest = rtrim(rest.substr(0, end));
            }
        }

        return std::make_pair(hashes, rest);
    }

    bool BlockParser::is_fence(const std::string_view line, char &ch, int &len,
                               std::string &info) {
        int indent = 0;
        while (indent < static_cast<int>(line.size()) && line[indent] == ' ') {
            ++indent;
        }

        if (indent >= 4) {
            return false;
        }

        const std::string_view t = line.substr(indent);

        if (t.empty()) {
            return false;
        }

        ch = t[0];

        if (ch != '`' && ch != '~') {
            return false;
        }

        int run = 0;
        while (run < static_cast<int>(t.size()) && t[run] == ch) {
            ++run;
        }

        if (run < 3) {
            return false;
        }

        len = run;
        info = std::string(trim(t.substr(run)));

        if (ch == '`' && info.find('`') != std::string::npos) {
            return false;
        }

        return true;
    }

    std::optional<std::pair<char, std::string_view> > BlockParser::list_marker(
        const std::string_view line) {
        int indent = 0;
        while (indent < static_cast<int>(line.size()) && line[indent] == ' ') {
            ++indent;
        }

        if (indent >= 4) {
            return std::nullopt;
        }

        const std::string_view t = line.substr(indent);

        if (t.empty()) {
            return std::nullopt;
        }

        if (t[0] == '-' || t[0] == '+' || t[0] == '*') {
            if (t.size() == 1) {
                return std::make_pair(t[0], std::string_view{});
            }
            if (std::isspace(static_cast<unsigned char>(t[1]))) {
                return std::make_pair(t[0], ltrim(t.substr(1)));
            }

            return std::nullopt;
        }

        std::size_t i = 0;
        while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
            ++i;
        }

        if (i == 0 || i > 9) {
            return std::nullopt;
        }
        if (i >= t.size() || (t[i] != '.' && t[i] != ')')) {
            return std::nullopt;
        }
        if (i + 1 < t.size() && !std::isspace(static_cast<unsigned char>(t[i + 1]))) {
            return std::nullopt;
        }

        return std::make_pair(t[i], ltrim(t.substr(i + 1)));
    }

    int BlockParser::marker_content_indent(const std::string_view line, const bool ordered) {
        int indent = 0;
        while (indent < static_cast<int>(line.size()) && line[indent] == ' ') {
            ++indent;
        }

        auto p = static_cast<std::size_t>(indent);

        if (ordered) {
            while (p < line.size() && std::isdigit(static_cast<unsigned char>(line[p]))) {
                ++p;
            }
            if (p < line.size() && (line[p] == '.' || line[p] == ')')) {
                ++p;
            }
        } else {
            if (p < line.size()) {
                ++p;
            }
        }

        std::size_t sp = 0;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) {
            ++sp;
            ++p;
        }

        if (sp == 0) {
            return indent + static_cast<int>(p - static_cast<std::size_t>(indent)) + 1;
        }
        if (sp > 4) {
            const std::size_t marker_end = p - sp;

            return static_cast<int>(marker_end) + 1;
        }

        return static_cast<int>(p);
    }

    bool BlockParser::is_block_start(const std::string_view line, const ParseOptions &opts) {
        if (is_hr(line)) {
            return true;
        }
        if (atx_heading(line).has_value()) {
            return true;
        }

        int len;
        std::string info;
        if (char ch; is_fence(line, ch, len, info)) {
            return true;
        }
        if (const auto t = trim(line); !t.empty() && t.front() == '>') {
            return true;
        }
        if (const auto m = list_marker(line); m && !m->second.empty()) {
            return true;
        }

        (void) opts;

        return false;
    }

    DocumentNode BlockParser::parse(const std::string_view markdown) {
        lines_ = split_lines(markdown);
        pos_ = 0;

        if (!prepass_done_) {
            // Reference and footnote pre-pass: scan every line once and pull out
            // [label]: url definitions and [^label]: footnote bodies before any
            // block is built, because inline parsing needs the map to already be
            // complete. Fenced and indented code are skipped so bracketed text in
            // examples never turns into a definition, and consumed lines are
            // dropped from the line list the real block parse will walk.
            std::vector<std::string_view> kept;
            std::vector<std::pair<std::string, std::string> > fn_defs;
            kept.reserve(lines_.size());

            char fence_char = 0;
            int fence_len = 0;

            for (std::size_t li = 0; li < lines_.size(); ++li) {
                auto line = lines_[li];

                if (fence_char != 0) {
                    std::size_t close_indent = 0;
                    while (close_indent < line.size() && line[close_indent] == ' ') {
                        ++close_indent;
                    }

                    bool closes = false;
                    if (close_indent < line.size() && line[close_indent] == fence_char) {
                        std::size_t run = 0;
                        while (close_indent + run < line.size() &&
                               line[close_indent + run] == fence_char) {
                            ++run;
                        }
                        closes = run >= static_cast<std::size_t>(fence_len) &&
                                 trim(line.substr(close_indent + run)).empty();
                    }

                    if (closes) {
                        fence_char = 0;
                        fence_len = 0;
                    }

                    kept.push_back(line);
                    continue;
                }

                char ch;
                std::string info;
                if (int len; is_fence(line, ch, len, info)) {
                    fence_char = ch;
                    fence_len = len;
                    kept.push_back(line);
                    continue;
                }

                std::size_t columns = 0;
                for (const char c: line) {
                    if (c == ' ') {
                        ++columns;
                    } else if (c == '\t') {
                        columns += 4 - (columns % 4);
                    } else {
                        break;
                    }
                    if (columns >= 4) {
                        break;
                    }
                }
                if (columns >= 4) {
                    kept.push_back(line);
                    continue;
                }

                auto t = trim(line);
                bool consumed = false;

                if (!t.empty() && t.front() == '[') {
                    if (const auto rb = t.find(']');
                        rb != std::string_view::npos && rb + 1 < t.size() && t[rb + 1] == ':') {
                        std::string label(t.substr(1, rb - 1));

                        if (!label.empty() && label.front() == '^') {
                            std::string content(trim(t.substr(rb + 2)));

                            while (li + 1 < lines_.size()) {
                                const auto nxt = lines_[li + 1];
                                const auto nt = trim(nxt);

                                if (nt.empty() || nt.front() == '[' ||
                                    is_block_start(nxt, opts_)) {
                                    break;
                                }

                                content.push_back('\n');
                                content.append(nt);
                                ++li;
                            }

                            fn_defs.emplace_back(label.substr(1), std::move(content));
                            consumed = true;
                        } else {
                            const std::string key = normalize_ref_key(label);
                            std::string rest(trim(t.substr(rb + 2)));
                            std::string url;

                            if (!rest.empty() && rest.front() == '<') {
                                if (const auto gt = rest.find('>'); gt != std::string::npos) {
                                    url = rest.substr(1, gt - 1);
                                    rest = trim(std::string_view(rest).substr(gt + 1));
                                }
                            } else {
                                std::size_t sp = 0;
                                while (sp < rest.size() &&
                                       !std::isspace(static_cast<unsigned char>(rest[sp]))) {
                                    ++sp;
                                }

                                url = rest.substr(0, sp);
                                rest = trim(std::string_view(rest).substr(sp));
                            }

                            std::string title;

                            if (rest.size() >= 2 &&
                                ((rest.front() == '"' && rest.back() == '"') ||
                                 (rest.front() == '\'' && rest.back() == '\''))) {
                                title = rest.substr(1, rest.size() - 2);
                            }

                            if (!url.empty() && key.size() <= 999) {
                                refs_[key] = {url, title};
                                consumed = true;
                            }
                        }
                    }
                }

                if (!consumed) {
                    kept.push_back(line);
                }
            }

            lines_ = std::move(kept);
            footnotes_ = std::move(fn_defs);
            inline_parser_.set_refs(&refs_);
        }

        DocumentNode doc;
        parse_blocks(doc.children);

        for (auto &[label, content]: footnotes_) {
            FootnoteDefinition def;
            def.label = label;
            def.inlines = inline_parser_.parse(content);
            doc.footnotes.push_back(std::move(def));
        }

        footnotes_.clear();

        return doc;
    }

    void BlockParser::parse_blocks(NodeList &out) {
        // Fixed dispatch order, first match wins: headings and rules come before
        // lists so "---" reads as a thematic break rather than a bullet, and the
        // option-gated table and HTML checks sit after the unambiguous constructs.
        while (pos_ < lines_.size()) {
            const std::string_view line = lines_[pos_];

            if (is_blank(line)) {
                ++pos_;
                continue;
            }

            if (try_heading(out)) {
                continue;
            }
            if (try_thematic_break(out)) {
                continue;
            }
            if (try_fenced_code(out)) {
                continue;
            }
            if (try_blockquote(out)) {
                continue;
            }
            if (try_list(out)) {
                continue;
            }
            if (opts_.gfm_tables && try_table(out)) {
                continue;
            }
            if (opts_.raw_html && try_html_block(out)) {
                continue;
            }
            if (try_definition_list(out)) {
                continue;
            }
            if (line.starts_with("    ") || line.starts_with('\t')) {
                parse_indented_code(out);
                continue;
            }

            parse_paragraph(out);
        }
    }

    bool BlockParser::try_heading(NodeList &out) {
        const auto h = atx_heading(lines_[pos_]);

        if (!h) {
            return false;
        }

        auto node = std::make_unique<HeadingNode>();
        node->level = h->first;
        node->inlines = inline_parser_.parse(h->second);
        out.push_back(std::move(node));
        ++pos_;

        return true;
    }

    bool BlockParser::try_thematic_break(NodeList &out) {
        if (!is_hr(lines_[pos_])) {
            return false;
        }

        out.push_back(std::make_unique<ThematicBreakNode>());
        ++pos_;

        return true;
    }

    bool BlockParser::try_fenced_code(NodeList &out) {
        char ch;
        int len;
        std::string info;

        if (!is_fence(lines_[pos_], ch, len, info)) {
            return false;
        }

        std::string lang;
        {
            const auto sp = info.find_first_of(" \t");
            lang = info.substr(0, sp);
        }

        ++pos_;

        std::string code;
        bool first = true;

        while (pos_ < lines_.size()) {
            const std::string_view t = lines_[pos_];
            int indent = 0;
            while (indent < static_cast<int>(t.size()) && t[indent] == ' ') {
                ++indent;
            }

            std::string_view tt = t.substr(indent);
            bool close = false;

            if (!tt.empty() && tt[0] == ch) {
                int run = 0;
                while (run < static_cast<int>(tt.size()) && tt[run] == ch) {
                    ++run;
                }

                if (run >= len && trim(tt.substr(run)).empty()) {
                    close = true;
                }
            }

            if (close) {
                ++pos_;
                break;
            }

            if (!first) {
                code.push_back('\n');
            }

            code.append(lines_[pos_]);
            first = false;
            ++pos_;
        }

        auto node = std::make_unique<CodeBlockNode>();
        node->language = std::move(lang);
        node->code = std::move(code);
        node->fenced = true;
        out.push_back(std::move(node));

        return true;
    }

    bool BlockParser::try_blockquote(NodeList &out) {
        if (const std::string_view ct = trim(lines_[pos_]);
            ct.empty() || ct.front() != '>') {
            return false;
        }

        std::string content;
        bool first = true;

        while (pos_ < lines_.size()) {
            std::string_view cur = lines_[pos_];

            if (auto t = trim(cur); !t.empty() && t.front() == '>') {
                std::string_view after = t.substr(1);

                if (!after.empty() && after.front() == ' ') {
                    after.remove_prefix(1);
                }

                if (!first) {
                    content.push_back('\n');
                }

                content.append(after);
                first = false;
                ++pos_;

                continue;
            }

            // Lazy continuation: an unmarked line still joins the quote, unless it
            // opens a construct that must start its own block.
            if (!first && !is_blank(content) && !is_blank(cur)) {
                if (!is_block_start(cur, opts_)) {
                    content.push_back('\n');
                    content.append(cur);
                    ++pos_;

                    continue;
                }
            }

            break;
        }

        // Quote content is re-parsed from scratch by a nested parser, so it sees
        // the full block grammar; the shared refs are handed over because this
        // parser's pre-pass already collected them.
        BlockParser sub(opts_);
        sub.set_refs(refs_);
        DocumentNode inner = sub.parse(content);

        auto node = std::make_unique<BlockQuoteNode>();

        for (auto &child: inner.children) {
            node->children.push_back(std::move(child));
        }

        out.push_back(std::move(node));

        return true;
    }

    void BlockParser::parse_indented_code(NodeList &out) {
        std::string code;
        bool first = true;

        while (pos_ < lines_.size()) {
            const std::string_view line = lines_[pos_];

            if (is_blank(line)) {
                // Blank lines are swallowed only while another indented line
                // follows, so the block never trails off into pure whitespace.
                std::size_t peek = pos_ + 1;
                while (peek < lines_.size() && is_blank(lines_[peek])) {
                    ++peek;
                }

                if (peek < lines_.size() &&
                    (lines_[peek].starts_with("    ") || lines_[peek].starts_with('\t'))) {
                    if (!first) {
                        code.push_back('\n');
                    }

                    ++pos_;
                    continue;
                }

                break;
            }

            if (!(line.starts_with("    ") || line.starts_with('\t'))) {
                break;
            }

            if (!first) {
                code.push_back('\n');
            }

            if (line.starts_with('\t')) {
                code.append(line.substr(1));
            } else {
                code.append(line.substr(4));
            }

            first = false;
            ++pos_;
        }

        auto node = std::make_unique<CodeBlockNode>();
        node->fenced = false;
        node->code = std::move(code);
        out.push_back(std::move(node));
    }

    void BlockParser::parse_paragraph(NodeList &out) {
        std::string content;
        bool first = true;

        while (pos_ < lines_.size()) {
            const std::string_view line = lines_[pos_];

            if (is_blank(line)) {
                break;
            }

            const auto t = trim(line);

            if (!first) {
                // Setext underlines only apply one line after paragraph text:
                // a run of '=' makes the accumulated lines an h1, '-' makes an h2.
                if (!t.empty() && t.front() == '=' &&
                    std::ranges::all_of(t, [](const char c) {
                        return c == '=' || std::isspace(static_cast<unsigned char>(c));
                    })) {
                    auto heading = std::make_unique<HeadingNode>();
                    heading->level = 1;
                    heading->inlines = inline_parser_.parse(trim(content));
                    out.push_back(std::move(heading));
                    ++pos_;

                    return;
                }

                if (!t.empty() && t.front() == '-' &&
                    std::ranges::all_of(t, [](const char c) {
                        return c == '-' || std::isspace(static_cast<unsigned char>(c));
                    })) {
                    auto heading = std::make_unique<HeadingNode>();
                    heading->level = 2;
                    heading->inlines = inline_parser_.parse(trim(content));
                    out.push_back(std::move(heading));
                    ++pos_;

                    return;
                }

                if (is_block_start(line, opts_)) {
                    break;
                }
                if (!t.empty() && t.front() == '>') {
                    break;
                }
                if (const auto m = list_marker(line); m && !m->second.empty()) {
                    break;
                }
            }

            if (!first) {
                content.push_back('\n');
            }

            content.append(line);
            first = false;
            ++pos_;
        }

        if (content.empty()) {
            return;
        }

        auto para = std::make_unique<ParagraphNode>();
        para->inlines = inline_parser_.parse(content);
        out.push_back(std::move(para));
    }
}
