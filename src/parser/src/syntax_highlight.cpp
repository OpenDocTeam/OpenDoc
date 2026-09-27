#include "opendoc/parser/syntax_highlight.hpp"

#include <cctype>
#include <unordered_set>

namespace opendoc::parser {
    namespace {
        // Languages the built-in highlighter knows; None means "escape and stop".
        enum class Lang {
            None, Cpp, JavaScript, Python, Bash, Yaml, Json, Sql, Rust, Go, Java, Xml
        };

        // Maps an info-string tag to a language, folding aliases (js/ts, py, sh...)
        // together; C-family tags share one lexer except for Java and C#.
        Lang lang_from_tag(const std::string_view tag) {
            if (tag.empty()) {
                return Lang::None;
            }
            if (tag == "cpp" || tag == "c++" || tag == "cc" || tag == "cxx" ||
                tag == "c" || tag == "h" || tag == "hpp" || tag == "hh" ||
                tag == "java" || tag == "cs" || tag == "csharp") {
                return tag == "java" || tag == "cs" || tag == "csharp"
                           ? Lang::Java
                           : Lang::Cpp;
            }
            if (tag == "js" || tag == "javascript" || tag == "ts" || tag == "typescript" ||
                tag == "jsx" || tag == "tsx" || tag == "mjs" || tag == "node") {
                return Lang::JavaScript;
            }
            if (tag == "py" || tag == "python" || tag == "python3") {
                return Lang::Python;
            }
            if (tag == "bash" || tag == "sh" || tag == "shell" || tag == "zsh" ||
                tag == "console" || tag == "shellsession") {
                return Lang::Bash;
            }
            if (tag == "yaml" || tag == "yml") {
                return Lang::Yaml;
            }
            if (tag == "json" || tag == "json5") {
                return Lang::Json;
            }
            if (tag == "sql" || tag == "mysql" || tag == "postgres" || tag == "sqlite") {
                return Lang::Sql;
            }
            if (tag == "rust" || tag == "rs") {
                return Lang::Rust;
            }
            if (tag == "go" || tag == "golang") {
                return Lang::Go;
            }
            if (tag == "xml" || tag == "html" || tag == "svg" || tag == "toml") {
                return Lang::Xml;
            }
            return Lang::None;
        }

