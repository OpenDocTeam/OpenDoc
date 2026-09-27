# OpenDoc Plugin Development Guide

The complete reference for packaging, installing, and writing OpenDoc plugins.

OpenDoc plugins are **drop-in folders** next to the executable, enabled from `opendoc.yaml`. Each plugin can observe the
full build lifecycle, ship a custom theme, and write extra artifacts.

## Table of contents

0. [Third-party plugins](#third-party-plugins)

1. [Overview](#1-overview)
2. [Install layout](#2-install-layout)
3. [plugin.json reference](#3-pluginjson-reference)
4. [Lifecycle events](#4-lifecycle-events)
5. [Build context API](#5-build-context-api)
6. [Themes from plugins](#6-themes-from-plugins)
7. [Writing a plugin](#7-writing-a-plugin)
8. [Building and installing](#8-building-and-installing)
9. [Built-in plugins](#9-built-in-plugins)
10. [Data types reference](#10-data-types-reference)
11. [Error handling](#11-error-handling)

---

## Third-party plugins

> [!CAUTION]
> Plugins are shared libraries loaded directly into your build process, so they run with your
> full user permissions. Only install plugins from authors you trust: a malicious plugin is
> effectively remote code execution on your machine.

No third-party plugins have been published yet.

---

## 1. Overview

### 1.1 What a plugin can do

| Capability                      | Event(s)                       | Example                           |
|---------------------------------|--------------------------------|-----------------------------------|
| Adjust config after YAML load   | `on_config`                    | Require or default `site_url`.    |
| Run once before page loop       | `on_pre_build`                 | Validate setup, prime caches.     |
| Transform raw Markdown          | `on_page_markdown`             | Inject banners into source.       |
| Transform the Markdown AST      | `on_page_content`              | Rewrite links, collect headings.  |
| Add/override template variables | `on_page_context`              | Custom `{{ my_var }}` in layouts. |
| Mutate final HTML               | `on_post_page`                 | Analytics, meta tags, comments.   |
| Write cross-page artifacts      | `on_post_build`                | Sitemaps, feeds, indexes.         |
| Ship a full custom theme        | `theme/` folder + `theme.name` | Layouts and CSS.                  |

### 1.2 How plugins are selected

1. Drop plugin folders into **`plugins/` next to the `opendoc` executable**.
2. List plugin **names** under `plugins:` in `opendoc.yaml`.
3. Only listed plugins load. Order in YAML = event execution order.

```yaml
plugins:
  - hello       # folder plugin (plugins/hello/)
  - search      # built-in
  - sitemap     # built-in
  - rss         # built-in
```

### 1.3 Duplicate-name rule

If **two folders** declare the same `"name"` in `plugin.json`, **none of those folders load**. Listing that name fails
the build with an explicit error.

A folder plugin that reuses a **built-in** name (`search`, `sitemap`, `rss`)
is also treated as conflicted (the built-in remains available).

### 1.4 Lifecycle in one diagram

```text
opendoc build
    │
    ├─ scan <exe>/plugins/*/plugin.json
    ├─ load plugins listed in opendoc.yaml (folder dlopen / built-in factory)
    ├─ on_config(config)                     # hook 1: SiteConfig mutable
    ├─ scan docs/ → SiteTree → validate
    ├─ on_pre_build(ctx)                     # hook 2: once
    ├─ load theme (default | plugin theme/)
    │
    ├─ for each Markdown page:
    │     on_page_markdown(ctx, page, raw)   # hook 3
    │     parse → copy front matter
    │     on_page_content(ctx, page, parsed) # hook 4: AST mutable
    │     .md link rewrite → HTML → plain text
    │     build TemplateContext
    │     on_page_context(ctx, page, tctx)   # hook 5: template vars
    │     render layout → full HTML
    │     on_post_page(ctx, page, html)      # hook 6: HTML mutable
    │     dead-link check → atomic write
    │
    ├─ copy static + theme assets
    └─ on_post_build(ctx)                    # hook 7: write artifacts
```

---

## 2. Install layout

```text
<directory containing opendoc executable>/
├── opendoc.exe            # or `opendoc`
├── libopendoc_sdk.dll     # shared SDK (Windows; .so/.dylib on Unix)
└── plugins/
    └── my-plugin/
        ├── plugin.json    # required metadata
        ├── my-plugin.dll  # shared library (.so / .dylib on Unix)
        └── theme/         # optional: custom theme
            ├── layout.html
            └── assets/…
```

| Piece          | Required | Notes                                                                                                                                      |
|----------------|:--------:|--------------------------------------------------------------------------------------------------------------------------------------------|
| `plugin.json`  |   Yes    | Name, description, version, author, optional github/library/theme.                                                                         |
| Shared library |   Yes    | `.dll` / `.so` / `.dylib` exporting the three C entry points. (`search`, `sitemap`, `rss` are compiled into the binary and need no folder) |
| `theme/`       |    No    | Only if `"theme"` is set or you want a theme named after the plugin.                                                                       |

Discovery uses the **executable’s directory** (`GetModuleFileNameW` /
`/proc/self/exe` / `_NSGetExecutablePath`), not the current working directory.

---

## 3. plugin.json reference

```json
{
  "name": "hello",
  "description": "Example OpenDoc plugin",
  "version": "1.0.0",
  "author": "OpenDoc",
  "github": "https://github.com/OpenDocTeam/OpenDoc",
  "library": "hello.dll",
  "theme": "hello"
}
```

| Field         | Required | Description                                                                                                                                                                                                      |
|---------------|:--------:|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `name`        |   Yes    | Unique id; must match `Plugin::name()` and the `plugins:` entry. No path separators.                                                                                                                             |
| `description` |    No    | One-line summary (shown in tooling / future UI).                                                                                                                                                                 |
| `version`     |    No    | Semver-style string for your plugin.                                                                                                                                                                             |
| `author`      |    No    | Author or organization.                                                                                                                                                                                          |
| `github`      |    No    | Repository URL.                                                                                                                                                                                                  |
| `library`     |    No    | Shared-library file name inside the folder. If omitted, OpenDoc tries `{name}.dll`, `lib{name}.dll`, `{name}.so`, `lib{name}.so`, `{name}.dylib`, `lib{name}.dylib`, then a single shared library in the folder. |
| `theme`       |    No    | Theme name registered by this plugin (files in `theme/`). Defaults to `name` when `theme/` exists.                                                                                                               |

> [!NOTE]
> `library` is optional and can be omitted entirely (as [`examples/plugin/plugin.json`](./examples/plugin/plugin.json)
> does). OpenDoc then probes the folder for the built library using the fallback order in the table above, and warns
> when no shared library can be found.

Invalid JSON or a missing `name` produces a discovery warning, and the folder is skipped.

---

## 4. Lifecycle events

Subclass `opendoc::plugins::Plugin` and override only the hooks you need.

```cpp
namespace opendoc::plugins {
class Plugin {
public:
    virtual ~Plugin() = default;
    [[nodiscard]] virtual std::string name() const = 0;

    virtual void on_config(mapper::SiteConfig &config);
    virtual void on_pre_build(BuildContext &ctx);
    virtual void on_page_markdown(BuildContext &ctx,
                                   const mapper::Page &page,
                                   std::string &markdown);
    virtual void on_page_content(BuildContext &ctx,
                                  const mapper::Page &page,
                                  parser::ParsedPage &parsed);
    virtual void on_page_context(BuildContext &ctx,
                                  const mapper::Page &page,
                                  theme::TemplateContext &template_context);
    virtual void on_post_page(BuildContext &ctx,
                              const mapper::Page &page,
                              std::string &html);
    virtual void on_post_build(BuildContext &ctx);
};
}
```

| Event              | When                                                                | Frequency | Mutable                 | Typical use           |
|--------------------|---------------------------------------------------------------------|:---------:|-------------------------|-----------------------|
| `on_config`        | After YAML load, **before** docs scan                               |   Once    | `SiteConfig&`           | Defaults, validation. |
| `on_pre_build`     | After SiteTree validate, before `site_dir` clean                    |   Once    | `BuildContext`          | Setup / scratch.      |
| `on_page_markdown` | After file read, **before** front-matter parse                      | Per page  | `std::string& markdown` | Source transforms.    |
| `on_page_content`  | After parse + front matter on `Page`, **before** `.md` link rewrite | Per page  | `ParsedPage&`           | AST edits.            |
| `on_page_context`  | TemplateContext fully built, before layout                          | Per page  | `TemplateContext&`      | New template vars.    |
| `on_post_page`     | Full HTML document, **before** dead-link check                      | Per page  | `std::string& html`     | Inject tags.          |
| `on_post_build`    | After all pages + assets written                                    |   Once    | `write_artifact`        | Feeds, indexes.       |

> [!NOTE]
> - Events run in **YAML `plugins:` order**.
> - `on_config` runs **before** the site tree exists. You cannot see pages yet. Use `on_pre_build` once pages are in the
    tree.
> - Dead-link checking runs **after** `on_post_page` (links you inject are checked; strict mode can fail the build).
> - `--watch` / `serve --watch` constructs a **new Builder** on every rebuild, so member state does not persist across
    cycles.

---

## 5. Build context API

```cpp
namespace opendoc::plugins {

class BuildContext {
public:
    const mapper::SiteConfig &config() const noexcept;
    mapper::SiteTree &site() const noexcept;
    const std::filesystem::path &site_root() const noexcept;

    void write_artifact(std::string_view rel_path, std::string content) const;

    std::map<std::string, std::string> &scratch(std::string_view plugin_name);
};

}
```

### `write_artifact`

```cpp
ctx.write_artifact("hello.txt", "report\n");
ctx.write_artifact("extra/robots.txt", "User-agent: *\nAllow: /\n");
```

- Relative paths only; absolute paths and `..` segments throw `PluginError`.
- Creates parent directories; overwrites existing files.
- Prefer this over opening files under `site_root` yourself.

### `scratch`

Per-plugin `map<string,string>` for the whole build (the same `BuildContext` is passed to all
seven events). Member variables on your plugin instance work too; the loader keeps one instance
alive for the entire build.

### `site()`

Mutable `SiteTree`: `pages()`, `nav_root()`, `find_by_url()`,
`find_by_rel_source()`. Useful in `on_post_build` for feeds/sitemaps.

---

## 6. Themes from plugins

### 6.1 Ship a theme

```text
plugins/my-theme-plugin/
├── plugin.json          # "theme": "material-lite"  (or omit to use plugin name)
├── my-theme-plugin.dll
└── theme/
    ├── layout.html      # required
    └── assets/
        └── extra.css    # any other files will be written under site_dir as-is
```

### 6.2 Enable it

```yaml
theme:
  name: material-lite

plugins:
  - my-theme-plugin      # required: theme resolves only from *active* plugin
```

If `theme.name` is not `default` and no active plugin provides that theme, OpenDoc will throw an error.

### 6.3 Layout contract

`layout.html` uses the same Mustache-like syntax as the default theme (`{{ var }}`, `{{{ raw }}}`, sections). Plugins
can add variables in `on_page_context`.

Every non-`layout.html` file under `theme/` is copied to `site_dir` with the **same relative path** (e.g.
`theme/assets/x.css` → `site/assets/x.css`).

`theme.custom_dir` still overlays the **default** theme only; it does not apply to plugin themes.

---

## 7. Writing a plugin

### 7.1 Minimal plugin

```cpp
// hello_plugin.cpp
#include <opendoc/plugins/build_context.hpp>
#include <opendoc/plugins/export.hpp>
#include <opendoc/plugins/plugin.hpp>

namespace {
class HelloPlugin final : public opendoc::plugins::Plugin {
public:
    [[nodiscard]] std::string name() const override { return "hello"; }

    void on_post_build(opendoc::plugins::BuildContext &ctx) override {
        ctx.write_artifact("hello.txt", "Hello from OpenDoc!\n");
    }
};
}

extern "C" {
OPENDOC_PLUGIN_EXPORT int opendoc_plugin_api_version() {
    return OPENDOC_PLUGIN_API_VERSION;
}
OPENDOC_PLUGIN_EXPORT opendoc::plugins::Plugin *opendoc_create_plugin() {
    return new HelloPlugin();
}
OPENDOC_PLUGIN_EXPORT void opendoc_destroy_plugin(opendoc::plugins::Plugin *p) {
    delete p;
}
}
```

`Plugin::name()` **must** equal `"name"` in `plugin.json`.

### 7.2 Event recipes

**Require config** (`on_config`):

```cpp
void on_config(opendoc::mapper::SiteConfig &config) override {
    if (config.site_url.empty())
        throw opendoc::plugins::PluginError(
            "hello plugin requires site_url in opendoc.yaml");
}
```

**Collect across pages** (members + `on_post_build`):

```cpp
void on_page_content(opendoc::plugins::BuildContext &,
                     const opendoc::mapper::Page &page,
                     opendoc::parser::ParsedPage &parsed) override {
    urls_.push_back(page.url);
    (void) parsed;
}

void on_post_build(opendoc::plugins::BuildContext &ctx) override {
    std::string body;
    for (const auto &u : urls_) body += u + "\n";
    ctx.write_artifact("urls.txt", body);
}
```

**Inject HTML** (`on_post_page`):

```cpp
void on_post_page(opendoc::plugins::BuildContext &,
                  const opendoc::mapper::Page &,
                  std::string &html) override {
    const auto pos = html.find("</body>");
    if (pos == std::string::npos) return;
    html.insert(pos, "<script src=\"https://example.com/a.js\"></script>\n");
}
```

**Template variable** (`on_page_context`):

```cpp
void on_page_context(opendoc::plugins::BuildContext &,
                     const opendoc::mapper::Page &,
                     opendoc::theme::TemplateContext &tctx) override {
    tctx.set("hello_plugin", "Hello from the OpenDoc example plugin");
}
```

Use in layout: `{{ hello_plugin }}`.

### 7.3 CMake

```cmake
add_library(hello_plugin SHARED hello_plugin.cpp)
target_link_libraries(hello_plugin PRIVATE opendoc_sdk)
target_compile_features(hello_plugin PRIVATE cxx_std_20)
set_target_properties(hello_plugin PROPERTIES
        OUTPUT_NAME "hello"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/hello"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/hello"
)
# copy plugin.json + theme/ into ${CMAKE_BINARY_DIR}/plugins/hello after build
```

Link **`opendoc_sdk`** (the shared library), not the per-module `opendoc_*` object libraries. The
plugin and the host must share one copy of RTTI, or `dynamic_cast` on AST nodes will fail across
the boundary.

---

## 8. Building and installing

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The example plugin stages itself here (Windows):

```text
build/
├── opendoc.exe
├── libopendoc_sdk.dll
└── plugins/hello/
    ├── hello.dll
    ├── plugin.json
    └── theme/…
```

Run from anywhere; discovery uses the executable directory:

```bash
./build/opendoc build -c path/to/opendoc.yaml
```

To **ship** a plugin: copy its folder into the `plugins/` directory beside the user’s `opendoc` binary and add the name
to their `plugins:` list.

---

## 9. Built-in plugins

Registered in software (no folder). Enable by name:

| Name      | Events                             | Output                                          |
|-----------|------------------------------------|-------------------------------------------------|
| `search`  | `on_page_content`, `on_post_build` | `search_index.json` (text capped at 4000 chars per section) |
| `sitemap` | `on_post_build`                    | `sitemap.xml`: soft-skips if `site_url` empty   |
| `rss`     | `on_post_build`                    | `rss.xml`: soft-skips if `site_url` empty       |

```yaml
site_url: https://example.com/docs
plugins:
  - search
  - sitemap
  - rss
```

---

## 10. Data types reference

### `mapper::Page`

```cpp
struct Page {
    std::string title;
    std::string description;
    std::filesystem::path abs_source;
    std::filesystem::path rel_source;
    std::filesystem::path abs_output;
    std::string url;
    std::vector<std::string> breadcrumbs;
    std::map<std::string, std::string> front_matter;
    std::string html_fragment;
    std::string plain_text;
};
```

In `on_page_markdown` / `on_page_content`, `html_fragment` and `plain_text`
are not final for that page. In `on_post_build`, all pages are complete.

### `parser::ParsedPage` / AST

```cpp
struct ParsedPage {
    FrontMatter front_matter;  // fields: map<string,string>; has/get
    DocumentNode document;     // root; children: NodeList
};
```

26 node types in [`opendoc/parser/ast.hpp`](./src/parser/include/opendoc/parser/ast.hpp)
(`HeadingNode`, `LinkNode`, `CodeBlockNode`, tables, lists, `MathNode`, `FootnoteRefNode`, …).
Walk with `dynamic_cast` or implement `AstVisitor`. Ownership: `unique_ptr<Node>` in `NodeList`.

### `theme::TemplateContext`

`set(key, value)`, `set_bool`, `set_sequence`: flat map of template values.

### `mapper::SiteConfig` (highlights)

| Field                                       | Use                                                 |
|---------------------------------------------|-----------------------------------------------------|
| `site_name`, `site_description`, `site_url` | Metadata; absolute URL prefix                       |
| `docs_dir`, `site_dir`                      | Paths (`ctx.site_root()` already resolved)          |
| `strict`                                    | Promote warnings to errors                          |
| `theme`                                     | `name`, `custom_dir`, `options`                     |
| `plugins`                                   | Active plugin list (already loaded when events run) |

There is no dedicated per-plugin YAML options block; use `theme.options`, front matter, or hardcode/extend `SiteConfig`
in a fork.

---

## 11. Error handling

Throw `opendoc::plugins::PluginError` for fatal problems. The builder wraps exceptions with stage context:

| Event              | Wrapper                                          |
|--------------------|--------------------------------------------------|
| load / discover    | raw `PluginError`                                |
| `on_config`        | `plugin on_config: <what>`                       |
| `on_pre_build`     | `plugin on_pre_build: <what>`                    |
| `on_page_markdown` | `plugin on_page_markdown: <what> [<rel_source>]` |
| `on_page_content`  | `plugin on_page_content: <what> [<rel_source>]`  |
| `on_page_context`  | `plugin on_page_context: <what> [<rel_source>]`  |
| `on_post_page`     | `plugin on_post_page: <what>`                    |
| `on_post_build`    | `plugin on_post_build: <what>`                   |

| Situation                         | Suggestion                                        |
|-----------------------------------|---------------------------------------------------|
| Missing required config           | Throw in `on_config`.                             |
| Optional feature needs `site_url` | Soft-skip (return) like `sitemap` / `rss`.        |
| Bad artifact path                 | `write_artifact` already throws.                  |
| Informational message             | No plugin logger; write a side artifact or throw. |
