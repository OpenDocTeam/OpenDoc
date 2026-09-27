# Contributing to OpenDoc

Thanks for your interest in contributing! This document explains how to set up a development
environment, the conventions the codebase follows, and what a good pull request looks like.

By participating, you agree to follow our [Code of Conduct](./CODE_OF_CONDUCT.md).

## Table of contents

1. [Ways to contribute](#1-ways-to-contribute)
2. [Development setup](#2-development-setup)
3. [Project layout](#3-project-layout)
4. [Building and verifying](#4-building-and-verifying)
5. [Coding conventions](#5-coding-conventions)
6. [Working on the Markdown parser](#6-working-on-the-markdown-parser)
7. [Working on the theme and assets](#7-working-on-the-theme-and-assets)
8. [Working on plugins](#8-working-on-plugins)
9. [Documentation](#9-documentation)
10. [Commits and pull requests](#10-commits-and-pull-requests)
11. [Reporting bugs](#11-reporting-bugs)
12. [License and AI disclosure](#12-license-and-ai-disclosure)

---

## 1. Ways to contribute

- **Bug reports**: See [Reporting bugs](#11-reporting-bugs).
- **Documentation**: [`README.md`](./README.md), [`GUIDE.md`](./GUIDE.md), and [`PLUGINS.md`](./PLUGINS.md) are all user-facing.
- **Bug fixes**: The issue tracker is the right place to claim a bug before starting.
- **Features**: Please open an issue first for anything larger than a small fix, so we can
  agree on the shape of the API before code is written.
- **Plugins and themes**: See [`PLUGINS.md`](./PLUGINS.md); third-party plugins are very welcome.

## 2. Development setup

### Requirements

| Dependency   | Version | Notes                                     |
|--------------|:-------:|-------------------------------------------|
| CMake        |  3.20+  | Build system generator                    |
| C++ compiler |  C++20  | GCC 10+, Clang 12+, or MSVC 19.29+        |
| yaml-cpp     |   any   | Configuration parsing                     |
| Ninja        |   any   | Recommended; `Make` also works            |

### Clone and configure

```bash
git clone https://github.com/OpenDocTeam/OpenDoc.git

cd OpenDoc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Use `Debug` while developing (`-g`, fast builds) and switch to `-DCMAKE_BUILD_TYPE=Release`
before benchmarking or shipping a build.

> [!NOTE]
> **Windows / MSYS2:** The project builds cleanly with the `ucrt64` toolchain. Point CMake at
> it explicitly if it is not already on `PATH`: <br>
> `-DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe`

## 3. Project layout

```text
OpenDoc/
├── src/
│   ├── mapper/     # Config, site model, navigation, link resolution, docs scanner
│   ├── parser/     # Markdown parser, AST, HTML renderer, PlainTextExtractor
│   ├── theme/      # Template engine + embedded default theme assets
│   ├── plugins/    # Plugin host, event dispatch, build context API
│   │   └── src/
│   │       ├── plugin_manager.cpp   # Discovery, dynamic loading, event dispatch
│   │       ├── build_context.cpp    # BuildContext::write_artifact, plugin scratch space
│   │       ├── manifest.cpp         # plugin.json parsing
│   │       ├── encoding.cpp         # UTF-8-safe JSON/XML escaping and truncation
│   │       ├── dynamic_loader.{hpp,cpp}
│   │       └── builtin/             # search, sitemap, rss + registration
│   └── app/        # CLI entry point, builder, logger, dev server
│       ├── builder.cpp              # Orchestration: scan → clean → render → copy → finish
│       ├── page_renderer.{hpp,cpp}  # One page: parse, resolve links, build context, render
│       ├── link_check.{hpp,cpp}     # Valid-target set + dead-link detection
│       ├── cli.cpp, main.cpp, log.cpp
│       └── serve/http_server.cpp
├── examples/
│   ├── site/       # Example documentation site (the de-facto regression fixture)
│   └── plugin/     # Minimal working plugin
├── assets/         # README images and screenshots
├── cmake/          # Toolchain files (cmake/toolchains/mingw-aarch64.cmake)
├── tools/          # fetch-sysroot.ps1, verify-pe.ps1
├── CMakeLists.txt  # Top-level build definition
├── ship.bat        # Windows packaging script
├── ship.sh         # POSIX packaging script
├── .gitignore      # Ignores build dirs, ship/, sysroot, example site output
├── README.md       # Project overview and quick start
├── GUIDE.md        # Full user guide (config, CLI, front matter)
├── PLUGINS.md      # Plugin and theme development guide
├── CODE_OF_CONDUCT.md  # Community standards
└── LICENSE         # MIT license
```

Targets produced by the build:

| Target           |   Kind   | Purpose                                          |
|------------------|:--------:|--------------------------------------------------|
| `opendoc`        |   exe    | CLI: `build`, `serve`, `--help`, `--version`     |
| `opendoc_sdk`    |  shared  | One copy of RTTI/vtables shared by exe + plugins |
| `opendoc_*`      |  object  | Per-module object libraries                      |

The shared SDK exists specifically so plugins loaded at runtime do not get a second copy of the
AST/RTTI tables. **Do not link plugin code directly against the object libraries**! Link against
`opendoc_sdk`, or you will get `dynamic_cast` failures across the plugin boundary.

## 4. Building and verifying

There is no automated test suite yet, so verification is a deliberate manual step. Keep the
build warning-clean and re-run the example site after your change.

```bash
# 1. Build
cmake --build build

# 2. Regenerate the example site
./build/opendoc build -c examples/site/opendoc.yaml

# 3. Inspect the output
# → examples/site/site/guide/markdown-example/index.html
```

[`examples/site/docs/guide/markdown-example.md`](./examples/site/docs/guide/markdown-example.md) is the closest thing this repository has to a
test suite: It exercises most Markdown features the parser supports. The notable gaps today are
hard line breaks, backslash escapes, `~~~` fences, and lazy continuation. **If you change
the parser, rebuild this page and check that the affected feature still renders correctly.**

### Definition of "done"

- [ ] `cmake --build build` succeeds with **zero warnings**.
- [ ] The example site rebuilds without errors.
- [ ] Any feature you touched is verified in the generated HTML, not just in the source.
- [ ] User-facing behavior changes are reflected in `README.md` / `GUIDE.md` / `PLUGINS.md`.
- [ ] No generated output is committed (see [Commit hygiene](#commit-hygiene)).

### Commit hygiene

`.gitignore` already covers `build/`, `cmake-build-*/`, `out/`, object files, IDE folders, `ship/`,
`tools/sysroot-aarch64/`, and the example site output (`examples/site/site`), so none of those
show up as changes:

```bash
git status --short          # should list only source and doc files you meant to touch
git diff --stat             # confirm you touched only what you meant to
```

## 5. Coding conventions

There is no `.clang-format` yet; the existing style is consistent and should be matched
literally.

### General

- **C++20**, `snake_case` for functions, variables, and file names; `PascalCase` for types.
- Member variables end with a trailing underscore (`opts_`, `out_`, `refs_`).
- Headers use `#pragma once`.
- File extensions: `.hpp` for headers, `.cpp` for sources.
- Prefer `std::string_view` and `std::span` for read-only parameters; pass by `const &` when a
  copy would otherwise be made.
- Use `[[nodiscard]]`, `noexcept`, and `const` wherever they genuinely apply.
- RAII everywhere: No manual `new`/`delete`, no raw owning pointers.
- Avoid exceptions for control flow; reserve them for unrecoverable errors (e.g. template parse
  failures).
- `auto` only when the type is obvious from context.

### Formatting

- 4-space indentation; no tabs.
- Namespaces are indented; opening brace goes on the same line as the declaration.
- One statement per line; blank line between functions.

```cpp
namespace opendoc::parser {
    bool BlockParser::try_heading(NodeList &out) {
        const std::string_view line = trim(lines_[pos_]);

        if (line.empty()) {
            return false;
        }

        return true;
    }
}
```

### Includes

Grouped and ordered; own header first, then project headers, then the standard library:

```cpp
#include "builder.hpp"

#include "log.hpp"

#include "opendoc/mapper/config.hpp"

#include <filesystem>
#include <string>
```

Standard-library headers go in a `<...>` block; use the full name (`<string_view>`, not
`<string_view.h>`).

### Warnings

The build enables `-Wall -Wextra -Wpedantic` (GCC/Clang) or `/W4 /permissive-` (MSVC), and does
**not** use `-Werror`, so a warning does not fail the build by itself. Policy: a new warning must be
fixed before merging; please do not silence warnings with casts or pragmas when a real fix is
available.

### Naming and API style

- Prefer free functions in a `detail` namespace over adding public API surface.
- Public headers live under `src/<module>/include/opendoc/<module>/`.
- Anything a plugin can see must be documented in [`PLUGINS.md`](./PLUGINS.md).

## 6. Working on the Markdown parser

The parser lives in [`src/parser/`](./src/parser):

```text
src/parser/
├── include/opendoc/parser/
│   ├── ast.hpp                         # Node types + AstVisitor
│   ├── front_matter.hpp                # Front-matter split + ParseError (with line no.)
│   ├── markdown_parser.hpp             # Public entry point, ParseOptions
│   ├── html_renderer.hpp
│   ├── syntax_highlight.hpp            # Build-time code-block highlighting
│   └── plain_text.hpp                  # AstVisitor that flattens a tree to text
└── src/
    ├── front_matter.cpp
    ├── markdown_parser.cpp
    ├── html_renderer.cpp
    ├── plain_text.cpp
    ├── syntax_highlight.cpp
    └── detail/
        ├── block_parser.{hpp,cpp}      # Block phase + link/footnote pre-pass
        ├── block_constructs.cpp        # Lists, tables, HTML blocks, definition lists
        ├── inline_parser.{hpp,cpp}     # Inline phase: links, emphasis, math
        └── text_utils.hpp              # Shared trim/split helpers
```

Key facts before you start:

- **Two phases.** `BlockParser::parse()` runs a pre-pass first (collecting `[label]: url`
  reference definitions and `[^label]:` footnote definitions, stripping those lines), then
  `parse_blocks()`, then the inline phase.
- **`AstVisitor` is the extension point.** Adding a node type means adding a `visit(...)`
  override to `AstVisitor`, `HtmlRenderer`, `PlainTextExtractor`, and any other implementer.
  Search for `AstVisitor` and `dynamic_cast` chains (`build_toc`, `split_sections`) before
  assuming a new node is harmless. Never hand-roll another "walk the tree and collect text"
  loop, `PlainTextExtractor` is that walk, and a second copy will disagree with it.
- **Sub-parsers must inherit state.** Inline sub-parsers are created via `make_sub()` so the
  reference map propagates. Forgetting this is a recurring source of "reference links work in
  the top paragraph but not inside a list item" bugs.
- **Block constructs are tried in a fixed order** inside `parse_blocks()`. A new construct
  handler must be added in the right position, or it will never be reached (or will shadow an
  existing one).
- Verify against the affected section of `markdown-example.md`, and ideally add the feature to
  that file so it stays the regression fixture.

## 7. Working on the theme and assets

Theme assets are **embedded at CMake configure time**, not compile time. `src/theme/CMakeLists.txt`
runs `file(READ)` over the assets and generates `theme_assets.hpp`. The asset files are registered
in `CMAKE_CONFIGURE_DEPENDS`, so editing one re-runs configure on the next build, and the generated
header is written through `configure_file(... COPYONLY)` so its timestamp (and therefore the
rebuild) only changes when the content actually changes.

> [!NOTE]
> No manual re-configure is needed: after editing `layout.html`, `css/base.css`, `js/theme.js`, or
> `js/search.js`, just rebuild and the fresh file is embedded automatically:
>
> ```bash
> cmake --build build
> ```

Template syntax gotchas (see [`src/theme/src/template_engine.cpp`](./src/theme/src/template_engine.cpp)):

| Syntax              | Meaning                                                      |
|---------------------|--------------------------------------------------------------|
| `{{{ name }}}`      | Raw, unescaped: **Must be one contiguous line**              |
| `{{ name }}`        | HTML-escaped                                                 |
| `{{# name }}` …     | Section: Rendered only if the variable is set **and** truthy |

Because `{{` is significant, template JavaScript must not contain brace pairs such as `{{`.
Use single-brace object literals, or move the script into `js/theme.js`.

CSS uses `var(--accent)` etc.; the palette override block lives near the top of `layout.html`
and is itself inside a template section, so a broken `{{ palette_primary }}` silently kills
every `--accent`-derived declaration (borders, backgrounds) while leaving layout intact; a
very confusing failure mode worth checking first when "styling stopped working".

Only pass values as `{{{ … }}}` when the builder has already produced markup for them (`content`,
`nav`, `toc`, `top_nav`, `versions`, `footer_columns`, `footer_text`). URLs, colors and titles go
through `{{ … }}`; `base_js` is the one pre-escaped exception (a JS string literal).

## 8. Working on plugins

Follow [`PLUGINS.md`](./PLUGINS.md). The essentials:

- A plugin is a directory with a `plugin.json` plus an entry point registered through the
  plugin host.
- [`examples/plugin/`](./examples/plugin) is the working reference: Build it and confirm the hook fires before
  writing your own.
- Plugin names must be unique: if two folders declare the same `name`, neither loads, and listing
  that name fails the build.
- Ship themes from plugins by setting `"theme"` in `plugin.json`.

## 9. Documentation

Documentation is part of the change, not a follow-up.

| File            | Audience            | Update when…                                        |
|-----------------|---------------------|-----------------------------------------------------|
| `README.md`     | First-time visitor  | Features, requirements, install, CLI surface change |
| `GUIDE.md`      | Users               | Config keys, CLI flags, Markdown support changes    |
| `PLUGINS.md`    | Plugin authors      | Plugin/theme API changes                            |

Conventions used across all three:

- Numbered top-level sections (`## 1. …`) with a `## Table of contents` list at the top.
- Reference tables for option/flag lists, fenced code blocks for every example.
- Link other docs relatively: `[the guide](./GUIDE.md#2-command-line-reference-cli)`.
  Anchors must match OpenDoc's slug rule, not GitHub's; see
  [`GUIDE.md` §4.3](./GUIDE.md#43-heading-anchors).

If you add a user-visible feature, add it to `GUIDE.md`'s Markdown support tables **and** to
`markdown-example.md` so it is exercised on every build.

## 10. Commits and pull requests

### Commits

- Small, focused commits: One logical change each, following [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/).
- Imperative subject line, ideally less than 100 characters: `fix(parser): parser throws error when (...)`, `docs(plugins): add new API event`
- Explain *why* in the body when the reason is not obvious from the diff.
- Do not mix unrelated reformatting into a functional change.

### Pull requests

1. Fork and branch from `main`. If maintainers are working on a development branch, branch from
   that instead; check the open PRs to find it.
2. Make sure the build is warning-free and the example site has been regenerated and checked.
3. Fill in the PR description with:
   - **What** changed and **why**.
   - **How you verified it**: The exact page/section you inspected.
   - Any follow-up work you deliberately left out.
4. Keep PRs reviewable: Prefer several small PRs over one large one.
5. Be responsive to review: Maintainers may ask for changes to match existing conventions.

## 11. Reporting bugs

A good report lets someone reproduce the bug without asking follow-up questions. Please include:

- OpenDoc version (`opendoc --version`) and how you built it (compiler, CMake flags, OS).
- The **exact** `opendoc.yaml` and the smallest Markdown snippet that reproduces the problem.
- What you expected, and what actually happened (paste the generated HTML if relevant).
- Whether `--strict` changes the outcome.

Search existing issues first and open one new issue per bug.

## 12. License and AI disclosure

This project is licensed under the [MIT License](./LICENSE). By contributing, you agree that
your contributions are licensed under the same terms.

Parts of this project were developed with AI assistance, and the project states this openly in
the [README.md](./README.md). Contributors are expected to hold their own work to the same standard: **review and
manually test anything you submit before opening a PR.** If AI assistance was substantial for
your contribution, mention it in the PR description. We would rather have transparency than
pretense.