        // Returns the static keyword set for a language, or nullptr when the
        // language has no keywords to colour; the sets live for the whole program.
        const std::unordered_set<std::string> *keywords_for(const Lang lang) {
            static const std::unordered_set<std::string> cpp = {
                "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case",
                "catch", "char", "class", "const", "constexpr", "const_cast", "continue",
                "decltype", "default", "delete", "do", "double", "dynamic_cast", "else",
                "enum", "explicit", "export", "extern", "false", "float", "for", "friend",
                "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
                "noexcept", "not", "nullptr", "operator", "or", "private", "protected",
                "public", "register", "reinterpret_cast", "return", "short", "signed",
                "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
                "template", "this", "throw", "true", "try", "typedef", "typeid",
                "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
                "wchar_t", "while", "override", "final", "constexpr", "nullptr_t",
                "size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t",
                "int16_t", "int32_t", "int64_t", "include", "define", "ifdef", "ifndef",
                "endif", "pragma", "once"
            };
            static const std::unordered_set<std::string> js = {
                "async", "await", "break", "case", "catch", "class", "const", "continue",
                "debugger", "default", "delete", "do", "else", "enum", "export", "extends",
                "false", "finally", "for", "function", "if", "implements", "import", "in",
                "instanceof", "interface", "let", "new", "null", "package", "private",
                "protected", "public", "return", "static", "super", "switch", "this",
                "throw", "true", "try", "typeof", "var", "void", "while", "with", "yield",
                "undefined", "arguments", "eval", "boolean", "number", "string", "symbol"
            };
            static const std::unordered_set<std::string> py = {
                "False", "None", "True", "and", "as", "assert", "async", "await", "break",
                "class", "continue", "def", "del", "elif", "else", "except", "finally",
                "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
                "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
                "self", "cls", "print", "len", "range", "int", "str", "float", "bool",
                "list", "dict", "set", "tuple", "type", "super", "isinstance", "object"
            };
            static const std::unordered_set<std::string> bash = {
                "if", "then", "elif", "else", "fi", "for", "in", "do", "done", "while",
                "until", "case", "esac", "function", "select", "time", "coproc", "break",
                "continue", "return", "exit", "local", "declare", "export", "readonly",
                "source", "alias", "unalias", "set", "unset", "trap", "eval", "exec",
                "echo", "printf", "read", "cd", "pwd", "test", "true", "false"
            };
            static const std::unordered_set<std::string> sql = {
                "ADD", "ALL", "ALTER", "AND", "AS", "ASC", "BETWEEN", "BY", "CASE", "CHECK",
                "COLUMN", "CONSTRAINT", "CREATE", "CROSS", "DATABASE", "DEFAULT",
                "DELETE", "DESC", "DISTINCT", "DROP", "ELSE", "END", "EXISTS", "FOREIGN",
                "FROM", "GROUP", "HAVING", "IN", "INDEX", "INNER", "INSERT", "INTO", "IS",
                "JOIN", "KEY", "LEFT", "LIKE", "LIMIT", "NOT", "NULL", "ON", "OR", "ORDER",
                "OUTER", "PRIMARY", "REFERENCES", "RIGHT", "SELECT", "SET", "TABLE", "THEN",
                "UNION", "UNIQUE", "UPDATE", "USING", "VALUES", "VIEW", "WHEN", "WHERE",
                "WITH", "add", "all", "alter", "and", "as", "asc", "between", "by", "case",
                "check", "column", "constraint", "create", "cross", "database", "default",
                "delete", "desc", "distinct", "drop", "else", "end", "exists", "foreign",
                "from", "group", "having", "in", "index", "inner", "insert", "into", "is",
                "join", "key", "left", "like", "limit", "not", "null", "on", "or", "order",
                "outer", "primary", "references", "right", "select", "set", "table", "then",
                "union", "unique", "update", "using", "values", "view", "when", "where",
                "with"
            };
            static const std::unordered_set<std::string> rust = {
                "as", "async", "await", "break", "const", "continue", "crate", "dyn", "else",
                "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop",
                "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self",
                "static", "struct", "super", "trait", "true", "type", "unsafe", "use",
                "where", "while", "bool", "char", "f32", "f64", "i8", "i16", "i32", "i64",
                "i128", "isize", "str", "u8", "u16", "u32", "u64", "u128", "usize",
                "String", "Vec", "Option", "Result", "Some", "None", "Ok", "Err", "pub"
            };
            static const std::unordered_set<std::string> go = {
                "break", "case", "chan", "const", "continue", "default", "defer", "else",
                "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
                "map", "package", "range", "return", "select", "struct", "switch", "type",
                "var", "nil", "true", "false", "iota", "append", "cap", "copy", "delete",
                "len", "make", "new", "panic", "print", "println", "recover", "string",
                "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16",
                "uint32", "uint64", "float32", "float64", "bool", "byte", "rune", "error"
            };
            static const std::unordered_set<std::string> java = {
                "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
                "class", "const", "continue", "default", "do", "double", "else", "enum",
                "extends", "final", "finally", "float", "for", "goto", "if", "implements",
                "import", "instanceof", "int", "interface", "long", "native", "new",
                "package", "private", "protected", "public", "return", "short", "static",
                "strictfp", "super", "switch", "synchronized", "this", "throw", "throws",
                "transient", "try", "void", "volatile", "while", "true", "false", "null",
                "var", "record", "sealed", "yield"
            };
            static const std::unordered_set<std::string> yaml = {
                "true", "false", "null", "yes", "no", "on", "off", "True", "False", "Null",
                "None", "~"
            };
            static const std::unordered_set<std::string> json = {
                "true", "false", "null"
            };

            switch (lang) {
                case Lang::Cpp:
                    return &cpp;
                case Lang::JavaScript:
                    return &js;
                case Lang::Python:
                    return &py;
                case Lang::Bash:
                    return &bash;
                case Lang::Sql:
                    return &sql;
                case Lang::Rust:
                    return &rust;
                case Lang::Go:
                    return &go;
                case Lang::Java:
                    return &java;
                case Lang::Yaml:
                    return &yaml;
                case Lang::Json:
                    return &json;
                default:
                    return nullptr;
            }
        }

        // Word characters that terminate an identifier or a number scan.
        bool is_word_char(const char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        // Character that starts a line comment, or 0 when the language has none.
        char line_comment_char(const Lang lang) {
            switch (lang) {
                case Lang::Python:
                case Lang::Bash:
                case Lang::Yaml:
                    return '#';
                case Lang::Cpp:
                case Lang::JavaScript:
                case Lang::Rust:
                case Lang::Go:
                case Lang::Java:
                    return '/';
                default:
                    return 0;
            }
        }

        // Languages that support /* ... */ blocks; others treat '/' as ordinary text.
        bool wants_block_comments(const Lang lang) {
            return lang == Lang::Cpp || lang == Lang::JavaScript || lang == Lang::Rust ||
                   lang == Lang::Go || lang == Lang::Java;
        }

        // JSON is excluded because its quotes delimit keys and values rather than
        // prose strings, and None has nothing worth quoting.
        bool wants_strings(const Lang lang) {
            return lang != Lang::None && lang != Lang::Json;
        }

        // Quote character the language prefers; currently informational only.
        char preferred_string_quote(const Lang lang) {
            switch (lang) {
                case Lang::Python:
                case Lang::Bash:
                    return '\'';
                case Lang::Cpp:
                case Lang::Java:
                    return '"';
                default:
                    return '"';
            }
        }
    }

