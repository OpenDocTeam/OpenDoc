#pragma once

#include <filesystem>
#include <vector>

namespace opendoc::mapper {
    // A Markdown source found under docs_dir, in both relative and absolute form.
    struct ScannedDoc {
        std::filesystem::path rel_path;
        std::filesystem::path abs_path;
    };

    // A non-Markdown file that is copied to the output site unchanged.
    struct ScannedStatic {
        std::filesystem::path rel_path;
        std::filesystem::path abs_path;
    };

    // Everything a directory walk turned up, sorted by relative path.
    struct ScanResult {
        std::vector<ScannedDoc> markdown;
        std::vector<ScannedStatic> static_files;
    };

    // Walks docs_dir once and classifies each regular file as markdown or static.
    class DocsScanner {
    public:
        // Throws filesystem_error if docs_root is missing or not a directory.
        [[nodiscard]] static ScanResult scan(const std::filesystem::path &docs_root);
    };

    // True for Windows reparse points (junctions, symlinks); always false elsewhere.
    [[nodiscard]] bool is_reparse_point(const std::filesystem::path &path);
}
