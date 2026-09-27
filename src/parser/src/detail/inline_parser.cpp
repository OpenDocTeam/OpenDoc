#include "inline_parser.hpp"

#include "text_utils.hpp"

#include <cctype>

namespace opendoc::parser::detail {
    std::string normalize_ref_key(const std::string_view label) {
        std::string key;
        key.reserve(label.size());
        bool prev_space = false;

        for (const char c: label) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!prev_space) {
                    key.push_back(' ');
                }
                prev_space = true;
            } else {
                key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                prev_space = false;
            }
        }

        return key;
    }

    void InlineParser::append_text(NodeList &out, const std::string_view chunk) {
        if (chunk.empty()) {
            return;
        }

        if (!out.empty()) {
            if (auto *t = dynamic_cast<TextNode *>(out.back().get())) {
                t->text.append(chunk);
                return;
            }
        }

        auto node = std::make_unique<TextNode>();
        node->text.assign(chunk);
        out.push_back(std::move(node));
    }

    NodeList InlineParser::parse(const std::string_view text) {
        NodeList out;
        parse_into(out, text);

        return out;
    }

    void InlineParser::parse_into(NodeList &out, const std::string_view text) {
        std::string buf;
        const std::size_t n = text.size();
        std::size_t i = 0;

        // Dumps the pending literal buffer as a text node before a construct
        // splits the run, so text on both sides stays separate.
        auto flush = [&] {
            append_text(out, buf);
            buf.clear();
        };

        while (i < n) {
            const char c = text[i];

            if (c == '\n') {
                // Two or more trailing spaces make a hard break; otherwise the
                // newline is just soft text inside the paragraph.
                std::size_t spaces = 0;
                while (spaces < buf.size() && buf[buf.size() - 1 - spaces] == ' ') {
                    ++spaces;
                }

                if (spaces >= 2) {
                    buf.erase(buf.size() - 2);
                    flush();

                    auto br = std::make_unique<LineBreakNode>();
                    br->hard = true;
                    out.push_back(std::move(br));
                } else {
                    flush();
                    append_text(out, "\n");
                }

                ++i;
                continue;
            }

            if (c == '\\' && i + 1 < n) {
                if (text[i + 1] == '\n') {
                    flush();

                    auto br = std::make_unique<LineBreakNode>();
                    br->hard = true;
                    out.push_back(std::move(br));
                    i += 2;
                    continue;
                }

                if (is_escapable(text[i + 1])) {
                    buf.push_back(text[i + 1]);
                    i += 2;
                    continue;
                }

                buf.push_back(c);
                ++i;
                continue;
            }

            if (c == '`') {
                // The opening run length selects the closer: only a run of exactly
                // that many ticks closes the span, so embedded backticks stay literal.
                std::size_t ticks = 0;
                while (i + ticks < n && text[i + ticks] == '`') {
                    ++ticks;
                }

                std::size_t search = i + ticks;
                std::size_t close_pos = std::string_view::npos;
                std::size_t close_len = 0;

                while (search < n) {
                    if (text[search] == '`') {
                        std::size_t run = 0;
                        std::size_t j = search;

                        while (j < n && text[j] == '`') {
                            ++run;
                            ++j;
                        }

                        if (run == ticks) {
                            close_pos = search;
                            close_len = run;
                            break;
                        }

                        search = j;
                    } else {
                        ++search;
                    }
                }

                if (close_pos != std::string_view::npos) {
                    flush();

                    std::string code(text.substr(i + ticks, close_pos - (i + ticks)));

                    // Strip one space from each end, but only when the interior
                    // still contains something other than spaces.
                    if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ') {
                        bool content_nonspace = false;

                        for (std::size_t k = 1; k + 1 < code.size(); ++k) {
                            if (code[k] != ' ') {
                                content_nonspace = true;
                                break;
                            }
                        }

                        if (content_nonspace) {
                            code = code.substr(1, code.size() - 2);
                        }
                    }

                    auto node = std::make_unique<CodeSpanNode>();
                    node->code = std::move(code);
                    out.push_back(std::move(node));
                    i = close_pos + close_len;
                    continue;
                }

                buf.append(ticks, '`');
                i += ticks;
                continue;
            }

            if (c == '[' || (c == '!' && i + 1 < n && text[i + 1] == '[')) {
                // Footnote syntax is probed first so "[^label]" is not read as a link.
                flush();

                if (c == '[' && i + 1 < n && text[i + 1] == '^') {
                    if (const auto next = try_footnote_ref(out, text, i); next != i) {
                        i = next;
                        continue;
                    }
                }

                if (const auto next = try_link_image(out, text, i); next != i) {
                    i = next;
                    continue;
                }
            }

            if (c == '$') {
                flush();

                if (const auto next = try_math(out, text, i); next != i) {
                    i = next;
                    continue;
                }
            }

            if (c == '<') {
                flush();

                if (const auto next = try_autolink_html(out, text, i); next != i) {
                    i = next;
                    continue;
                }
            }

            if (c == '*' || c == '_' || (c == '~' && opts_.gfm_strikethrough)) {
                // Strikethrough stays behind the GFM option; * and _ always reach
                // the emphasis scanner.
                if (const auto next = try_delims(out, buf, text, i); next != i) {
                    i = next;
                    continue;
                }
            }

            buf.push_back(c);
            ++i;
        }

        flush();
    }

    std::size_t InlineParser::try_link_image(NodeList &out, const std::string_view text,
                                             std::size_t i) {
        const std::size_t n = text.size();
        const std::size_t start = i;
        bool is_image = false;

        if (text[i] == '!') {
            if (i + 1 >= n || text[i + 1] != '[') {
                return i;
            }

            is_image = true;
            ++i;
        }

        ++i;

        // Brackets nest, so the label runs until the "]" that takes depth back to zero.
        std::size_t depth = 1;
        const std::size_t label_start = i;
        std::size_t label_end = std::string_view::npos;

        while (i < n) {
            if (text[i] == '\\' && i + 1 < n) {
                i += 2;
                continue;
            }
            if (text[i] == '[') {
                ++depth;
            }
            if (text[i] == ']') {
                --depth;

                if (depth == 0) {
                    label_end = i;
                    break;
                }
            }

            ++i;
        }

        if (label_end == std::string_view::npos) {
            return start;
        }

        const std::string_view labelText = text.substr(label_start, label_end - label_start);
        const std::size_t after = label_end + 1;

        std::string dest, title;
        std::size_t end_pos = std::string_view::npos;

        if (after < n && text[after] == '(') {
            // Inline destination: scan balanced parentheses while skipping
            // backslash escapes, giving up when they never balance.
            std::size_t j = after + 1;
            std::size_t paren = 1;

            while (j < n) {
                if (text[j] == '\\' && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if (text[j] == '(') {
                    ++paren;
                }
                if (text[j] == ')') {
                    --paren;

                    if (paren == 0) {
                        break;
                    }
                }

                ++j;
            }

            if (paren != 0) {
                return start;
            }

            std::string_view inside = trim(text.substr(after + 1, j - after - 1));

            if (!inside.empty() && inside.front() == '<') {
                const auto gt = inside.find('>');

                if (gt == std::string_view::npos) {
                    return start;
                }

                dest = std::string(inside.substr(1, gt - 1));
                inside = ltrim(inside.substr(gt + 1));
            } else {
                std::size_t sp = 0;
                while (sp < inside.size() &&
                       !std::isspace(static_cast<unsigned char>(inside[sp]))) {
                    ++sp;
                }

                dest = std::string(inside.substr(0, sp));
                inside = ltrim(inside.substr(sp));
            }

            if (inside.size() >= 2) {
                if (const char q = inside.front();
                    (q == '"' || q == '\'') && inside.back() == q) {
                    title = std::string(inside.substr(1, inside.size() - 2));
                }
            }

            end_pos = j + 1;
        } else {
            if (!refs_) {
                return start;
            }

            // Reference destination: an explicit "[other label]" overrides the
            // key implied by the link text itself.
            std::string key;
            std::size_t search_from = after;

            if (after < n && text[after] == '[') {
                std::size_t j = after + 1;
                std::size_t depth = 1;

                while (j < n) {
                    if (text[j] == '\\' && j + 1 < n) {
                        j += 2;
                        continue;
                    }
                    if (text[j] == '[') {
                        ++depth;
                    }
                    if (text[j] == ']') {
                        --depth;

                        if (depth == 0) {
                            break;
                        }
                    }

                    ++j;
                }

                if (depth != 0) {
                    return start;
                }

                const std::string_view explicit_label = text.substr(after + 1, j - after - 1);
                key = explicit_label.empty()
                          ? normalize_ref_key(labelText)
                          : normalize_ref_key(explicit_label);
                search_from = j + 1;
            } else {
                key = normalize_ref_key(labelText);
            }

            const auto it = refs_->find(key);

            if (it == refs_->end()) {
                return start;
            }

            dest = it->second.first;
            title = it->second.second;
            end_pos = search_from;
        }

        if (end_pos == std::string_view::npos) {
            return start;
        }

        if (is_image) {
            auto img = std::make_unique<ImageNode>();
            img->src = dest;
            img->alt = resolve_escapes(labelText);
            img->title = title;
            out.push_back(std::move(img));
        } else {
            auto link = std::make_unique<LinkNode>();
            link->href = dest;
            link->title = title;
            link->inlines = parse(labelText);
            out.push_back(std::move(link));
        }

        return end_pos;
    }

    std::size_t InlineParser::try_footnote_ref(NodeList &out, const std::string_view text,
                                               const std::size_t i) {
        const std::size_t n = text.size();

        if (i + 2 >= n || text[i] != '[' || text[i + 1] != '^') {
            return i;
        }

        const auto close = text.find(']', i + 2);

        if (close == std::string_view::npos || close == i + 2) {
            return i;
        }

        // "[^label]:" is a definition line, not a reference; the pre-pass already
        // removed those, so bail out and let it be treated as ordinary text.
        if (close + 1 < n && text[close + 1] == ':') {
            return i;
        }

        auto node = std::make_unique<FootnoteRefNode>();
        node->label.assign(text.substr(i + 2, close - i - 2));
        out.push_back(std::move(node));

        return close + 1;
    }

    std::size_t InlineParser::try_math(NodeList &out, const std::string_view text,
                                       const std::size_t i) {
        const std::size_t n = text.size();

        if (i >= n || text[i] != '$') {
            return i;
        }

        const bool display = (i + 1 < n && text[i + 1] == '$');
        const std::size_t open_len = display ? 2 : 1;

        if (i + open_len >= n) {
            return i;
        }

        if (!display && std::isspace(static_cast<unsigned char>(text[i + 1]))) {
            return i;
        }

        std::size_t j = i + open_len;

        // Scan for a closing run. A closer only counts when the body is non-empty
        // and has no whitespace right before it, and a single-$ span must also lie
        // on one line, which keeps ordinary prices like "$5 and $6" as plain text.
        while (j + open_len <= n) {
            if (text[j] == '$') {
                const std::size_t content_len = j - (i + open_len);
                bool valid = content_len > 0 && text[j - 1] != ' ' && text[j - 1] != '\t';

                if (valid && open_len == 1) {
                    valid = text.find('\n', i + open_len) >= j;
                }

                if (valid) {
                    auto node = std::make_unique<MathNode>();
                    node->tex.assign(text.substr(i + open_len, content_len));
                    node->display = display;
                    out.push_back(std::move(node));
                    return j + open_len;
                }

                if (open_len == 2) {
                    break;
                }
            }

            ++j;
        }

        return i;
    }

    std::size_t InlineParser::try_autolink_html(NodeList &out, const std::string_view text,
                                                const std::size_t i) const {
        // Everything between '<' and '>' is offered to the autolink rules first,
        // then to raw HTML; returning i leaves the '<' as literal text.
        const std::size_t gt = text.find('>', i + 1);

        if (gt == std::string_view::npos || gt - i > 2000) {
            return i;
        }

        const std::string_view inner = text.substr(i + 1, gt - i - 1);

        if (inner.empty() || inner.find(' ') != std::string_view::npos) {
            // could still be HTML with spaces in attributes so fall through to HTML check
        } else {
            // Known schemes and bare email addresses become links; anything else
            // falls through to the raw HTML check below.
            static const char *schemes[] = {"http://", "https://", "ftp://", "mailto:"};
            bool is_scheme = false;

            for (const char *s: schemes) {
                if (inner.starts_with(s)) {
                    is_scheme = true;
                    break;
                }
            }

            const bool is_email =
                    !is_scheme && inner.find('@') != std::string_view::npos &&
                    inner.find('"') == std::string_view::npos &&
                    inner.find('<') == std::string_view::npos;

            if (is_scheme || is_email) {
                auto link = std::make_unique<LinkNode>();
                link->href = (is_email && !inner.starts_with("mailto:"))
                                 ? "mailto:" + std::string(inner)
                                 : std::string(inner);

                auto t = std::make_unique<TextNode>();
                t->text = std::string(inner);
                link->inlines.push_back(std::move(t));
                out.push_back(std::move(link));

                return gt + 1;
            }
        }

        if (opts_.raw_html && !inner.empty()) {
            const char f = inner.front();
            const bool looks_tag =
                    f == '/' || f == '!' || f == '?' || std::isalpha(static_cast<unsigned char>(f));

            if (looks_tag) {
                auto html = std::make_unique<HtmlInlineNode>();
                html->html.assign(text.substr(i, gt - i + 1));
                out.push_back(std::move(html));

                return gt + 1;
            }
        }

        return i;
    }

    std::size_t InlineParser::try_delims(NodeList &out, std::string &buf,
                                         const std::string_view text, const std::size_t i) const {
        const std::size_t n = text.size();
        const char marker = text[i];

        if (marker == '~') {
            // Strikethrough is a simple non-nesting scan: it needs a "~~" opener,
            // a "~~" closer, and at least one character between them.
            if (i + 1 >= n || text[i + 1] != '~') {
                return i;
            }

            std::size_t j = i + 2;
            while (j + 1 < n) {
                if (text[j] == '~' && text[j + 1] == '~') {
                    break;
                }
                ++j;
            }

            if (j + 1 >= n) {
                return i;
            }
            if (j == i + 2) {
                return i;
            }

            append_text(out, buf);
            buf.clear();

            InlineParser sub = make_sub();
            auto node = std::make_unique<StrikethroughNode>();
            node->inlines = sub.parse(text.substr(i + 2, j - i - 2));
            out.push_back(std::move(node));

            return j + 2;
        }

        std::size_t run = 0;
        while (i + run < n && text[i + run] == marker) {
            ++run;
        }

        if (run == 0) {
            return i;
        }

        if (run >= 2) {
            // Long runs first try a strong span; when the intraword underscore
            // rules reject the candidate pair, control jumps to the single-marker
            // fallback below rather than failing the whole delimiter run.
            constexpr std::size_t want = 2;
            std::size_t j = i + run;

            while (j + want <= n) {
                if (text[j] == marker && text[j + 1] == marker) {
                    std::size_t closer_run = 0;
                    while (j + closer_run < n && text[j + closer_run] == marker) {
                        ++closer_run;
                    }

                    if (const std::size_t inner_len = j - (i + run); inner_len > 0) {
                        if (marker == '_') {
                            // Left-flank test: an intraword "_" cannot open strong,
                            // so retry the run under the single-marker rules.
                            if (const char prev = i > 0 ? text[i - 1] : ' ';
                                i > 0 &&
                                (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_')) {
                                goto try_single;
                            }

                            const char nextc =
                                    (j + 2) < n ? text[j + 2] : ' ';

                            if (j + 2 < n &&
                                (std::isalnum(static_cast<unsigned char>(nextc)) ||
                                 nextc == '_')) {
                                j += (closer_run > 0 ? closer_run : 1);
                                continue;
                            }
                        }

                        append_text(out, buf);
                        buf.clear();

                        InlineParser sub = make_sub();
                        auto node = std::make_unique<StrongNode>();
                        node->inlines =
                                sub.parse(text.substr(i + run, inner_len));

                        const std::size_t consumed = std::min(run, closer_run);

                        if (run >= 3 && closer_run >= 3) {
                            // Six or more markers total: the inner three wrap the
                            // strong span to give em(strong), as CommonMark does.
                            auto em = std::make_unique<EmphasisNode>();
                            em->inlines.push_back(std::move(node));
                            out.push_back(std::move(em));
                        } else {
                            out.push_back(std::move(node));
                        }

                        return j + consumed;
                    }
                }

                if (text[j] == '\\') {
                    j += 2;
                    continue;
                }

                ++j;
            }
        }

    try_single:

        // Single-marker fallback: applies the left and right flank rules that the
        // double-marker pass skipped, and ignores a closer that is really part of
        // a double run, which the strong pass would already have claimed.
        if (run >= 1) {
            const char prev = i > 0 ? text[i - 1] : ' ';
            bool left_ok = true;

            if (marker == '_') {
                if (i > 0 && (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_')) {
                    left_ok = false;
                }
            }

            if (i + 1 < n &&
                std::isspace(static_cast<unsigned char>(text[i + 1]))) {
                left_ok = false;
            }

            if (left_ok) {
                std::size_t j = i + 1;
                while (j < n) {
                    if (text[j] == '\\') {
                        j += 2;
                        continue;
                    }

                    if (text[j] == marker) {
                        std::size_t run_end = j;
                        while (run_end < n && text[run_end] == marker) {
                            ++run_end;
                        }
                        const std::size_t closer_run = run_end - j;

                        if (closer_run >= 2 && run == 1) {
                            j = run_end;
                            continue;
                        }

                        const char nextc = run_end < n ? text[run_end] : ' ';
                        bool right_ok = true;

                        if (marker == '_') {
                            if (run_end < n &&
                                (std::isalnum(static_cast<unsigned char>(nextc)) ||
                                 nextc == '_')) {
                                right_ok = false;
                            }
                        }

                        if (std::isspace(static_cast<unsigned char>(j > 0 ? text[j - 1] : ' '))) {
                            right_ok = false;
                        }

                        if (right_ok && closer_run >= 1) {
                            if (const std::size_t inner_len = j - (i + 1); inner_len > 0) {
                                append_text(out, buf);
                                buf.clear();

                                InlineParser sub = make_sub();
                                auto node = std::make_unique<EmphasisNode>();
                                node->inlines =
                                        sub.parse(text.substr(i + 1, inner_len));
                                out.push_back(std::move(node));

                                return j + 1;
                            }
                        }

                        j = run_end;
                        continue;
                    }

                    ++j;
                }
            }
        }

        return i;
    }
}