    std::string highlight_escape(const std::string_view input) {
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

    std::string highlight_code(const std::string_view code, const std::string_view lang) {
        const Lang L = lang_from_tag(lang);
        if (L == Lang::None || code.empty()) {
            return highlight_escape(code);
        }

        const auto *kw = keywords_for(L);
        const char line_c = line_comment_char(L);
        const bool block_c = wants_block_comments(L);
        const bool strings = wants_strings(L);
        const char pref_q = preferred_string_quote(L);

        std::string out;
        out.reserve(code.size() + code.size() / 4);

        // Plain text written without a span wrapper, but still entity-escaped.
        auto emit_escaped = [&](const std::string_view s) { out += highlight_escape(s); };
        // Wraps escaped source in a <span> carrying the token class.
        auto emit_span = [&](const std::string_view cls, const std::string_view s) {
            out += "<span class=\"";
            out += cls;
            out += "\">";
            out += highlight_escape(s);
            out += "</span>";
        };

        std::size_t i = 0;
        const std::size_t n = code.size();
        bool in_number = false;

        while (i < n) {
            const char c = code[i];

            // '#' and similar markers comment out the rest of the line, while '/'
            // only opens a comment when a second '/' follows it.
            if (line_c && c == line_c &&
                !(line_c == '/' && i + 1 < n && code[i + 1] != '/' &&
                  line_comment_char(L) == '/')) {
                if (line_c == '/') {
                    if (i + 1 < n && code[i + 1] == '/') {
                        std::size_t j = i;
                        while (j < n && code[j] != '\n') {
                            ++j;
                        }
                        emit_span("tok-c", code.substr(i, j - i));
                        i = j;
                        continue;
                    }
                } else {
                    std::size_t j = i;
                    while (j < n && code[j] != '\n') {
                        ++j;
                    }
                    emit_span("tok-c", code.substr(i, j - i));
                    i = j;
                    continue;
                }
            }

            if (block_c && c == '/' && i + 1 < n && code[i + 1] == '*') {
                // Scans for the closing */; an unterminated block runs to end of input.
                std::size_t j = i + 2;
                while (j + 1 < n && !(code[j] == '*' && code[j + 1] == '/')) {
                    ++j;
                }
                j = (j + 1 < n) ? j + 2 : n;
                emit_span("tok-c", code.substr(i, j - i));
                i = j;
                continue;
            }

            if (strings && (c == '"' || c == '\'' || c == '`')) {
                const char q = c;
                std::size_t j = i + 1;
                while (j < n) {
                    if (code[j] == '\\' && j + 1 < n) {
                        j += 2;
                        continue;
                    }
                    if (code[j] == q) {
                        ++j;
                        break;
                    }
                    if (q != '`' && code[j] == '\n') {
                        break;
                    }
                    ++j;
                }
                emit_span("tok-s", code.substr(i, j - i));
                i = j;
                continue;
            }

            (void) pref_q;

            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && i + 1 < n &&
                 std::isdigit(static_cast<unsigned char>(code[i + 1])) &&
                 (i == 0 || !is_word_char(code[i - 1])))) {
                std::size_t j = i;
                while (j < n) {
                    if (const char d = code[j];
                        std::isalnum(static_cast<unsigned char>(d)) || d == '.' ||
                        d == '_' || d == 'x' || d == 'X') {
                        if ((d == 'e' || d == 'E') && j + 1 < n &&
                            (code[j + 1] == '+' || code[j + 1] == '-')) {
                            ++j;
                        }
                        ++j;
                    } else {
                        break;
                    }
                }
                emit_span("tok-n", code.substr(i, j - i));
                i = j;
                in_number = false;
                continue;
            }

            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::size_t j = i;
                while (j < n && is_word_char(code[j])) {
                    ++j;
                }
                const auto word = std::string(code.substr(i, j - i));
                // Keyword, call, or plain identifier, decided by the set and what
                // immediately follows the word.
                if (kw && kw->contains(word)) {
                    emit_span("tok-k", word);
                } else if (j < n && code[j] == '(') {
                    emit_span("tok-f", word);
                } else {
                    emit_escaped(word);
                }
                i = j;
                continue;
            }

            emit_escaped(std::string_view(&c, 1));
            ++i;
            in_number = false;
        }

        (void) in_number;
        return out;
    }
}
