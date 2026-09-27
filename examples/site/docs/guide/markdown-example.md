---
title: Markdown Example
description: Example of Markdown usage.
---

# Comprehensive Markdown Reference Guide

Welcome! This document is a reference sheet showcasing the standard and extended Markdown formats that OpenDoc supports.

---

## 1. Headers

# Heading Level 1 (`#`)
## Heading Level 2 (`##`)
### Heading Level 3 (`###`)
#### Heading Level 4 (`####`)
##### Heading Level 5 (`#####`)
###### Heading Level 6 (`######`)

Alternative Heading Syntax (Setext style):

Alt Heading Level 1
====================

Alt Heading Level 2
-----------------

---

## 2. Text Formatting & Styling

* **Bold text** using double asterisks (`**`) or __double underscores__ (`__`).
* *Italic text* using single asterisks (`*`) or _single underscores_ (`_`).
* ***Bold and italic text*** using triple asterisks (`***`).
* ~~Strikethrough text~~ using double tildes (`~~`).
* <u>Underlined text</u> using HTML tags (`<u>`).
* `Inline code` using backticks (`` ` ``).
* <mark>Highlighted text</mark> using HTML `<mark>` tags.
* Superscript text: $x^2$ (or HTML `<sup>2</sup>`).
* Subscript text: $H_2O$ (or HTML `<sub>2</sub>`).

---

## 3. Blockquotes

Standard blockquote:
> Markdown is a lightweight markup language with plain-text-formatting syntax. Its design allows it to be readable as-is, unlike some languages adorned with tags.

Nested blockquotes:
> This is the first level of quoting.
>> This is a nested blockquote (second level).
>>> This is a third-level nested quote with **bold formatting** inside.

---

## 4. Lists

### Ordered Lists
1. First item
2. Second item
    1. Sub-item A
    2. Sub-item B
3. Third item

### Unordered Lists
* Item one
* Item two
    * Sub-item one
    * Sub-item two
- Item three (using dash)
+ Item four (using plus)

### Task Lists (Checkboxes)
- [x] Completed task item
- [ ] Incomplete task item
- [ ] Another pending item to complete

### Definition Lists (Markdown extension)
Term 1
: Definition 1 for term 1

Term 2
: Definition 2 for term 2

---

## 5. Links and Images

### Links
* Inline link: [OpenAI Website](https://openai.com)
* Link with a title: [Google Search](https://www.google.com "The world's leading search engine")
* Reference-style link: [Reference Link Target][ref-id]
* Auto-linked URL: <https://www.github.com>

[ref-id]: https://www.markdown.org "Official Markdown Website"

### Images
* Inline image with alt text:
  ![Markdown Logo](https://markdown-here.com/img/icon256.png)
* Reference-style image:
  ![Alt Text Reference][img-ref]

[img-ref]: https://markdown-here.com/img/icon256.png "Markdown Logo Title"

---

## 6. Code Blocks

### Indented Code Block (4 spaces indentation)
    function sayHello() {
        console.log("Hello, world!");
    }

### Fenced Code Block (JavaScript)
```javascript
// A simple JavaScript function
function calculateSum(a, b) {
    return a + b;
}
console.log(calculateSum(5, 10));
```

### Fenced Code Block (Python)
```python
def fibonacci(n):
    a, b = 0, 1
    for _ in range(n):
        yield a
        a, b = b, a + b

print(list(fibonacci(10)))
```

---

## 7. Tables

| Header 1 (Left) | Header 2 (Center) | Header 3 (Right) |
|:----------------|:-----------------:|-----------------:|
| Cell 1          |      Cell 2       |           Cell 3 |
| Row 2, Col 1    |   Row 2, Col 2    |     Row 2, Col 3 |
| Markdown works  | **inside** tables |           `too!` |

---

## 8. Horizontal Rules

You can create horizontal dividers using three or more asterisks, dashes, or underscores:

***

---

___

---

## 9. Footnotes and Citations

Here is a sentence with a footnote reference[^1]. Here is another reference[^note].

[^1]: This is the text of the first footnote.
[^note]: Footnotes can contain multiple lines and `code` blocks or formatting.

---

## 10. Advanced / Extended Features

### Emoji Support
* Thumbs up: 👍
* Rocket launch: 🚀
* Smiling face: 😀
* Check mark: ✅

### Table of Contents

* [Comprehensive Markdown Reference Guide](#comprehensive-markdown-reference-guide)
    * [1. Headers](#1-headers)
    * [2. Text Formatting & Styling](#2-text-formatting-styling)
    * [3. Blockquotes](#3-blockquotes)
    * [4. Lists](#4-lists)
    * [5. Links and Images](#5-links-and-images)
    * [6. Code Blocks](#6-code-blocks)
    * [7. Tables](#7-tables)
    * [8. Horizontal Rules](#8-horizontal-rules)
    * [9. Footnotes and Citations](#9-footnotes-and-citations)
    * [10. Advanced / Extended Features](#10-advanced-extended-features)

### Details / Disclosure Block (HTML `<details>`)
<details>
<summary>Click here to expand hidden content!</summary>

Surprise! This content was hidden inside a collapsible accordion element using standard HTML tags embedded within Markdown.

</details>

### Mathematical Notation (LaTeX style)
* Inline math: $E = mc^2$
* Block math:
  $$\int_{0}^{\infty} e^{-x^2} dx = \frac{\sqrt{\pi}}{2}$$
