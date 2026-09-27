#include "opendoc/mapper/scanner.hpp"

#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace opendoc::mapper {
    bool is_reparse_point([[maybe_unused]] const std::filesystem::path &path) {
#if defined(_WIN32)
        const DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES &&
               (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
        return false;
#endif
    }

    ScanResult DocsScanner::scan(const std::filesystem::path &docs_root) {
        if (!std::filesystem::exists(docs_root)) {
            throw std::filesystem::filesystem_error("docs directory does not exist",
                                                    docs_root, std::error_code{});
        }
        if (!std::filesystem::is_directory(docs_root)) {
            throw std::filesystem::filesystem_error("docs_dir is not a directory",
                                                    docs_root, std::error_code{});
        }

        ScanResult result;
        std::error_code walk_ec;
        std::filesystem::recursive_directory_iterator it(docs_root, walk_ec), end;
        if (walk_ec) {
            throw std::filesystem::filesystem_error("cannot read docs directory",
                                                    docs_root, walk_ec);
        }

        for (; it != end; it.increment(walk_ec)) {
            // Unreadable entries are skipped rather than aborting the whole walk.
            if (walk_ec) {
                walk_ec.clear();
                continue;
            }
            
            // Never descend into symlinks or Windows junctions: they can form cycles
            // and would let the build read files from outside docs_root.
            const bool reparse =
                    it->is_symlink(walk_ec) || is_reparse_point(it->path());
            walk_ec.clear();
            if (reparse) {
                it.disable_recursion_pending();
                continue;
            }

            if (!it->is_regular_file()) {
                continue;
            }

            const auto &abs = it->path();
            const auto rel = std::filesystem::relative(abs, docs_root);

            if (rel.empty()) {
                continue;
            }

            // Dotfiles and dot-directories (.git, .DS_Store) are never published.
            bool hidden = false;
            for (const auto &part: rel) {
                if (const auto s = part.string(); !s.empty() && s[0] == '.') {
                    hidden = true;
                    break;
                }
            }
            if (hidden) {
                continue;
            }

            // The extension is lowercased first so README.MD classifies like readme.md.
            const auto ext = abs.extension().string();
            std::string lower_ext = ext;
            std::ranges::transform(lower_ext, lower_ext.begin(),
                                   [](const unsigned char c) {
                                       return static_cast<char>(std::tolower(c));
                                   });

            if (lower_ext == ".md" || lower_ext == ".markdown") {
                result.markdown.push_back({.rel_path = rel.generic_string(), .abs_path = abs});
            } else {
                result.static_files.push_back({.rel_path = rel.generic_string(), .abs_path = abs});
            }
        }

        // Sort so builds are reproducible no matter what order the OS listed files in.
        const auto sort_rel = [](const auto &a, const auto &b) {
            return a.rel_path.generic_string() < b.rel_path.generic_string();
        };
        std::ranges::sort(result.markdown, sort_rel);
        std::ranges::sort(result.static_files, sort_rel);
        return result;
    }
}
