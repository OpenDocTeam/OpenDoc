# OpenDoc Guide

The complete reference for installing, configuring, and extending OpenDoc.

## Table of contents

1. [Getting started](#1-getting-started)
2. [Command line reference](#2-command-line-reference-cli)
3. [Configuration reference](#3-configuration-reference)
4. [Markdown support](#4-markdown-support)
5. [Front matter](#5-front-matter)
6. [Navigation](#6-navigation)
7. [Themes and templates](#7-themes-and-templates)
8. [Plugins](#8-plugins)
9. [Development server](#9-development-server)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Getting started

### 1.1 Prerequisites

Install the following before building from source:

| Tool         | Minimum                                 | Purpose                               |
|--------------|-----------------------------------------|---------------------------------------|
| CMake        | 3.20                                    | Configure the build                   |
| C++ compiler | C++20 (GCC 10+, Clang 12+, MSVC 19.29+) | Compile the source                    |
| yaml-cpp     | any                                     | Parse `opendoc.yaml` and front matter |
| Ninja        | any                                     | Recommended build generator           |

**MSYS2 (Windows):**

```bash
pacman -S mingw-w64-ucrt-x86_64-toolchain \
         mingw-w64-ucrt-x86_64-cmake \
         mingw-w64-ucrt-x86_64-ninja \
         mingw-w64-ucrt-x86_64-yaml-cpp
```

Make sure `C:\msys64\ucrt64\bin` appears **before** any other compiler (`LLVM`, bundled MinGW) on your `PATH`, or pass
the compiler explicitly when configuring (see [below](#12-clone-and-build)). A stale `build/CMakeCache.txt` keeps
whichever compiler was chosen first; delete `build/` when switching toolchains.

**Debian / Ubuntu:**

```bash
sudo apt install build-essential cmake ninja-build libyaml-cpp-dev
```

**macOS (Homebrew):**

```bash
brew install cmake ninja yaml-cpp
```

### 1.2 Clone and build

If Git is not installed yet, download it from [git-scm.com](https://git-scm.com/).

```bash
git clone https://github.com/OpenDocTeam/OpenDoc.git       # clone repo
cd OpenDoc                                                 # navigate to cloned repo's directory

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release    # configure
cmake --build build                                        # compile
```

To pin a specific compiler (useful when multiple toolchains are on `PATH`):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe
```

Outputs land in the `build/` root:

```text
build/
├── opendoc          # the generator (`opendoc.exe` on Windows, `opendoc` on Linux/macOS)
├── build.ninja
└── CMakeCache.txt
```

### 1.3 Your first site

Structure of your site:

```text
my-project/
├── opendoc.yaml
└── docs/
    └── index.md
```

#### Create `opendoc.yaml`:

This file is required: it is OpenDoc's configuration file.

```yaml
site_name: My Docs
docs_dir: docs
site_dir: site
nav:
  - Home: index.md
```

#### Create `docs/index.md`:

This is the entry point of your site.

```markdown
# Hello World!

This is my **first** page.
```

#### Build & serve

Use the following commands so OpenDoc will convert your Markdown files into HTML and then serve them locally on your
system:

```bash
cd my-project
opendoc build
opendoc serve --port 8080  # specific port if 8000 is already taken
```

Open <http://127.0.0.1:8080/> in your browser.

### 1.4 The bundled example

The repository ships a complete sample site under [`examples/site/`](examples/site):

```bash
cd examples/site
opendoc build
opendoc serve --port 8080
```

It demonstrates front matter, tables, nested navigation, link rewriting, plugins, and the most common configuration
keys (see [`examples/site/opendoc.yaml`](examples/site/opendoc.yaml); the full schema is listed in
[§3.1](#31-full-schema)).

---

## 2. Command line reference (CLI)

### 2.1 Synopsis

```text
opendoc build [options]     Build the site
opendoc serve [options]     Build and serve locally
opendoc --help              Show help
opendoc --version           Show version
```

### 2.2 Commands

| Command                | Description                                                                   |
|------------------------|-------------------------------------------------------------------------------|
| `build`                | Read config, parse `docs/`, write the static site to `site_dir`.              |
| `serve`                | Run an initial build (unless `--no-build`), then start the local HTTP server. |
| `--help`, `-h`, `help` | Print the help text and exit `0`.                                             |
| `--version`, `-V`      | Print version and build date, then exit `0`.                                  |

Unknown commands print an error to stderr and exit with code `2`.

### 2.3 Options

| Flag                  |  Applies to  |    Default     | Description                                                                                                                          |
|-----------------------|:------------:|:--------------:|--------------------------------------------------------------------------------------------------------------------------------------|
| `-c, --config <file>` |    global    | `opendoc.yaml` | Path to the configuration file. Relative paths inside the config resolve from the config file's directory.                           |
| `--strict`            |    global    |    `false`     | Promote build warnings (orphaned pages, failed asset copies, no Markdown found, dead links) to errors. Overrides the `strict:` key in YAML.      |
| `--quiet`, `-q`       |    global    |      off       | Suppress informational stdout (progress, stats). Errors still go to stderr.                                                          |
| `--host <addr>`       |    serve     |  `127.0.0.1`   | Bind address. `localhost` or an empty string is treated as `127.0.0.1`. Must be a valid IPv4 address.                                |
| `--port <port>`       |    serve     |     `8000`     | Listen port, `0`–`65535`. Port `0` binds an ephemeral port and prints the actual bound port. Invalid values exit `2`.                |
| `--no-build`          |    serve     |      off       | Skip the initial build and serve the existing `site_dir`. Errors if `site_dir` does not exist.                                       |
| `--watch`             | build, serve |      off       | Rebuild whenever the config file or any file under `docs/` changes (polls every 500 ms). `serve --watch` rebuilds in the background. |

### 2.4 Exit codes

| Code | Meaning                                                                                                |
|------|--------------------------------------------------------------------------------------------------------|
| `0`  | Success.                                                                                               |
| `1`  | Runtime, config, build error (`ConfigError`, `ValidationError`, `BuildError`, or unhandled exception). |
| `2`  | CLI usage error (unknown command or option, missing value, invalid port).                              |

### 2.5 Examples

```bash
# Standard build with default config
opendoc build

# Build with a custom config, strict mode, quiet output
opendoc build -c docs/opendoc.yaml --strict --quiet

# Serve on all interfaces, port 9000, no rebuild
opendoc serve --host 0.0.0.0 --port 9000 --no-build

# Rebuild on change while iterating on docs
opendoc build --watch

# Show version
opendoc --version
```

---

## 3. Configuration reference

All site settings live in a single YAML file, `opendoc.yaml` by default. The file must sit in the **same parent
directory as `docs/`** (or you point at it with `-c`). Every relative path in the config resolves from the config file's
own directory, not from the process working directory.

### 3.1 Full schema

```yaml
# Identity
site_name: string              # default: "OpenDoc"; must not be empty
site_description: string       # default: ""  → meta description / template var
site_url: string               # default: ""  → absolute base URL; enables sitemap.xml + rss.xml
logo: string                   # default: ""  → logo image path (site-root relative or absolute URL)
footer: string                 # default: ""  → custom footer text (HTML allowed)
favicon: string                # default: ""  → favicon path (site-root relative or absolute URL)
social_card: string            # default: ""  → og:image path (absolute when site_url is set)
edit_url: string               # default: ""  → base URL for "Edit this page" (source path appended)
github_url: string             # default: ""  → header GitHub icon URL (hidden when empty)
footer_columns: list           # default: []  → multi-column footer links
versions: list                 # default: []  → docs version switcher entries
current_version: string        # default: first entry; label of the active version

# Paths
docs_dir: docs                 # default: "docs"; Markdown root; must exist
site_dir: site                 # default: "site"; output root; cleaned every build

# Behavior
use_directory_urls: bool       # default: true
strict: bool                   # default: false; CLI --strict also sets this

# Theme
theme: default                 # scalar shorthand for theme.name
# or:
theme:
  name: default                # built-in, or a theme name provided by an active plugin
  custom_dir: ./my_theme       # optional overlay (default theme only)
  options: # consumed by the default theme (see 3.2)
    palette_primary: "#0070f3"
    show_toc: true
    show_edit_link: true
    show_prev_next: true
    show_reading_time: true

# Footer columns (optional; rendered above the fixed "Made with OpenDoc" line)
footer_columns:
  - title: Docs
    links:
      - text: Getting Started
        href: guide/getting-started/

# Version switcher (optional dropdown in the header)
versions:
  - label: v2.0
    url: ./
  - label: v1.0
    url: https://example.com/v1/
current_version: v2.0

# Navigation
nav: # omit entirely to auto-generate from folders
  - Home: index.md
  - Guide:
      - Getting Started: guide/getting-started.md
      - Advanced:
          - Markdown Example: guide/markdown-example.md

# Plugins
plugins:
  - search                      # built-in: writes search_index.json
  - sitemap                     # built-in: writes sitemap.xml (needs site_url)
  - rss                         # built-in: writes rss.xml (needs site_url)
```

### 3.2 Key reference

| Key                  |         Type         |   Default   | Description                                                                                                                                                                                                            |
|----------------------|:--------------------:|:-----------:|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `site_name`          |        string        | `"OpenDoc"` | Site title shown in the header and `<title>`. **Must not be empty.**                                                                                                                                                   |
| `site_description`   |        string        |    `""`     | Default meta description; overridable per page by front-matter `description`.                                                                                                                                          |
| `site_url`           |        string        |    `""`     | Absolute base URL of the deployed site (e.g. `https://example.com/docs`). Required for `sitemap.xml`. Leave empty to skip.                                                                                             |
| `logo`               |        string        |    `""`     | Logo image shown in the top nav. Site-root relative (e.g. `assets/logo.svg`, place the file under `docs/`) or an absolute URL. Empty → built-in docs icon.                                                             |
| `footer`             |        string        |    `""`     | Custom footer text (HTML allowed). Rendered left of the fixed, non-modifiable **"Made with OpenDoc"** mark.                                                                                                            |
| `favicon`            |        string        |    `""`     | Favicon path (site-root relative or absolute URL). Empty → browser default.                                                                                                                                            |
| `social_card`        |        string        |    `""`     | Social preview image for `og:image` / `twitter:image`. Made absolute when `site_url` is set.                                                                                                                           |
| `edit_url`           |        string        |    `""`     | Base URL for the "Edit this page" link; the page's source path is appended.                                                                                                                                            |
| `github_url`         |        string        |    `""`     | Repository URL for the header GitHub icon. Empty → icon hidden.                                                                                                                                                        |
| `footer_columns`     |     list of maps     |    `[]`     | Footer link columns: `{title, links: [{text, href}]}`. Rendered above the fixed footer mark.                                                                                                                           |
| `versions`           |     list of maps     |    `[]`     | Header version switcher: `{label, url}`.                                                                                                                                                                               |
| `current_version`    |        string        | first entry | Label of the selected version in the switcher.                                                                                                                                                                         |
| `docs_dir`           |         path         |   `docs`    | Markdown source root. Must exist and must not be empty.                                                                                                                                                                |
| `site_dir`           |         path         |   `site`    | Output root. **Cleaned on every build.** Must not equal or be nested inside `docs_dir`.                                                                                                                                |
| `use_directory_urls` |         bool         |   `true`    | `true`: `guide/setup` → `guide/setup/index.html`. `false`: `guide/setup` → `guide/setup.html`.                                                                                                                         |
| `strict`             |         bool         |   `false`   | Promote warnings to errors. Also settable with `--strict`.                                                                                                                                                             |
| `theme`              |    string \| map     |  `default`  | Scalar form is shorthand for `theme.name`.                                                                                                                                                                             |
| `theme.name`         |        string        |  `default`  | Built-in `default`, or a theme name provided by an **active plugin**. Otherwise throws `ThemeError` at build time.                                                                                                     |
| `theme.custom_dir`   |         path         |      —      | Overlay directory for the **default** theme only. Files present there replace the built-in `layout.html`, `css/base.css`, `js/theme.js`, and `js/search.js`. Optional; missing or empty files keep the built-in asset. |
| `theme.options`      | map\<string,string\> |      —      | Consumed by the default theme: `palette_primary` (accent CSS color), `show_toc`, `show_edit_link`, `show_prev_next` (booleans); `show_reading_time` (default `true`) enables the `reading_time` variable.              |
| `nav`                |     nested list      |    auto     | Explicit sidebar. Omit to derive from the `docs/` folder tree. See [Navigation](#6-navigation).                                                                                                                        |
| `plugins`            |   list of strings    |    `[]`     | Plugin names to load, in order: built-ins (`search`, `sitemap`, `rss`) and folder plugins under `<exe>/plugins/`. Unknown or duplicate names fail the build.                                                           |

### 3.3 Validation rules

OpenDoc fails the build with a clear `ConfigError` / `ValidationError` when:

- `opendoc.yaml` is missing or fails YAML parsing.
- `site_name`, `docs_dir`, or `site_dir` is empty.
- `docs_dir` does not exist.
- `site_dir` equals `docs_dir`, or is nested inside it.
- A `nav` entry references a file that does not exist under `docs/`.
- Two pages map to the same output URL (e.g. `guide.md` + `guide/index.md`).
- A `plugins:` entry is unknown or listed twice.
- `theme.name` is not `default` and no active plugin provides that theme.

With `strict: true` (or `--strict`), these additional conditions become errors instead of warnings:

- Pages not reachable from an explicit `nav` (orphans).
- Failed static-asset copies.
- No Markdown files found under `docs/`.
- Dead links (an `href`/`src` that does not resolve to a generated page or asset).

---

## 4. Markdown support

OpenDoc implements CommonMark core constructs plus GitHub Flavored Markdown (GFM) extras. All GFM extras are **enabled
by default** (`ParseOptions`:
`gfm_tables`, `gfm_task_lists`, `gfm_strikethrough`, `raw_html`).

### 4.1 Block elements

| Element         | Syntax                                      | Notes                                                                                            |
|-----------------|---------------------------------------------|--------------------------------------------------------------------------------------------------|
| ATX headings    | `#` … `######`                              | Closing hashes stripped.                                                                         |
| Setext headings | `Text` + `===` / `---`                      | Detected inside paragraphs.                                                                      |
| Thematic break  | `---`, `***`, `___`                         | ≥ 3 characters.                                                                                  |
| Fenced code     | ` ``` ` / `~~~` + info string               | Info string → `class="language-…"`; highlighted at build time ([§4.8](#48-syntax-highlighting)). |
| Indented code   | 4 spaces or a tab                           | Standard CommonMark.                                                                             |
| Blockquote      | `>`                                         | Lazy continuation; nested blocks via recursive parse.                                            |
| Bullet list     | `-`, `+`, `*`                               | Tight/loose detection; nested items.                                                             |
| Ordered list    | `1.`, `1)`                                  | Custom `start`; CommonMark content-indent rules.                                                 |
| Task list       | `- [ ]` / `- [x]`                           | → `<li class="task-list-item"><input type="checkbox" disabled…>`.                                |
| Pipe table      | `\| a \| b \|` + delimiter row              | Alignment `:--`, `--:`, `:-:` → inline `text-align`; `\|` escapes a pipe.                        |
| Raw HTML block  | `<div>…`, `<!-- -->`, doctype, script, etc. | Large allowlist of block tags.                                                                   |
| Reference defs  | `[label]: url "title"`                      | Stripped in a pre-pass; used by inline links.                                                    |
| Footnote def    | `[^label]: text` (+ indented continuations) | Collected in the pre-pass; see [§4.7](#47-footnotes).                                            |
| Definition list | `Term` + `: Definition`                     | → `<dl>` / `<dt>` / `<dd>`; blank-line groups supported.                                         |

### 4.2 Inline elements

| Element          | Syntax                                         | Output                                                 |
|------------------|------------------------------------------------|--------------------------------------------------------|
| Emphasis         | `*em*` / `_em_`                                | `<em>`                                                 |
| Strong           | `**strong**` / `__strong__`                    | `<strong>`                                             |
| Strikethrough    | `~~text~~`                                     | `<del>`                                                |
| Code span        | `` `code` ``                                   | Multi-backtick matching; space-trimmed per CommonMark. |
| Link             | `[text](url "title")`                          | Nested brackets supported; `<url>` angle form too.     |
| Image            | `![alt](src "title")`                          |                                                        |
| Autolink         | `<https://…>`, `<mailto:…>`                    | Schemes: `http`, `https`, `ftp`, `mailto`.             |
| Bare email       | `user@example.com`                             | → `mailto:` link.                                      |
| Raw inline HTML  | `<tag attr="…">`                               | When `raw_html` is on.                                 |
| Hard break       | two trailing spaces + newline, or `\<newline>` | → `<br />`.                                            |
| Soft break       | single newline                                 | Keeps `\n`.                                            |
| Backslash escape | `\*`, `\_`, `\#`, etc.                         | Extensive escapable set.                               |
| Superscript      | `<sup>` / `<sub>`                              | Rendered as raw HTML; math also covers `x^2` / `H_2O`. |
| Footnote ref     | `[^label]`                                     | → `<sup class="footnote-ref">`, numbered on first use. |
| Reference link   | `[text][label]`, `[text][]`, `[label]`         | Shortcut, full and collapsed forms.                    |
| Reference image  | `![alt][label]`                                | Same resolution as reference links.                    |
| Inline math      | `$tex$`                                        | → `<span class="math math-inline">\(tex\)</span>`.     |
| Display math     | `$$tex$$`                                      | → `<div class="math math-display">\[tex\]</div>`.      |

### 4.3 Heading anchors

Every heading gets a stable `id` derived from its text:

- Lowercased; letters and digits are kept. Spaces, `-`, and `_` become a single `-`. **Every other
  character (`.`, `&`, `(`, `)`, `#`, …) is dropped entirely**; it does not become a dash. Runs of
  separators collapse to one `-`, and trailing `-` is trimmed.
- Duplicates get `-1`, `-2`, … suffixes (`Setup` → `setup`, `setup-1`).

Examples: `## Getting Started` → `<h2 id="getting-started">Getting Started</h2>` and
`## 2. Text Formatting & Styling` → `<h2 id="2-text-formatting-styling">`.

> [!WARNING]
> Because `.` and `&` are dropped rather than replaced, hand-written anchors in your own Markdown
> must match this rule; GitHub-style anchors (`#2-text-formatting--styling`) will not resolve.

### 4.4 Link rewriting

Internal links ending in `.md` / `.markdown` are rewritten to the target page's site URL based on `use_directory_urls`
and the source page's depth:

```markdown
[Configuration](configuration.md)     <!-- resolved relative to current page -->
[Home](../index.md)
![Logo](../assets/logo.svg)           <!-- asset paths normalized -->
[External](https://example.com)       <!-- untouched -->
[Section](#my-heading)                <!-- fragment-only, untouched -->
```

### 4.5 Not currently supported

- **Markdown toggles in `opendoc.yaml`.** The parser options (`gfm_tables`, `gfm_task_lists`,
  `gfm_strikethrough`, `raw_html`) are fixed at build time and all default to on, so there is no
  `markdown:` block in the configuration file.

### 4.6 Math rendering

Math is written as `$tex$` for inline and `$$tex$$` for display, and is emitted as
KaTeX-compatible delimiters (`\(...\)` inline, `\[...\]` display).

The default layout loads KaTeX and its auto-render extension from the jsDelivr CDN; **but only on
pages that actually contain math**. The build sets a `has_math` template variable by scanning the
rendered HTML for `class="math"`, so pages without math never fetch KaTeX.

> [!NOTE]
> Because KaTeX comes from the CDN, math requires an internet connection when the page is viewed.
> To self-host it, override `layout.html` with a `theme.custom_dir` overlay (see
> [§7.2](#72-selecting-and-overriding-the-theme)) and point the stylesheet/script tags at local
> copies.

### 4.7 Footnotes

Write a reference as `[^label]` and its definition as `[^label]: text` further down the page.
Definitions may continue over several lines when each continuation is indented.

References are numbered in order of first use (not document order), so numbering stays stable
regardless of where the definitions live. Definitions that are never referenced are not rendered.
Each rendered definition ends with a back-link to its first reference.

### 4.8 Syntax highlighting

Fenced code blocks are highlighted **at build time**; no client-side highlighter is needed. The
info string after the opening fence selects the language:

```javascript
function greet(name) {
    return `Hello, ${name}`;
}
```

Recognized tags (aliases in parentheses):

| Group                | Info string tags                                                                       |
|----------------------|----------------------------------------------------------------------------------------|
| C / C++              | `c`, `cpp`, `c++`, `cc`, `cxx`, `h`, `hpp`, `hh`                                       |
| Java / C#            | `java`, `cs`, `csharp`                                                                 |
| JavaScript / TS      | `js`, `javascript`, `ts`, `typescript`, `jsx`, `tsx`, `mjs`, `node`                    |
| Python               | `py`, `python`, `python3`                                                              |
| Shell                | `bash`, `sh`, `shell`, `zsh`, `console`, `shellsession`                                |
| YAML                 | `yaml`, `yml`                                                                          |
| JSON                 | `json`, `json5`                                                                        |
| SQL                  | `sql`, `mysql`, `postgres`, `sqlite`                                                   |
| Rust                 | `rust`, `rs`                                                                           |
| Go                   | `go`, `golang`                                                                         |
| Markup               | `xml`, `html`, `svg`, `toml`                                                           |

Tokens are emitted as `<span class="tok-…">` for keywords (`tok-k`), strings (`tok-s`), comments
(`tok-c`), numbers (`tok-n`), and function names (`tok-f`); colors come from the matching
`--tok-*` custom properties in `css/base.css`. An unrecognized info string leaves the block as
plain, unhighlighted `<code>`.

---

## 5. Front matter

Any Markdown file may begin with a YAML front-matter block:

```markdown
---
title: Custom page title
description: Used for <meta name="description"> and search.
author: Jane Doe
---

# Heading still works
```

Rules:

- Delimiters: opening `---` (exactly three dashes) and closing `---` or `...` at the start of the file. BOM and leading
  blank lines are tolerated.
- The block is parsed as YAML. Sequences flatten to `"a, b"`; maps flatten to
  `"k=v"` for scalar template access.
- Malformed YAML raises a `ParseError` with the line number.
- A missing closing delimiter is treated as ordinary Markdown (not an error).
- `title` and `description` flow into the theme (page title, meta tag).
- **Every other scalar key** is also exposed as a Mustache variable in the layout (built-in keys win on conflict).

---

## 6. Navigation

### 6.1 Automatic navigation

Omit `nav` from `opendoc.yaml` and the folder tree under `docs/` becomes the sidebar, in deterministic sorted order.
Hidden files/directories (leading `.`)
are skipped. `index.md` files name their parent directory section.

### 6.2 Explicit navigation

Declare order, labels, and nesting yourself:

```yaml
nav:
  - Home: index.md
  - Guide:
      - Getting Started: guide/getting-started.md
      - Configuration: guide/configuration.md
      - Advanced:
          - Markdown Example: guide/markdown-example.md
```

Rules:

- A leaf is `- Label: path/to/page.md`.
- A section is `- Label:` followed by an indented nested list.
- A bare scalar path (no label) uses the page's derived title.
- Labels in `nav` **override** titles derived from filenames/front matter.
- Only listed pages appear in the sidebar, in the declared order.
- In `strict` mode, pages not reachable from `nav` are errors (orphans).

### 6.3 Rendered nav

The theme receives nav as a nested `<ul class="nav-list">` with:

- Relative `href`s (respecting `base` prefix and directory URLs).
- `active` / `aria-current="page"` on the current page.
- `has-children` classes on sections; non-active subtrees at depth > 0 are hidden by default (expanded via the sidebar
  UI).

---

## 7. Themes and templates

### 7.1 Built-in theme

Only one theme ships with OpenDoc: **`default`**. It is embedded in the binary at configure time (via a generated
`theme_assets.hpp`) and consists of:

```text
layout.html          # page shell (head, header, sidebar, article, footer)
css/base.css         # responsive styles, light/dark via prefers-color-scheme
js/theme.js          # nav toggle, mobile search overlay, code-copy buttons
js/search.js         # client-side search against search_index.json
```

### 7.2 Selecting and overriding the theme

```yaml
theme: default                 # scalar shorthand

# or with an overlay:
theme:
  name: default
  custom_dir: ./my_theme
```

Any `theme.name` other than `default` must be provided by an **active plugin**
(see [`PLUGINS.md`](./PLUGINS.md)); otherwise:

```text
unknown theme 'X' (available: default). If a plugin provides this theme, list it
under plugins: in opendoc.yaml.
```

`custom_dir` is an **overlay for the default theme only**: any of these files present in that directory replace the
built-in counterpart:

```text
my_theme/
├── layout.html       # full page shell
├── css/
│   └── base.css      # stylesheet
└── js/
    ├── theme.js      # behavior
    └── search.js     # search client
```

Each file you provide is an optional override: files that are missing or empty are skipped, and the built-in asset is
used instead.

### 7.3 Template syntax (Mustache-like)

The layout is rendered with a small Mustache-compatible engine:

| Syntax                    | Meaning                                                                                                          |
|---------------------------|------------------------------------------------------------------------------------------------------------------|
| `{{ name }}`              | HTML-escaped scalar. Missing key → empty string (lenient).                                                       |
| `{{{ name }}}`            | Raw (unescaped) HTML.                                                                                            |
| `{{# name }}…{{/ name }}` | Truthy string/bool → render once with scope. Sequence → iterate (each item becomes scope). Missing/false → skip. |
| `{{^ name }}…{{/ name }}` | Inverted section (render when missing/false/empty).                                                              |
| `{{! comment }}`          | Comment; skipped.                                                                                                |
| `{{& name}}`              | Alternate raw form (treated as comment/skipped in this engine).                                                  |

Structural errors (unclosed/mismatched sections, empty names) throw
`TemplateError` with a line number.

### 7.4 Template variables

Provided by the builder on every page:

| Variable                  |  Type   | Description                                                                                            |
|---------------------------|:-------:|--------------------------------------------------------------------------------------------------------|
| `site_name`               | escaped | From config.                                                                                           |
| `site_description`        | escaped | From config.                                                                                           |
| `site_url`                | escaped | From config; section only when set.                                                                    |
| `logo`                    | escaped | Logo `src` URL (base-prefixed); section only when `logo:` is set in config.                            |
| `favicon`                 | escaped | Favicon URL (base-prefixed); section only when set.                                                    |
| `social_card`             | escaped | Absolute (when `site_url` set) social image URL; section only when set.                                |
| `palette_primary`         | escaped | Accent color from `theme.options.palette_primary`; drives a `:root` CSS override.                      |
| `title`                   | escaped | Page title (front matter → filename → nav label).                                                      |
| `description`             | escaped | Page meta description.                                                                                 |
| `content`                 | **raw** | Rendered Markdown HTML fragment.                                                                       |
| `nav`                     | **raw** | Sidebar `<ul>` HTML (with collapse carets).                                                            |
| `top_nav`                 | **raw** | Horizontal header `<ul>` of top-level nav entries; section only when non-empty.                        |
| `github_url`              | escaped | Header GitHub icon link; section only when `github_url:` is set.                                       |
| `toc`                     | **raw** | Right-rail TOC `<ul>` HTML; section only when ≥ 2 h2/h3 headings (and `show_toc`).                     |
| `reading_time`            | escaped | e.g. `3 min read`; available unless `show_reading_time` is disabled (not shown in the default layout). |
| `edit_url`                | escaped | Full "Edit this page" URL; section only when `edit_url:` is set.                                       |
| `prev_title` / `prev_url` | escaped | Previous page in nav order; sections only when present.                                                |
| `next_title` / `next_url` | escaped | Next page in nav order; sections only when present.                                                    |
| `versions`                | **raw** | Version `<select>` HTML; section only when `versions:` is set.                                         |
| `footer_columns`          | **raw** | Footer columns HTML; section only when `footer_columns:` is set.                                       |
| `base`                    | escaped | Relative prefix to site root (`""`, `../`, `../../`, …).                                               |
| `base_js`                 | raw JS  | `base` as a complete, JS-escaped `"…"` string literal, for embedding inside a `<script>` block.        |
| `url`                     | escaped | This page's site URL.                                                                                  |
| `generator`               | escaped | e.g. `OpenDoc 1.0.0`.                                                                                  |
| `has_math`                |  bool   | `true` when the rendered page contains math; gates the KaTeX CDN tags in the default layout.           |
| `footer_text`             | **raw** | Custom footer text from `footer:`; section only when set.                                              |
| `breadcrumbs`             | escaped | Plain text `A / B / C`; section only when non-empty.                                                   |
| *(front-matter scalars)*  | escaped | Every other scalar key from the page's front matter.                                                   |

Built-in keys win over front-matter keys of the same name. The full reserved list, 
`site_name`, `site_description`, `site_url`, `title`, `description`, `content`, `nav`, `top_nav`,
`toc`, `base`, `base_js`, `url`, `logo`, `favicon`, `social_card`, `footer_text`, `footer_columns`,
`versions`, `breadcrumbs`, `reading_time`, `edit_url`, `prev_*`, `next_*`, `palette_primary`,
`github_url`, `generator`, `has_math`, is ignored in front matter, so a page cannot blank out the
layout or point every asset at an outside URL. Use a differently named key instead.

`{{{ … }}}` is reserved for values the builder has already HTML-escaped by hand because they are
genuinely markup (`content`, `nav`, `toc`, `top_nav`, `versions`, `footer_columns`, `footer_text`).
Anything else, URLs, colors, titles, should be written as `{{ … }}`.

### 7.5 Default layout slots

The shipped `layout.html` exposes these regions:

- `<head>` — charset, viewport, description + Open Graph/Twitter meta, conditional favicon, title, stylesheet, optional
  palette override, tiny theme-bootstrap script.
- Header — hamburger (mobile), logo or docs icon + site name link (left), optional version switcher, search box, theme
  toggle (right).
- Sidebar — `{{{ nav }}}` (categories and their pages, collapsible).
- Breadcrumbs — conditional `{{# breadcrumbs }}`.
- Main — `<article class="page-content">{{{ content }}}`, optional
  "Edit this page" link, prev/next pager.
- Right TOC rail — conditional `{{# toc }}` (scroll-spy "On This Page").
- Footer — optional `{{# footer_columns }}`, optional `{{# footer_text }}`, plus the fixed **"Made with OpenDoc"**
  mark.
- Scripts — sets `window.__OPEN_DOC_BASE__ = {{{ base_js }}};`, loads `theme.js` and `search.js`.

---

## 8. Plugins

Plugins are **drop-in folders** next to the `opendoc` executable and are enabled from `opendoc.yaml`. Full developer
reference: **[`PLUGINS.md`](./PLUGINS.md)**.

### 8.1 Install and enable

```text
<exe dir>/plugins/
└── my-plugin/
    ├── plugin.json      # name, description, version, author, github, …
    ├── my-plugin.dll    # .so / .dylib on Unix
    └── theme/           # optional custom theme (layout.html + assets)
```

```yaml
plugins:
  - my-plugin   # folder plugin
  - search      # built-in
  - sitemap
  - rss
```

Only listed plugins load; YAML order = event order. **If two folders declare the same `name`, none of them load**
(listing that name fails the build).

### 8.2 Lifecycle events

| Event              | When                              | Typical use                   |
|--------------------|-----------------------------------|-------------------------------|
| `on_config`        | After YAML load, before docs scan | Validate/adjust `SiteConfig`. |
| `on_pre_build`     | After site tree exists            | One-time setup.               |
| `on_page_markdown` | Raw Markdown before parse         | Source transforms.            |
| `on_page_content`  | After AST parse                   | Rewrite AST nodes.            |
| `on_page_context`  | Template context ready            | Custom `{{ vars }}`.          |
| `on_post_page`     | Full HTML, before write           | Inject scripts/meta.          |
| `on_post_build`    | After all files written           | Cross-page artifacts.         |

### 8.3 Built-in plugins

```yaml
plugins:
  - search     # writes search_index.json
  - sitemap    # writes sitemap.xml (requires site_url)
  - rss        # writes rss.xml (requires site_url)
```

`search` indexes each page during `on_page_content` and writes
`search_index.json` on `on_post_build` (text capped at 4000 chars per section). `sitemap`
and `rss` write on `on_post_build` and **soft-skip when `site_url` is empty**.

### 8.4 Build context API

```cpp
ctx.config();                 // SiteConfig (read-only)
ctx.site();                   // SiteTree (mutable nav/pages)
ctx.site_root();              // resolved site_dir path
ctx.write_artifact("robots.txt", "User-agent: *\n");
ctx.scratch("my-plugin");     // std::map<std::string,std::string> for the build
```

`write_artifact` allows only **relative** paths under `site_dir` (no absolute paths, no `..`).

### 8.5 Writing your own

1. Create `plugins/my-plugin/` with `plugin.json` and a shared library linked against `opendoc_sdk`.
2. Subclass `opendoc::plugins::Plugin`, implement `name()`, override events.
3. Export `opendoc_plugin_api_version`, `opendoc_create_plugin`, `opendoc_destroy_plugin`.
4. List the name under `plugins:` in `opendoc.yaml`.

See [`PLUGINS.md`](./PLUGINS.md) and the working example at
[`examples/plugin/`](./examples/plugin). Themes ship in the plugin’s `theme/` folder and activate with `theme.name` +
the plugin in
`plugins:`.

---

## 9. Development server

```bash
opendoc serve [options]
```

`serve` runs a full build first (unless `--no-build`), then starts a local HTTP server rooted at `site_dir`.

### 9.1 Behavior

- Single accept thread serving requests one at a time over HTTP/1.1 (Winsock on Windows, BSD sockets on POSIX), with a
  200 ms `select()` timeout so shutdown stays responsive.
- **GET** and **HEAD** only; other methods return `405`.
- Pretty URLs: `/guide/setup` serves `guide/setup.html` or
  `guide/setup/index.html` automatically.
- Path normalization with traversal protection (`..` cannot escape the root).
- Standard MIME types; built-in static `404` page.
- Graceful shutdown on **Ctrl+C** (SIGINT / SIGTERM on POSIX).
- Port `0` binds an ephemeral port and prints the actual bound port.

### 9.2 Common invocations

```bash
# Default: build, then serve at 127.0.0.1:8000
opendoc serve

# Custom port
opendoc serve --port 8080

# LAN access
opendoc serve --host 0.0.0.0 --port 9000

# Serve an already-built site without rebuilding
opendoc serve --no-build --port 8080

# Quiet serve (CI / scripted)
opendoc serve --quiet --port 8080
```

---

## 10. Troubleshooting

### CMake picks the wrong compiler

**Symptom:** `clang++.exe` from `C:\Program Files\LLVM` is selected and linking fails with
`could not open 'kernel32.lib'`.

**Cause:** LLVM appears earlier on `PATH` than MSYS2, and/or a stale
`build/CMakeCache.txt` remembers the first compiler chosen.

**Fix:** delete `build/` and reconfigure with explicit paths:

```bash
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe
```

Or put `C:\msys64\ucrt64\bin` before `C:\Program Files\LLVM\bin` on `PATH`. When switching toolchains, always delete
`build/CMakeCache.txt` (or the whole
`build/` directory).

### Error: `Could not find a package configuration file provided by "yaml-cpp"`

Install the development package for your toolchain (see
[Prerequisites](#11-prerequisites)), or pass its prefix:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
```

### Build fails: `site_dir` is inside `docs_dir`

`site_dir` must not equal or nest inside `docs_dir`. Use separate trees:

```yaml
docs_dir: docs
site_dir: site    # ✅ sibling
# site_dir: docs/site   # ❌ rejected
```

### Error: `unknown theme 'X'`

Only `default` ships with OpenDoc. Either use `theme.custom_dir` to overlay layout/CSS/JS on `default`, or provide the theme from a plugin (see [Plugins](#8-plugins) and [`PLUGINS.md`](./PLUGINS.md)) and list that plugin under `plugins:`.

### Error: `unknown plugin 'X'`

Built-ins are `search`, `sitemap`, and `rss`. Folder plugins must live under `<exe>/plugins/<id>/` with a valid `plugin.json`. The name must appear under `plugins:` in `opendoc.yaml`. Conflicts (two folders with the same `name`, or a folder shadowing a built-in) fail the build when listed.

### Sitemap is missing

`sitemap` requires a non-empty `site_url`:

```yaml
site_url: https://example.com/docs
plugins:
  - sitemap
```

### Page titles look wrong

Resolution order: front-matter `title` → `nav` label → derived from filename (`guide/getting-started` →
`Getting Started`). A `nav` label always wins over the filename-derived title when both exist for display in the
sidebar; front-matter `title` wins for `<title>`.

### Search box does nothing

Client-side search needs both pieces: the `search` plugin (writes
`search_index.json`) and the built-in `assets/js/search.js` (shipped with the default theme). Enable the plugin and
rebuild:

```yaml
plugins:
  - search
```

If results still do not appear, confirm `search_index.json` exists next to your pages' `assets/` folder and that the
browser can fetch it (no blocked
`fetch` / wrong base path).
