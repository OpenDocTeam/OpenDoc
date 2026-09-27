#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace opendoc::theme {
    // Thrown for template syntax errors; carries the offending line number in the message.
    class TemplateError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    struct TemplateValue;
    // Named map of values, used for section scopes and dotted key lookups.
    using Object = std::map<std::string, TemplateValue>;
    // Ordered list of values, used to iterate a section once per item.
    using Sequence = std::vector<TemplateValue>;

    // One renderable value: an unset, bool, string, object, or list variant.
    struct TemplateValue {
        std::variant<std::monostate, bool, std::string, Object, Sequence> data;

        // Reports whether this value should render its section body.
        [[nodiscard]] bool truthy() const;

        // Returns the nested value for a key, or null when not an object or missing.
        [[nodiscard]] const TemplateValue *child(std::string_view key) const;
    };

    // Flat key to value store that feeds variables into a template render.
    class TemplateContext {
    public:
        // Stores a string value under an absolute key, replacing any prior entry.
        void set(std::string key, std::string value);

        // Stores a boolean flag so truthiness checks can gate sections.
        void set_bool(std::string key, bool flag);

        // Stores a list value so a section can render once per item.
        void set_sequence(std::string key, Sequence items);

        // Looks up a top level key, returning null when it is absent.
        [[nodiscard]] const TemplateValue *find(std::string_view key) const;

        // Exposes the whole map for callers that need to iterate the context.
        [[nodiscard]] const std::map<std::string, TemplateValue> &values() const noexcept {
            return values_;
        }

    private:
        std::map<std::string, TemplateValue> values_;
    };

    // Compiles a template source once and renders it against many contexts.
    class TemplateEngine {
    public:
        // Tokenizes the source eagerly so a bad template fails at construction.
        explicit TemplateEngine(std::string source);

        // Expands every token into the output string using the given context.
        [[nodiscard]] std::string render(const TemplateContext &ctx) const;

        // Returns the original unmodified template text.
        [[nodiscard]] const std::string &source() const noexcept {
            return source_;
        }

    private:
        // One lexed piece of the template: literal text or a single tag.
        struct Token {
            // Discriminates literal text, variables, and section delimiters.
            enum class Kind { Text, Var, RawVar, SectionOpen, InvertedOpen, SectionClose };

            Kind kind;
            // Variable or section name without its sigil, empty for Text tokens.
            std::string name;
            // Literal payload, populated only for Text tokens.
            std::string text;
            // 1-based source line, used to report template errors.
            std::size_t line = 0;
        };

        // Splits the source into tokens, throwing TemplateError on malformed input.
        static std::vector<Token> tokenize(const std::string &src);

        // Pre-lexed tokens produced by the constructor.
        std::vector<Token> tokens_;
        std::string source_;
    };
}
