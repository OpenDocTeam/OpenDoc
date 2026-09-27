#include "opendoc/theme/template_engine.hpp"

#include "opendoc/parser/html_renderer.hpp"

#include <cctype>

namespace opendoc::theme {
    bool TemplateValue::truthy() const {
        if (const auto *b = std::get_if<bool>(&data)) {
            return *b;
        }

        if (const auto *s = std::get_if<std::string>(&data)) {
            return !s->empty();
        }

        if (const auto *seq = std::get_if<Sequence>(&data)) {
            return !seq->empty();
        }

        if (const auto *obj = std::get_if<Object>(&data)) {
            return !obj->empty();
        }

        if (std::get_if<std::monostate>(&data)) {
            return false;
        }

        return false;
    }

    const TemplateValue *TemplateValue::child(const std::string_view key) const {
        if (const auto *obj = std::get_if<Object>(&data)) {
            const auto it = obj->find(std::string(key));
            if (it != obj->end()) {
                return &it->second;
            }
        }

        return nullptr;
    }

    void TemplateContext::set(std::string key, std::string value) {
        TemplateValue v;
        v.data = std::move(value);
        values_[std::move(key)] = std::move(v);
    }

    void TemplateContext::set_bool(std::string key, bool flag) {
        TemplateValue v;
        v.data = flag;
        values_[std::move(key)] = std::move(v);
    }

    void TemplateContext::set_sequence(std::string key, Sequence items) {
        TemplateValue v;
        v.data = std::move(items);
        values_[std::move(key)] = std::move(v);
    }

    const TemplateValue *TemplateContext::find(const std::string_view key) const {
        const auto it = values_.find(std::string(key));
        if (it == values_.end()) {
            return nullptr;
        }

        return &it->second;
    }

    namespace {
        // Strips leading and trailing whitespace so tag names compare cleanly.
        std::string_view trim_name(std::string_view s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
                s.remove_prefix(1);
            }

            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
                s.remove_suffix(1);
            }

