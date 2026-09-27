(function () {
    "use strict";

    var input = document.getElementById("search-input");
    var results = document.getElementById("search-results");
    if (!input || !results) return;

    var base = typeof window.__OPEN_DOC_BASE__ === "string"
        ? window.__OPEN_DOC_BASE__ : "./";
    var index = null;
    var loading = null;
    var links = [];
    var active = -1;
    var lastQuery = null;

    var MAX_RESULTS = 12;

    // Coerces raw index entries so every expected field is present and typed.
    function normalizeIndex(data) {
        var list = Array.isArray(data) ? data : [];
        for (var i = 0; i < list.length; i++) {
            var e = list[i] || {};
            if (!Array.isArray(e.sections)) {
                e.sections = [{
                    anchor: "",
                    heading: "",
                    level: 0,
                    text: typeof e.text === "string" ? e.text : ""
                }];
            }
            if (!Array.isArray(e.keywords)) e.keywords = [];
            e.section = e.section || "";
            e.description = e.description || "";
            list[i] = e;
        }
        return list;
    }

    // Fetches search_index.json once, caching the promise so callers share it.
    function loadIndex() {
        if (index) return Promise.resolve(index);
        if (loading) return loading;
        loading = fetch(base + "search_index.json")
            .then(function (r) {
                if (!r.ok) throw new Error("HTTP " + r.status);
                return r.json();
            })
            .then(function (data) {
                index = normalizeIndex(data);
                return index;
            })
            .catch(function () {
                index = [];
                return index;
            })
            .finally(function () {
                loading = null;
            });
        return loading;
    }

    // Escapes the four characters that would otherwise break injected HTML.
    function escapeHtml(s) {
        return String(s)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;");
    }

    // Quotes regex metacharacters so a literal term can be matched safely.
    function escapeRegex(s) {
        return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    }

    // Lowercases a possibly missing value, always returning a string.
    function lower(s) {
        return String(s || "").toLowerCase();
    }

    /** All terms must appear somewhere on the page; quoted phrases are exact. */
    function parseQuery(raw) {
        var q = {terms: [], phrases: []};
        var rest = String(raw || "");
        rest = rest.replace(/"([^"]*)"/g, function (_, phrase) {
            var p = phrase.trim().toLowerCase();
            if (p) q.phrases.push(p);
            return " ";
        });
        var words = rest.toLowerCase().split(/\s+/);
        for (var i = 0; i < words.length; i++) {
            if (words[i]) q.terms.push(words[i]);
        }
        return q;
    }

    // True when the query has neither terms nor phrases to match.
    function queryIsEmpty(q) {
        return !q.terms.length && !q.phrases.length;
    }

    // Builds and caches the lowercase blob of all searchable text on a page.
    function haystackFor(entry) {
        if (entry._haystack) return entry._haystack;
        var parts = [entry.title || "", entry.section || "", entry.description || ""];
        for (var i = 0; i < entry.keywords.length; i++) parts.push(entry.keywords[i]);
        for (var j = 0; j < entry.sections.length; j++) {
            parts.push(entry.sections[j].heading || "", entry.sections[j].text || "");
        }
        entry._haystack = parts.join(" ").toLowerCase();
        return entry._haystack;
    }

    // Counts non-overlapping hits of needle, capped at five for cheap scoring.
    function countOccurrences(hay, needle) {
        if (!needle) return 0;
        var n = 0;
        var at = hay.indexOf(needle);
        while (at !== -1 && n < 5) {
            n++;
            at = hay.indexOf(needle, at + needle.length);
        }
        return n;
    }

    /** True when `term` sits on a word boundary (start of string or after non-alnum). */
    function boundaryIndex(hay, term) {
        var at = hay.indexOf(term);
        while (at !== -1) {
            if (at === 0 || !/[a-z0-9]/.test(hay.charAt(at - 1))) return at;
            at = hay.indexOf(term, at + term.length);
        }
        return -1;
    }

    // Scores one section, weighting heading matches above body matches.
    function scoreSection(section, q) {
        var head = lower(section.heading);
        var body = lower(section.text);
        var s = 0;
        var i;

        for (i = 0; i < q.terms.length; i++) {
            var t = q.terms[i];

            if (head) {
                if (head === t) s += 70;
                else if (head.indexOf(t) === 0) s += 48;
                else if (boundaryIndex(head, t) !== -1) s += 34;
                else if (head.indexOf(t) !== -1) s += 20;
            }

            var hits = countOccurrences(body, t);
            if (hits) {
                s += 10 + Math.min(hits - 1, 3) * 3;
                if (boundaryIndex(body, t) !== -1) s += 6;
            }
        }

        for (i = 0; i < q.phrases.length; i++) {
            if (head.indexOf(q.phrases[i]) !== -1) s += 55;
            if (body.indexOf(q.phrases[i]) !== -1) s += 30;
        }

        return s;
    }

    // Ranks one page against the query; returns null when any term is missing.
    function scoreEntry(entry, q) {
        var hay = haystackFor(entry);

        var i;
        for (i = 0; i < q.terms.length; i++) {
            if (hay.indexOf(q.terms[i]) === -1) return null;
        }
        for (i = 0; i < q.phrases.length; i++) {
            if (hay.indexOf(q.phrases[i]) === -1) return null;
        }

        var title = lower(entry.title);
        var section = lower(entry.section);
        var desc = lower(entry.description);
        var score = 0;

        for (i = 0; i < q.terms.length; i++) {
            var t = q.terms[i];

            if (title) {
                if (title === t) score += 120;
                else if (title.indexOf(t) === 0) score += 80;
                else if (boundaryIndex(title, t) !== -1) score += 55;
                else if (title.indexOf(t) !== -1) score += 30;
            }

            if (section && section.indexOf(t) !== -1) score += 22;
            if (desc && desc.indexOf(t) !== -1) score += 26;

            for (var k = 0; k < entry.keywords.length; k++) {
                var kw = lower(entry.keywords[k]);
                if (kw === t) score += 40;
                else if (kw.indexOf(t) === 0) score += 24;
            }
        }

        for (i = 0; i < q.phrases.length; i++) {
            var p = q.phrases[i];
            if (title.indexOf(p) !== -1) score += 90;
            if (desc.indexOf(p) !== -1) score += 40;
        }

        var best = null;
        var bestScore = -1;
        for (i = 0; i < entry.sections.length; i++) {
            var s = scoreSection(entry.sections[i], q);
            if (s > bestScore) {
                bestScore = s;
                best = entry.sections[i];
            }
        }
        if (!best) best = entry.sections[0];
        score += Math.max(bestScore, 0);

        /* Prefer shorter pages when relevance ties: a hit in a small page is
           a stronger signal than the same hit buried in a huge one. */
        score += Math.max(0, 24 - hay.length / 500);

        return {score: score, section: best};
    }

    // Runs the full query, returning the top hits sorted by descending score.
    function search(rawQuery) {
        var q = parseQuery(rawQuery);
        if (queryIsEmpty(q)) return [];
        lastQuery = q;

        var scored = [];
        for (var i = 0; i < index.length; i++) {
            var hit = scoreEntry(index[i], q);
            if (hit && hit.score > 0) {
                scored.push({score: hit.score, entry: index[i], section: hit.section});
            }
        }

        scored.sort(function (a, b) {
            if (b.score !== a.score) return b.score - a.score;
            return a.entry.title.localeCompare(b.entry.title);
        });
        return scored.slice(0, MAX_RESULTS);
    }

    // Cuts a window of text around the earliest match, with ellipses when trimmed.
    function snippetFor(text, q) {
        var src = String(text || "");
        if (!src) return "";

        var hay = src.toLowerCase();
        var at = -1;
        var needle = "";

        var candidates = q.phrases.concat(q.terms);
        for (var i = 0; i < candidates.length; i++) {
            var pos = hay.indexOf(candidates[i]);
            if (pos !== -1 && (at === -1 || pos < at)) {
                at = pos;
                needle = candidates[i];
            }
        }

        if (at === -1) {
            return highlight(src.slice(0, 160) + (src.length > 160 ? "…" : ""), q);
        }

        var start = Math.max(0, at - 55);
        var end = Math.min(src.length, at + needle.length + 110);
        if (start > 0) {
            var sp = src.indexOf(" ", start);
            if (sp !== -1 && sp < at) start = sp + 1;
        }
        var prefix = start > 0 ? "…" : "";
        var suffix = end < src.length ? "…" : "";
        return prefix + highlight(src.slice(start, end), q) + suffix;
    }

    // Wraps matches in <mark> after escaping, longest pattern first.
    function highlight(text, q) {
        var esc = escapeHtml(text);
        var patterns = [];
        var i;
        for (i = 0; i < q.phrases.length; i++) patterns.push(escapeRegex(escapeHtml(q.phrases[i])));
        for (i = 0; i < q.terms.length; i++) patterns.push(escapeRegex(escapeHtml(q.terms[i])));
        if (!patterns.length) return esc;

        patterns.sort(function (a, b) {
            return b.length - a.length;
        });
        try {
            return esc.replace(new RegExp("(" + patterns.join("|") + ")", "gi"),
                "<mark>$1</mark>");
        } catch (e) {
            return esc;
        }
    }

    // Builds a result URL, prefixing the site base and appending the anchor.
    function hrefFor(entry, section) {
        var href = entry.url || "/";
        if (href.charAt(0) === "/") href = base + href.slice(1);
        if (section && section.anchor) href += "#" + section.anchor;
        return href;
    }

    // Paints the hit list and resets keyboard selection to the first result.
    function render(hits, rawQuery) {
        links = [];
        active = -1;
        input.setAttribute("aria-expanded", "false");

        if (!hits.length) {
            results.innerHTML =
                '<div class="empty">No results for <strong>' +
                escapeHtml(rawQuery) + "</strong></div>";
            results.hidden = false;
            input.setAttribute("aria-expanded", "true");
            return;
        }

        var q = lastQuery || parseQuery(rawQuery);
        var html = "";

        for (var i = 0; i < hits.length; i++) {
            var entry = hits[i].entry;
            var section = hits[i].section || {};
            var context = [];
            if (entry.section) context.push(entry.section);
            if (entry.description) context.push(entry.description);

            html += '<a href="' + escapeHtml(hrefFor(entry, section)) + '">';
            if (context.length) {
                html += '<span class="result-context">' +
                    highlight(context.join(" · "), q) + "</span>";
            }
            html += '<span class="result-title">' + highlight(entry.title, q);
            if (section.heading && section.heading !== entry.title) {
                html += '<span class="result-crumb">' +
                    highlight(section.heading, q) + "</span>";
            }
            html += "</span>";

            var snippetSource = section.text || entry.description || "";
            if (snippetSource) {
                html += '<span class="result-snippet">' +
                    snippetFor(snippetSource, q) + "</span>";
            }
            html += "</a>";
        }

        html += '<div class="search-hint">' +
            "<span><kbd>↑</kbd><kbd>↓</kbd> navigate</span>" +
            "<span><kbd>↵</kbd> open</span>" +
            "<span><kbd>esc</kbd> close</span>" +
            "</div>";

        results.innerHTML = html;
        results.hidden = false;
        input.setAttribute("aria-expanded", "true");

        var nodes = results.querySelectorAll("a");
        for (var j = 0; j < nodes.length; j++) {
            links.push(nodes[j]);
        }
        setActive(links.length ? 0 : -1);
    }

    // Moves the keyboard cursor, keeping the highlighted result in view.
    function setActive(next) {
        if (active >= 0 && links[active]) {
            links[active].classList.remove("result-active");
            links[active].removeAttribute("aria-selected");
        }
        active = next;
        if (active >= 0 && links[active]) {
            links[active].classList.add("result-active");
            links[active].setAttribute("aria-selected", "true");
            links[active].scrollIntoView({block: "nearest"});
        }
    }

    // Clears the dropdown and resets ARIA state as if nothing was searched.
    function hide() {
        results.hidden = true;
        results.innerHTML = "";
        links = [];
        active = -1;
        input.setAttribute("aria-expanded", "false");
    }

    // Loads the index if needed and refreshes results for the current input.
    function run() {
        var raw = input.value.trim();
        if (!raw) {
            hide();
            return;
        }
        loadIndex().then(function (all) {
            if (!all.length) {
                results.innerHTML =
                    '<div class="empty">Search index unavailable for this site.</div>';
                results.hidden = false;
                return;
            }
            if (input.value.trim() !== raw) return;
            render(search(raw), raw);
        });
    }

    var timer = null;
    // Debounces typing so the index is not re-queried on every keystroke.
    input.addEventListener("input", function () {
        clearTimeout(timer);
        timer = setTimeout(run, 90);
    });

    // Warms the index on focus and re-runs an already populated query.
    input.addEventListener("focus", function () {
        loadIndex();
        if (input.value.trim()) run();
    });

    // Follows the pointer so the highlighted result tracks the mouse.
    results.addEventListener("mousemove", function (ev) {
        var link = ev.target.closest ? ev.target.closest("a") : null;
        if (!link) return;
        var i = links.indexOf(link);
        if (i !== -1 && i !== active) setActive(i);
    });

    // Maps arrow, Home, End, Enter, and Escape keys onto the result list.
    input.addEventListener("keydown", function (ev) {
        if (ev.key === "Escape") {
            ev.preventDefault();
            input.value = "";
            hide();
            input.blur();
        } else if (ev.key === "ArrowDown") {
            if (links.length) {
                ev.preventDefault();
                setActive((active + 1) % links.length);
            }
        } else if (ev.key === "ArrowUp") {
            if (links.length) {
                ev.preventDefault();
                setActive((active - 1 + links.length) % links.length);
            }
        } else if (ev.key === "Home" && links.length && results.contains(document.activeElement)) {
            ev.preventDefault();
            setActive(0);
        } else if (ev.key === "End" && links.length) {
            ev.preventDefault();
            setActive(links.length - 1);
        } else if (ev.key === "Enter") {
            var target = links[active] || links[0];
            if (target) {
                ev.preventDefault();
                window.location.href = target.getAttribute("href");
            }
        }
    });

    // Dismisses the dropdown when clicking outside it and the input.
    document.addEventListener("click", function (ev) {
        if (!results.hidden &&
            !results.contains(ev.target) &&
            ev.target !== input) {
            hide();
        }
    });

    // Closes the dropdown when focus moves somewhere unrelated.
    document.addEventListener("focusin", function (ev) {
        if (results.hidden) return;
        if (results.contains(ev.target) || ev.target === input) return;
        hide();
    });
})();
