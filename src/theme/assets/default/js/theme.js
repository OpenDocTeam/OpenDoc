(function () {
    "use strict";

    var header = document.getElementById("site-header");
    if (header) {
        // Marks the header as condensed once the page has scrolled a little.
        var onScroll = function () {
            if (window.scrollY > 4) header.classList.add("is-scrolled");
            else header.classList.remove("is-scrolled");
        };
        window.addEventListener("scroll", onScroll, {passive: true});
        onScroll();
    }

    var toggle = document.getElementById("nav-toggle");
    var sidebar = document.getElementById("sidebar");
    var backdrop = document.getElementById("sidebar-backdrop");

    // Opens or closes the mobile drawer, keeping ARIA state in sync.
    function setSidebarOpen(open) {
        if (!sidebar || !toggle) return;
        sidebar.classList.toggle("open", open);
        toggle.setAttribute("aria-expanded", open ? "true" : "false");
        if (backdrop) {
            if (open) backdrop.removeAttribute("hidden");
            else backdrop.setAttribute("hidden", "");
        }
    }

    if (toggle && sidebar) {
        // The header button flips the drawer on small screens.
        toggle.addEventListener("click", function () {
            setSidebarOpen(!sidebar.classList.contains("open"));
        });
    }
    if (backdrop) {
        // Tapping the dimmed overlay dismisses the drawer.
        backdrop.addEventListener("click", function () {
            setSidebarOpen(false);
        });
    }

    // Expands or collapses a branch, deferring the hidden attribute until the
    // collapse transition finishes (with a timeout fallback if it never fires).
    function toggleNavSection(li, caret) {
        var children = li.querySelector(":scope > .nav-children");
        if (!children) return;
        var isOpen = li.classList.contains("expanded") && !children.hasAttribute("hidden");
        if (isOpen) {
            li.classList.remove("expanded");
            var onEnd = function (e) {
                if (e.target !== children) return;
                if (!li.classList.contains("expanded")) {
                    children.setAttribute("hidden", "");
                }
                children.removeEventListener("transitionend", onEnd);
            };
            children.addEventListener("transitionend", onEnd);
            setTimeout(function () {
                if (!li.classList.contains("expanded")) {
                    children.setAttribute("hidden", "");
                }
            }, 300);
            if (caret) caret.setAttribute("aria-expanded", "false");
        } else {
            children.removeAttribute("hidden");
            void children.offsetHeight;
            li.classList.add("expanded");
            if (caret) caret.setAttribute("aria-expanded", "true");
        }
    }

    // The caret button toggles its own branch without following any link.
    document.querySelectorAll(".nav-caret").forEach(function (caret) {
        caret.addEventListener("click", function (ev) {
            ev.preventDefault();
            ev.stopPropagation();
            var li = caret.closest(".nav-item");
            if (li) toggleNavSection(li, caret);
        });
    });

    // Clicking a branch label toggles that branch as well.
    document.querySelectorAll(".nav-label").forEach(function (label) {
        label.addEventListener("click", function () {
            var li = label.closest(".nav-item.has-children");
            if (!li) return;
            var caret = li.querySelector(":scope > .nav-row > .nav-caret");
            toggleNavSection(li, caret);
        });
    });

    // Scroll the sidebar so the active item is visible on load.
    var activeItem = document.querySelector(".nav-item.active");
    if (activeItem && sidebar) {
        var itemTop = activeItem.offsetTop;
        var viewTop = sidebar.scrollTop;
        var viewBottom = viewTop + sidebar.clientHeight;
        if (itemTop < viewTop || itemTop + activeItem.offsetHeight > viewBottom) {
            sidebar.scrollTop = itemTop - sidebar.clientHeight / 3;
        }
    }

    // Appends a Copy button to each code block that writes it to the clipboard.
    document.querySelectorAll("pre > code").forEach(function (code) {
        var pre = code.parentElement;
        if (!pre) return;
        var btn = document.createElement("button");
        btn.type = "button";
        btn.className = "code-copy";
        btn.textContent = "Copy";
        btn.addEventListener("click", function () {
            var text = code.textContent || "";
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(text).then(function () {
                    btn.textContent = "Copied!";
                    setTimeout(function () {
                        btn.textContent = "Copy";
                    }, 1500);
                });
            }
        });
        pre.appendChild(btn);
    });

    // Labels the search shortcut with the platform's modifier key.
    var kbd = document.getElementById("search-kbd");
    if (kbd) {
        var isMac = /Mac|iPhone|iPad|iPod/.test(
            navigator.platform || navigator.userAgent || ""
        );
        kbd.textContent = isMac ? "⌘K" : "Ctrl K";
    }

    // Reveals the header search box on narrow screens and focuses the input.
    var searchBtn = document.getElementById("search-toggle");
    var searchBox = document.getElementById("header-search");
    if (searchBtn && searchBox) {
        searchBtn.addEventListener("click", function () {
            var open = searchBox.classList.toggle("open");
            if (open) {
                var input = document.getElementById("search-input");
                if (input) input.focus();
            }
        });
    }

    // Global shortcut: Ctrl/Cmd+K always, "/" only when not typing in a field.
    document.addEventListener("keydown", function (ev) {
        var t = ev.target;
        var typing =
            t &&
            (t.tagName === "INPUT" ||
                t.tagName === "TEXTAREA" ||
                t.isContentEditable);

        var isCmdK =
            (ev.metaKey || ev.ctrlKey) && (ev.key === "k" || ev.key === "K");
        var isSlash = ev.key === "/" && !ev.ctrlKey && !ev.metaKey && !ev.altKey;

        if (!isCmdK && !isSlash) return;
        if (isSlash && typing) return;
        ev.preventDefault();

        var box = document.getElementById("header-search");
        var input = document.getElementById("search-input");
        if (box && window.matchMedia("(max-width: 767px)").matches) {
            box.classList.add("open");
        }
        if (input) {
            input.focus();
            input.select();
        }
    });

    // Flips light/dark mode and persists the choice for later visits.
    var themeBtn = document.getElementById("theme-toggle");
    if (themeBtn) {
        themeBtn.addEventListener("click", function () {
            var root = document.documentElement;
            var current = root.getAttribute("data-theme");
            if (!current) {
                current = window.matchMedia("(prefers-color-scheme: dark)").matches
                    ? "dark"
                    : "light";
            }
            var next = current === "dark" ? "light" : "dark";
            root.setAttribute("data-theme", next);
            try {
                localStorage.setItem("opendoc-theme", next);
            } catch (e) { /* ignore */
            }
        });
    }

    var versionDropdown = document.getElementById("version-dropdown");
    if (versionDropdown) {
        var versionTrigger = versionDropdown.querySelector(".version-trigger");
        var versionMenu = versionDropdown.querySelector(".version-menu");
        if (versionTrigger && versionMenu) {
            // Hides the menu and resets the trigger's expanded state.
            var closeVersionMenu = function () {
                versionMenu.setAttribute("hidden", "");
                versionTrigger.setAttribute("aria-expanded", "false");
            };
            // Reveals the menu and marks the trigger as expanded.
            var openVersionMenu = function () {
                versionMenu.removeAttribute("hidden");
                versionTrigger.setAttribute("aria-expanded", "true");
            };
            // Clicking the trigger toggles; stop propagation keeps the outside
            // click listener from closing it immediately.
            versionTrigger.addEventListener("click", function (ev) {
                ev.stopPropagation();
                if (versionMenu.hasAttribute("hidden")) openVersionMenu();
                else closeVersionMenu();
            });
            // Choosing a version navigates to that version's URL.
            versionMenu.querySelectorAll(".version-option").forEach(function (opt) {
                opt.addEventListener("click", function (ev) {
                    ev.preventDefault();
                    ev.stopPropagation();
                    var url = opt.getAttribute("data-url");
                    closeVersionMenu();
                    if (url) window.location.href = url;
                });
            });
            // Clicks elsewhere or Escape dismiss the open version menu.
            document.addEventListener("click", function (ev) {
                if (!versionDropdown.contains(ev.target)) closeVersionMenu();
            });
            // Escape closes the version menu wherever focus currently is.
            document.addEventListener("keydown", function (ev) {
                if (ev.key === "Escape") closeVersionMenu();
            });
        }
    }

    var tocToggle = document.getElementById("toc-toggle");
    var tocBody = document.getElementById("toc-body");
    if (tocToggle && tocBody) {
        // Toggles the collapsible table of contents panel.
        tocToggle.addEventListener("click", function () {
            var open = tocBody.classList.toggle("is-open");
            tocToggle.setAttribute("aria-expanded", open ? "true" : "false");
        });

        // A deep link opens the panel so the linked heading is reachable.
        if (window.location.hash) {
            tocBody.classList.add("is-open");
            tocToggle.setAttribute("aria-expanded", "true");
        }
    }

    var tocLinks = Array.prototype.slice.call(
        document.querySelectorAll(".toc-list a")
    );
    if (tocLinks.length) {
        var idToLink = {};
        var targets = [];
        // Pairs each TOC link with its heading, dropping links with no target.
        tocLinks.forEach(function (link) {
            var id = decodeURIComponent((link.getAttribute("href") || "").slice(1));
            if (!id) return;
            var el = document.getElementById(id);
            if (!el) return;
            idToLink[id] = link;
            targets.push(el);
        });

        var activeTocId = null;

        // Highlights the last heading that scrolled past the header marker.
        function updateTocActive() {
            if (!targets.length) return;
            var headerH = header ? header.offsetHeight : 0;
            var marker = headerH + 28;
            var best = targets[0].id;

            for (var i = 0; i < targets.length; i++) {
                if (targets[i].getBoundingClientRect().top <= marker) {
                    best = targets[i].id;
                } else {
                    break;
                }
            }

            if (best === activeTocId) return;
            activeTocId = best;
            tocLinks.forEach(function (l) {
                l.classList.remove("active");
            });
            if (idToLink[best]) idToLink[best].classList.add("active");
        }

        var tocTicking = false;

        // Throttles scroll handling to at most one update per frame.
        function onTocScroll() {
            if (tocTicking) return;
            tocTicking = true;
            requestAnimationFrame(function () {
                updateTocActive();
                tocTicking = false;
            });
        }

        window.addEventListener("scroll", onTocScroll, {passive: true});
        window.addEventListener("resize", onTocScroll);
        updateTocActive();
    }
})();
