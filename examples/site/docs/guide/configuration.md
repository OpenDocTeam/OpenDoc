---
title: Configuration
description: Common opendoc.yaml options and navigation.
---

# Configuration

Settings live in `opendoc.yaml` next to `docs/`.

## Common options

```yaml
site_name: My Docs
site_description: Short meta description
site_url: https://example.com/docs         # this enables sitemap.xml and rss.xml

docs_dir: docs
site_dir: site

theme:
  name: default
  options:
    palette_primary: "#0070f3"
    show_toc: true
    show_prev_next: true

plugins:
  - search
  - sitemap
  - rss
```

## Navigation

Declare order and labels with `nav`:

```yaml
nav:
  - Home: index.md
  - Guide:
      - Getting Started: guide/getting-started.md
      - Configuration: guide/configuration.md
```

- Only listed pages appear in the sidebar.
- Nav labels override titles derived from filenames.
- Omit `nav` entirely to build the sidebar from the `docs/` folder tree.

## Front matter

```markdown
---
title: Page title
description: Meta description for this page.
---

# Heading
```