            return s;
        }

        // Resolves a name against the innermost section scope first, then the context.
        const TemplateValue *lookup(const TemplateContext &ctx, const std::string_view name,
                                    const TemplateValue *scope) {
            if (scope) {
                if (auto *c = scope->child(name)) {
                    return c;
                }
            }

            return ctx.find(name);
        }

        // Flattens a value to text for output; objects and lists render as empty.
        std::string scalar_string(const TemplateValue &v) {
            if (const auto *s = std::get_if<std::string>(&v.data)) {
                return *s;
            }

            if (const auto *b = std::get_if<bool>(&v.data)) {
                return *b ? "true" : "false";
            }

            return {};
        }
    }

    std::vector<TemplateEngine::Token> TemplateEngine::tokenize(const std::string &src) {
        std::vector<Token> tokens;
        std::size_t i = 0;
        const std::size_t n = src.size();
        std::size_t line = 1;
        std::size_t text_start = 0;

        // Emits the literal run accumulated since text_start up to `end`.
        auto flush_text = [&](const std::size_t end) {
            if (end > text_start) {
                Token t;
                t.kind = Token::Kind::Text;
                t.text = src.substr(text_start, end - text_start);
                t.line = line;
                tokens.push_back(std::move(t));
            }
        };

        // Scans for '{'; any brace that does not open a tag stays literal text.
        while (i < n) {
            if (src[i] == '\n') {
                ++line;
                ++i;
                continue;
            }
            if (src[i] != '{') {
                ++i;
                continue;
            }

            // Triple-brace tags emit raw output and end at the matching triple brace.
            if (i + 2 < n && src[i + 1] == '{' && src[i + 2] == '{') {
                const auto close = src.find("}}}", i + 3);
                if (close == std::string::npos) {
                    throw TemplateError("unclosed {{{ raw var at line " + std::to_string(line));
                }

                flush_text(i);
                std::string name(trim_name(src.substr(i + 3, close - (i + 3))));
                if (name.empty()) {
                    throw TemplateError("empty variable name at line " + std::to_string(line));
                }

                Token t;
                t.kind = Token::Kind::RawVar;
                t.name = std::move(name);
                t.line = line;
                tokens.push_back(std::move(t));
                i = close + 3;
                text_start = i;
                continue;
            }

            // A double brace tag; the leading sigil picks the token kind.
            if (i + 1 < n && src[i + 1] == '{') {
                const auto close = src.find("}}", i + 2);
                if (close == std::string::npos) {
                    throw TemplateError("unclosed {{ at line " + std::to_string(line));
                }

                flush_text(i);
                std::string_view inner = trim_name(src.substr(i + 2, close - (i + 2)));
                if (inner.empty()) {
                    throw TemplateError("empty tag at line " + std::to_string(line));
                }

                Token t;
                t.line = line;
                // # opens a section, ^ an inverted one, / closes, ! and & emit nothing.
                if (inner.front() == '#') {
                    t.kind = Token::Kind::SectionOpen;
                    t.name = std::string(trim_name(inner.substr(1)));
                } else if (inner.front() == '^') {
                    t.kind = Token::Kind::InvertedOpen;
                    t.name = std::string(trim_name(inner.substr(1)));
                } else if (inner.front() == '/') {
                    t.kind = Token::Kind::SectionClose;
                    t.name = std::string(trim_name(inner.substr(1)));
                } else if (inner.front() == '!' || inner.front() == '&') {
                    i = close + 2;
                    text_start = i;
                    continue;
                } else {
                    t.kind = Token::Kind::Var;
                    t.name = std::string(inner);
                }

                if (t.name.empty()) {
                    throw TemplateError("empty section name at line " + std::to_string(line));
                }

                tokens.push_back(std::move(t));
                i = close + 2;
                text_start = i;
                continue;
            }
            ++i;
        }

        flush_text(n);
        return tokens;
    }

    TemplateEngine::TemplateEngine(std::string source) : source_(std::move(source)) {
        tokens_ = tokenize(source_);
    }

    std::string TemplateEngine::render(const TemplateContext &ctx) const {
        std::string out;
        out.reserve(source_.size() * 2);

        // Recursive walker over token [begin, end); sections re-enter it with a new scope.
        auto render_range = [&](const std::size_t begin, const std::size_t end,
                                const TemplateValue *scope,
                                auto &&self) -> void {
            std::size_t i = begin;
            while (i < end) {
                const auto &[kind, name, text, line] = tokens_[i];
                switch (kind) {
                    case Token::Kind::Text:
                        out += text;
                        ++i;
                        break;
                    // Escaped output: HTML-escaped before it reaches the page.
                    case Token::Kind::Var: {
                        if (const auto *v = lookup(ctx, name, scope)) {
                            out += parser::HtmlRenderer::escape_html(scalar_string(*v));
                        }
                        ++i;
                        break;
                    }
                    // Raw output: appended untouched so pre-rendered HTML survives.
                    case Token::Kind::RawVar: {
                        if (const auto *v = lookup(ctx, name, scope)) {
                            out += scalar_string(*v);
                        }
                        ++i;
                        break;
                    }
                    // Section bodies are found by a forward scan before rendering.
                    case Token::Kind::SectionOpen:
                    case Token::Kind::InvertedOpen: {
                        const bool inverted = kind == Token::Kind::InvertedOpen;
                        // Depth counts nested opens so only the matching close terminates.
                        int depth = 1;
                        std::size_t j = i + 1;
                        std::size_t match = std::string::npos;
                        for (; j < end; ++j) {
                            if (tokens_[j].kind == Token::Kind::SectionOpen ||
                                tokens_[j].kind == Token::Kind::InvertedOpen) {
                                ++depth;
                            } else if (tokens_[j].kind == Token::Kind::SectionClose) {
                                --depth;
                                if (depth == 0) {
                                    if (tokens_[j].name != name) {
                                        throw TemplateError(
                                            "section '" + name + "' closed by '</" +
                                            tokens_[j].name + ">' at line " +
                                            std::to_string(tokens_[j].line));
                                    }

                                    match = j;
                                    break;
                                }
                            }
                        }

                        if (match == std::string::npos) {
                            throw TemplateError("unclosed section '" + name +
                                                "' at line " + std::to_string(line));
                        }

                        const auto *v = lookup(ctx, name, scope);
                        const bool truthy = v && v->truthy();

                        // Normal section: truthy value renders once, lists render per item.
                        if (!inverted) {
                            if (v && !truthy) {
                                // skip
                            } else if (v && std::get_if<Sequence>(&v->data)) {
                                // Each object item becomes the scope for its own pass.
                                for (const auto &item: *std::get_if<Sequence>(&v->data)) {
                                    if (const auto *obj = std::get_if<Object>(&item.data)) {
                                        TemplateValue itemv;
                                        itemv.data = *obj;
                                        self(i + 1, match, &itemv, self);
                                    } else {
                                        self(i + 1, match, &item, self);
                                    }
                                }
                            } else if (v) {
                                self(i + 1, match, v, self);
                            } else {
                            }
                        } else {
                            // Inverted section: renders only when absent or falsy.
                            if (!v || !truthy) {
                                self(i + 1, match, scope, self);
                            }
                        }

                        i = match + 1;
                        break;
                    }
                    // A close tag reaching here had no open, so the template is malformed.
                    case Token::Kind::SectionClose: {
                        throw TemplateError("unexpected </" + name +
                                            "> at line " + std::to_string(line));
                    }
                }
            }
        };

        render_range(0, tokens_.size(), nullptr, render_range);
        return out;
    }
}
