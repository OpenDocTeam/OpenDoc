#include "opendoc/parser/front_matter.hpp"

#include <yaml-cpp/yaml.h>

#include <cctype>

namespace opendoc::parser {
    bool FrontMatter::has(const std::string_view key) const {
        return fields.contains(std::string(key));
    }

    std::string FrontMatter::get(const std::string_view key, const std::string_view def) const {
        const auto it = fields.find(std::string(key));
        return it == fields.end() ? std::string(def) : it->second;
    }

    namespace {
        // Trims ASCII whitespace from both ends of a view, without allocating.
        std::string_view trim_view(std::string_view s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
                s.remove_prefix(1);
            }

            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
                s.remove_suffix(1);
            }
            return s;
        }

        // Flattens any YAML node into the single string stored in the field map:
        // scalars verbatim, null as empty, sequences joined with ", ", and maps as
        // key=value pairs so nested metadata is not silently dropped.
        std::string scalar_to_string(const YAML::Node &node) {
            if (node.IsScalar()) {
                return node.Scalar();
            }
            if (node.IsNull()) {
                return "";
            }
            if (node.IsSequence()) {
                std::string out;
                for (std::size_t i = 0; i < node.size(); ++i) {
                    if (i > 0) {
                        out += ", ";
                    }
                    out += node[i].Scalar();
                }
                return out;
            }
            if (node.IsMap()) {
                std::string out;
                bool first = true;
                for (const auto &kv: node) {
                    if (!first) {
                        out += ", ";
                    }
                    first = false;
                    out += kv.first.Scalar();
                    out += '=';
                    out += kv.second.IsScalar() ? kv.second.Scalar() : std::string{};
                }
                return out;
            }
            return "";
        }
    }

    SplitResult split_front_matter(std::string_view source) {
        SplitResult result;
        result.body = source;

        // A UTF-8 BOM would defeat the `---` check below; drop it up front.
        if (source.starts_with("\xEF\xBB\xBF")) {
            source.remove_prefix(3);
            result.body = source;
        }

        const auto trimmed_start = trim_view(source);
        if (trimmed_start.empty() || !trimmed_start.starts_with("---")) {
            return result;
        }
        if (const auto after_open = trimmed_start.substr(3);
            !after_open.empty() && after_open.front() != '\n' && after_open.front() != '\r') {
            return result;
        }

        const auto open_offset = static_cast<std::size_t>(trimmed_start.data() - source.data());
        std::size_t body_start = source.find('\n', open_offset);

        if (body_start == std::string_view::npos) {
            return result;
        }

        ++body_start;
        // Walk lines looking for the closing "---" or "..."; if none appears before
        // the end of input the block is unterminated and the whole source stays body.
        std::size_t line_start = body_start;
        while (line_start <= source.size()) {
            const std::size_t line_end = source.find('\n', line_start);
            const std::string_view line = (line_end == std::string_view::npos)
                                              ? source.substr(line_start)
                                              : source.substr(line_start, line_end - line_start);

            if (const auto line_trimmed = trim_view(line);
                line_trimmed == "---" || line_trimmed == "...") {
                const std::string yaml_text(source.substr(body_start, line_start - body_start));
                const std::size_t body_pos = (line_end == std::string_view::npos)
                                                 ? source.size()
                                                 : line_end + 1;
                result.body = source.substr(body_pos);

                try {
                    if (const YAML::Node root = YAML::Load(yaml_text); root.IsMap()) {
                        for (const auto &kv: root) {
                            if (kv.first.IsScalar()) {
                                result.front.fields[kv.first.Scalar()] =
                                        scalar_to_string(kv.second);
                            }
                        }
                    }
                } catch (const YAML::Exception &e) {
                    // Translate the yaml-cpp mark (0-based inside yaml_text)
                    // into a 1-based line number in the original source.
                    std::size_t yaml_line = 1;
                    for (std::size_t i = 0; i < body_start; ++i) {
                        if (source[i] == '\n') {
                            ++yaml_line;
                        }
                    }
                    const std::size_t line =
                            e.mark.is_null() ? yaml_line : yaml_line + e.mark.line;
                    throw ParseError(std::string("invalid front matter YAML: ") +
                                             e.what(),
                                     line);
                }

                return result;
            }

            if (line_end == std::string_view::npos) {
                break;
            }
            line_start = line_end + 1;
        }

        return result;
    }
}
