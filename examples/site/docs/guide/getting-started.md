---
title: Getting Started
description: Create a site and run your first build.
---

# Getting Started

## 1. Create a project

```text
my-docs/
├── opendoc.yaml
└── docs/
    └── index.md
```

## 2. Configure

```yaml
site_name: My Docs
docs_dir: docs
site_dir: site
nav:
  - Home: index.md
```

## 3. Write Markdown

```markdown
# Hello

This is my first page.
```

## 4. Build and preview

```bash
opendoc build
opendoc serve --port 8080
```

Open <http://127.0.0.1:8080/> in your browser.
