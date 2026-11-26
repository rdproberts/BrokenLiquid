#!/usr/bin/env python3
"""Preview README.md as a GitHub repository front page, offline.

    python scripts/preview_readme.py            # writes build/readme-preview.html
    python scripts/preview_readme.py --open     # ...and opens it in the default browser

The page imitates GitHub's layout (file list, README box, About sidebar) and its Markdown
rendering: headings with anchors, paragraphs, emphasis, links, images, lists, task lists, tables,
fenced code, block quotes and alerts, and raw HTML such as <p align="center">. Local images are
embedded and shields.io badges are drawn locally, so the file works without a network.

It is a close approximation, not GitHub's real renderer: check the live page after pushing.
Edit ABOUT and TOPICS below to try out the repository description shown in the sidebar.
"""

from __future__ import annotations

import argparse
import base64
import html
import mimetypes
import re
import subprocess
import sys
import webbrowser
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent

# The repository "About" box. Set the same text on GitHub (Settings or `gh repo edit`).
ABOUT = (
    "Free 3D CAD for Windows that runs on your own machine. Sketch, model parts, build assemblies "
    "and make drawings, with no account and no subscription."
)
TOPICS = [
    "cad", "3d-cad", "parametric-cad", "mechanical-engineering", "opencascade", "qt6", "cpp20",
    "windows", "constraint-solver", "engineering-drawing", "step", "offline-first",
]

# --------------------------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------------------------


def git(*args: str) -> str:
    try:
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                              encoding="utf-8", check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def esc(text: str) -> str:
    return html.escape(text, quote=True)


def slug(text: str) -> str:
    """GitHub heading anchor: lower case, punctuation dropped, spaces to hyphens."""
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # [text](url) -> text
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


BADGE_COLOURS = {
    "brightgreen": "#4c1", "green": "#97ca00", "yellowgreen": "#a4a61d", "yellow": "#dfb317",
    "orange": "#fe7d37", "red": "#e05d44", "blue": "#007ec6", "grey": "#555", "gray": "#555",
    "lightgrey": "#9f9f9f", "lightgray": "#9f9f9f", "success": "#4c1", "important": "#fe7d37",
    "critical": "#e05d44", "informational": "#007ec6", "inactive": "#9f9f9f",
}


def text_width(text: str) -> float:
    """Rough width of 11 px Verdana, which shields.io badges use."""
    width = 0.0
    for ch in text:
        if ch in "il.,:;|!'`":
            width += 3.4
        elif ch in " ()[]-/":
            width += 4.2
        elif ch.isupper() or ch in "mwMW%@#":
            width += 8.2
        else:
            width += 6.8
    return width


def badge(url: str) -> str:
    """Draw a static shields.io badge (/badge/label-message-colour) as an SVG data URI."""
    raw = url.split("/badge/", 1)[1].split("?", 1)[0]
    raw = re.sub(r"\.svg$", "", raw)
    parts = [unquote(p.replace("\x01", "-").replace("__", "\x02").replace("_", " ").replace("\x02", "_"))
             for p in raw.replace("--", "\x01").split("-")]
    if len(parts) >= 3:
        label, message, colour = parts[0], "-".join(parts[1:-1]), parts[-1]
    elif len(parts) == 2:
        label, message, colour = "", parts[0], parts[1]
    else:
        label, message, colour = "", parts[0], "lightgrey"
    colour = BADGE_COLOURS.get(colour.lower(), "#" + colour if re.fullmatch(r"[0-9a-fA-F]{3,8}", colour) else colour)
    lw = round(text_width(label) + 12) if label else 0
    mw = round(text_width(message) + 12)
    w = lw + mw

    def label_text(x: float, s: str) -> str:
        return (f'<text x="{x}" y="15" fill="#010101" fill-opacity=".3">{esc(s)}</text>'
                f'<text x="{x}" y="14">{esc(s)}</text>')

    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="20">'
        '<linearGradient id="s" x2="0" y2="100%"><stop offset="0" stop-color="#bbb" stop-opacity=".1"/>'
        '<stop offset="1" stop-opacity=".1"/></linearGradient>'
        f'<clipPath id="r"><rect width="{w}" height="20" rx="3" fill="#fff"/></clipPath>'
        f'<g clip-path="url(#r)"><rect width="{lw}" height="20" fill="#555"/>'
        f'<rect x="{lw}" width="{mw}" height="20" fill="{esc(colour)}"/>'
        f'<rect width="{w}" height="20" fill="url(#s)"/></g>'
        '<g fill="#fff" text-anchor="middle" font-family="Verdana,Geneva,DejaVu Sans,sans-serif" font-size="11">'
        + (label_text(lw / 2, label) if label else "") + label_text(lw + mw / 2, message) + "</g></svg>"
    )
    return "data:image/svg+xml;base64," + base64.b64encode(svg.encode("utf-8")).decode("ascii")


# --------------------------------------------------------------------------------------------
# Markdown
# --------------------------------------------------------------------------------------------

LIST_ITEM = re.compile(r"^( {0,12})([-*+]|\d{1,9}[.)])( +|$)(.*)$")
FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})\s*([\w+#.-]*)")
HEADING = re.compile(r"^ {0,3}(#{1,6})(?:\s+(.*?))?\s*#*\s*$")
HR = re.compile(r"^ {0,3}(?:(?:\*\s*){3,}|(?:-\s*){3,}|(?:_\s*){3,})$")
TABLE_SEP = re.compile(r"^\s*\|?\s*:?-+:?\s*(?:\|\s*:?-+:?\s*)*\|?\s*$")
HTML_BLOCK_TAGS = {
    "address", "article", "aside", "blockquote", "center", "details", "dialog", "dd", "div", "dl",
    "dt", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6",
    "header", "hr", "li", "main", "nav", "ol", "p", "pre", "section", "summary", "table", "tbody",
    "td", "tfoot", "th", "thead", "tr", "ul",
}
INLINE_HTML = re.compile(r"<!--.*?-->|</?[A-Za-z][A-Za-z0-9-]*(?:\s+[^<>]*?)?/?>", re.S)
ALERTS = {"NOTE": "Note", "TIP": "Tip", "IMPORTANT": "Important", "WARNING": "Warning", "CAUTION": "Caution"}


def indent_of(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


class Renderer:
    def __init__(self, base: Path):
        self.base = base
        self.ids: dict[str, int] = {}
        self.missing: list[str] = []

    # -- links and images ------------------------------------------------------------------

    def anchor(self, text: str) -> str:
        s = slug(text)
        n = self.ids.get(s, 0)
        self.ids[s] = n + 1
        return s if n == 0 else f"{s}-{n}"

    def src(self, url: str) -> str:
        if url.startswith("https://img.shields.io/badge/"):
            return badge(url)
        if re.match(r"^[a-zA-Z][\w+.-]*:", url) or url.startswith("//"):
            return url
        path = (self.base / unquote(url.split("#", 1)[0].split("?", 1)[0])).resolve()
        if path.is_file():
            mime = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
            return f"data:{mime};base64," + base64.b64encode(path.read_bytes()).decode("ascii")
        self.missing.append(url)
        return url

    def href(self, url: str) -> str:
        if url.startswith("#") or re.match(r"^[a-zA-Z][\w+.-]*:", url):
            return url
        target, _, fragment = url.partition("#")
        path = (self.base / unquote(target)).resolve()
        if not path.exists():
            self.missing.append(url)
        return path.as_uri() + (f"#{fragment}" if fragment else "")

    def fix_html(self, tag: str) -> str:
        if tag.startswith("<!--"):
            return ""
        tag = re.sub(r"\s(?:style|on\w+)\s*=\s*(\"[^\"]*\"|'[^']*')", "", tag)  # GitHub strips these
        tag = re.sub(r'(\ssrc=")([^"]+)(")', lambda m: m.group(1) + self.src(html.unescape(m.group(2))) + m.group(3), tag)
        return re.sub(r'(\shref=")([^"]+)(")', lambda m: m.group(1) + esc(self.href(html.unescape(m.group(2)))) + m.group(3), tag)

    # -- inline -----------------------------------------------------------------------------

    def inline(self, text: str) -> str:
        stash: list[str] = []

        def keep(s: str) -> str:
            stash.append(s)
            return f"\x00{len(stash) - 1}\x00"

        text = re.sub(r"\\([\\`*_{}\[\]()#+\-.!|<>~])", lambda m: keep(esc(m.group(1))), text)
        text = re.sub(r"(`+)(.+?)\1", lambda m: keep("<code>" + esc(m.group(2).strip()) + "</code>"), text)
        text = INLINE_HTML.sub(lambda m: keep(self.fix_html(m.group(0))), text)
        text = re.sub(r"<(https?://[^>\s]+)>", lambda m: keep(f'<a href="{esc(m.group(1))}">{esc(m.group(1))}</a>'), text)
        text = re.sub(r'!\[([^\]]*)\]\(\s*([^)\s]+)(?:\s+"([^"]*)")?\s*\)',
                      lambda m: keep(f'<img src="{self.src(m.group(2))}" alt="{esc(m.group(1))}"'
                                     + (f' title="{esc(m.group(3))}"' if m.group(3) else "") + ">"), text)
        text = re.sub(r'\[([^\]]+)\]\(\s*([^)\s]+)(?:\s+"[^"]*")?\s*\)',
                      lambda m: keep(f'<a href="{esc(self.href(m.group(2)))}">') + m.group(1) + keep("</a>"), text)
        text = re.sub(r"\bhttps?://[^\s<>\x00]*[^\s<>\x00.,;:!?)\]'\"]",
                      lambda m: keep(f'<a href="{esc(m.group(0))}">{esc(m.group(0))}</a>'), text)
        text = esc(text).replace("&#x27;", "'").replace("&quot;", '"')
        text = re.sub(r"(\*\*|__)(?=\S)(.+?)(?<=\S)\1", r"<strong>\2</strong>", text)
        text = re.sub(r"(?<![\w*])\*(?=[^\s*])(.+?)(?<=[^\s*])\*(?![\w*])", r"<em>\1</em>", text)
        text = re.sub(r"(?<![\w_])_(?=[^\s_])(.+?)(?<=[^\s_])_(?![\w_])", r"<em>\1</em>", text)
        text = re.sub(r"~~(?=\S)(.+?)(?<=\S)~~", r"<del>\1</del>", text)
        for _ in range(3):
            text = re.sub(r"\x00(\d+)\x00", lambda m: stash[int(m.group(1))], text)
        return text

    # -- blocks -----------------------------------------------------------------------------

    def is_html_start(self, line: str, interrupting: bool) -> bool:
        s = line.lstrip(" ")
        if indent_of(line) > 3 or not s.startswith("<"):
            return False
        if s.startswith("<!--"):
            return True
        m = re.match(r"^</?([A-Za-z][A-Za-z0-9-]*)(?=[\s/>]|$)", s)
        if m and m.group(1).lower() in HTML_BLOCK_TAGS:
            return True
        # Any other complete tag alone on its line, but that cannot interrupt a paragraph.
        return not interrupting and bool(re.fullmatch(r"</?[A-Za-z][\w-]*(?:\s[^<>]*)?/?>\s*", s))

    def starts_block(self, lines: list[str], i: int) -> bool:
        line = lines[i]
        if not line.strip():
            return True
        if FENCE.match(line) or HEADING.match(line) or HR.match(line) or line.lstrip().startswith(">"):
            return True
        m = LIST_ITEM.match(line)
        if m and indent_of(line) <= 3 and m.group(4).strip() and (not m.group(2)[0].isdigit() or m.group(2)[:-1] == "1"):
            return True
        return self.is_html_start(line, interrupting=True)

    def blocks(self, lines: list[str], tight: bool = False) -> str:
        out: list[str] = []
        i, n = 0, len(lines)
        while i < n:
            line = lines[i]
            s = line.strip()
            if not s:
                i += 1
                continue

            m = FENCE.match(line)
            if m:
                fence, lang = m.group(1), m.group(2)
                i += 1
                code: list[str] = []
                while i < n and not lines[i].strip().startswith(fence[0] * len(fence)):
                    code.append(lines[i])
                    i += 1
                i += 1
                body = esc("\n".join(code))
                if lang.lower() in {"powershell", "ps1", "pwsh", "bash", "sh", "shell", "python", "py", "cmake"}:
                    body = re.sub(r"(^|\s)(#(?!&).*)$", r'\1<span class="pl-c">\2</span>', body, flags=re.M)
                cls = f' class="language-{esc(lang)}"' if lang else ""
                out.append(f'<div class="highlight"><pre><code{cls}>{body}</code></pre></div>')
                continue

            m = HEADING.match(line)
            if m:
                level, text = len(m.group(1)), m.group(2) or ""
                ident = self.anchor(text)
                out.append(f'<h{level} id="{ident}"><a class="anchor" href="#{ident}" aria-hidden="true">#</a>'
                           f"{self.inline(text)}</h{level}>")
                i += 1
                continue

            if HR.match(line):
                out.append("<hr>")
                i += 1
                continue

            if self.is_html_start(line, interrupting=False):
                if s.startswith("<!--"):
                    chunk = []
                    while i < n:
                        chunk.append(lines[i])
                        i += 1
                        if "-->" in chunk[-1]:
                            break
                    rest = chunk[-1].split("-->", 1)[1].strip()
                    if rest:
                        out.append(self.fix_raw(rest))
                    continue
                chunk = []
                while i < n and lines[i].strip():
                    chunk.append(lines[i])
                    i += 1
                out.append(self.fix_raw("\n".join(chunk)))
                continue

            if s.startswith(">"):
                quoted = []
                while i < n and lines[i].strip():
                    if lines[i].lstrip().startswith(">"):
                        quoted.append(re.sub(r"^\s*> ?", "", lines[i]))
                    elif quoted and not self.starts_block(lines, i):
                        quoted.append(lines[i])
                    else:
                        break
                    i += 1
                alert = re.match(r"^\s*\[!(\w+)\]\s*$", quoted[0]) if quoted else None
                if alert and alert.group(1).upper() in ALERTS:
                    kind = alert.group(1).upper()
                    out.append(f'<div class="alert alert-{kind.lower()}"><p class="alert-title">{ALERTS[kind]}</p>'
                               f"{self.blocks(quoted[1:])}</div>")
                else:
                    out.append(f"<blockquote>{self.blocks(quoted)}</blockquote>")
                continue

            if "|" in line and i + 1 < n and TABLE_SEP.match(lines[i + 1]) and "-" in lines[i + 1]:
                out.append(self.table(lines, i))
                i += 2
                while i < n and lines[i].strip() and "|" in lines[i]:
                    i += 1
                continue

            m = LIST_ITEM.match(line)
            if m and indent_of(line) <= 3:
                html_list, i = self.list_block(lines, i)
                out.append(html_list)
                continue

            # Paragraph (with setext headings).
            para = [line.strip()]
            i += 1
            heading = 0
            while i < n:
                nxt = lines[i]
                if re.fullmatch(r" {0,3}=+\s*", nxt):
                    heading, i = 1, i + 1
                    break
                if re.fullmatch(r" {0,3}-+\s*", nxt):
                    heading, i = 2, i + 1
                    break
                if self.starts_block(lines, i):
                    break
                para.append(nxt if nxt.endswith("  ") else nxt.strip())
                i += 1
            if heading:
                text = " ".join(p.strip() for p in para)
                ident = self.anchor(text)
                out.append(f'<h{heading} id="{ident}"><a class="anchor" href="#{ident}" aria-hidden="true">#</a>'
                           f"{self.inline(text)}</h{heading}>")
                continue
            body = self.inline("\n".join(para).rstrip())
            body = re.sub(r"(?: {2,}|\\)\n", "<br>\n", body)
            out.append(body if tight else f"<p>{body}</p>")
        return "\n".join(out)

    def fix_raw(self, chunk: str) -> str:
        return INLINE_HTML.sub(lambda m: self.fix_html(m.group(0)), chunk)

    def table(self, lines: list[str], i: int) -> str:
        def cells(row: str) -> list[str]:
            row = row.strip()
            row = row[1:] if row.startswith("|") else row
            row = row[:-1] if row.endswith("|") and not row.endswith("\\|") else row
            parts, cur, code = [], "", False
            for j, ch in enumerate(row):
                if ch == "`":
                    code = not code
                if ch == "|" and not code and (j == 0 or row[j - 1] != "\\"):
                    parts.append(cur)
                    cur = ""
                else:
                    cur += ch
            parts.append(cur)
            return [p.strip().replace("\\|", "|") for p in parts]

        head = cells(lines[i])
        aligns = []
        for spec in cells(lines[i + 1]):
            left, right = spec.startswith(":"), spec.endswith(":")
            aligns.append("center" if left and right else "right" if right else "left" if left else "")
        rows = []
        j = i + 2
        while j < len(lines) and lines[j].strip() and "|" in lines[j]:
            rows.append(cells(lines[j]))
            j += 1

        def cell(tag: str, text: str, k: int) -> str:
            align = aligns[k] if k < len(aligns) else ""
            attr = f' align="{align}"' if align else ""
            return f"<{tag}{attr}>{self.inline(text)}</{tag}>"

        out = ["<table><thead><tr>" + "".join(cell("th", h, k) for k, h in enumerate(head)) + "</tr></thead><tbody>"]
        for row in rows:
            row = (row + [""] * len(head))[: len(head)]
            out.append("<tr>" + "".join(cell("td", c, k) for k, c in enumerate(row)) + "</tr>")
        out.append("</tbody></table>")
        return "\n".join(out)

    def list_block(self, lines: list[str], i: int) -> tuple[str, int]:
        first = LIST_ITEM.match(lines[i])
        base = indent_of(lines[i])
        ordered = first.group(2)[0].isdigit()
        start = int(first.group(2)[:-1]) if ordered else 1
        items: list[list[str]] = []
        loose = False
        n = len(lines)
        while i < n:
            m = LIST_ITEM.match(lines[i])
            if not m or indent_of(lines[i]) != base or m.group(2)[0].isdigit() != ordered:
                break
            gap = len(m.group(3))
            content_indent = base + len(m.group(2)) + (gap if 1 <= gap <= 4 else 1)
            body = [m.group(4)]
            i += 1
            ended = False
            while i < n:
                line = lines[i]
                if not line.strip():
                    j = i
                    while j < n and not lines[j].strip():
                        j += 1
                    if j < n and indent_of(lines[j]) >= content_indent:
                        body.extend([""] * (j - i))  # blank line inside the item: a loose list
                        loose = True
                        i = j
                        continue
                    nm = LIST_ITEM.match(lines[j]) if j < n else None
                    if nm and indent_of(lines[j]) == base and nm.group(2)[0].isdigit() == ordered:
                        loose = True  # items separated by a blank line
                    else:
                        ended = True
                    i = j
                    break
                ind = indent_of(line)
                if ind >= content_indent:
                    body.append(line[content_indent:])
                elif LIST_ITEM.match(line):
                    break  # the next item, or the end of this list
                elif self.starts_block(lines, i):
                    ended = True
                    break
                else:
                    body.append(line.strip())  # lazy continuation of the paragraph
                i += 1
            items.append(body)
            if ended:
                break
        tag = "ol" if ordered else "ul"
        attr = f' start="{start}"' if ordered and start != 1 else ""
        html_items = []
        for body in items:
            check = re.match(r"^\[([ xX])\]\s+", body[0]) if body else None
            prefix, cls = "", ""
            if check:
                body = [body[0][check.end():]] + body[1:]
                prefix = f'<input type="checkbox" disabled{" checked" if check.group(1) != " " else ""}> '
                cls = ' class="task-list-item"'
            html_items.append(f"<li{cls}>{prefix}{self.blocks(body, tight=not loose)}</li>")
        return f"<{tag}{attr}>\n" + "\n".join(html_items) + f"\n</{tag}>", i


# --------------------------------------------------------------------------------------------
# Page
# --------------------------------------------------------------------------------------------

LANGUAGES = {  # extension -> (name, colour), as GitHub's language bar shows them
    ".cpp": ("C++", "#f34b7d"), ".h": ("C++", "#f34b7d"), ".hpp": ("C++", "#f34b7d"), ".cxx": ("C++", "#f34b7d"),
    ".c": ("C", "#555555"), ".py": ("Python", "#3572A5"), ".ps1": ("PowerShell", "#012456"),
    ".cmake": ("CMake", "#DA3434"), ".iss": ("Inno Setup", "#264b99"), ".qss": ("CSS", "#663399"),
    ".css": ("CSS", "#663399"), ".js": ("JavaScript", "#f1e05a"), ".glsl": ("GLSL", "#5686a5"),
}

ICON_DIR = ('<svg class="ico dir" viewBox="0 0 16 16" width="16" height="16"><path d="M1.5 2.5h4.2l1.6 1.6h7.2'
            'v9.4h-13z"/></svg>')
ICON_FILE = ('<svg class="ico file" viewBox="0 0 16 16" width="16" height="16"><path d="M3.5 1.5h6l3 3v10h-9z" '
             'fill="none" stroke-width="1.2"/></svg>')


def repo_entries() -> list[tuple[str, bool, str, str]]:
    tracked = git("ls-files").splitlines()
    untracked = git("ls-files", "--others", "--exclude-standard").splitlines()
    tops: dict[str, bool] = {}
    for path in tracked + untracked:
        head, sep, _ = path.partition("/")
        tops[head] = tops.get(head, False) or bool(sep)
    entries = []
    for name, is_dir in tops.items():
        info = git("log", "-1", "--format=%s%x1f%cr", "--", name)
        message, _, when = info.partition("\x1f")
        entries.append((name, is_dir, message or "Not committed yet", when or "now"))
    entries.sort(key=lambda e: (not e[1], e[0].lower()))
    return entries


def language_bar() -> str:
    sizes: dict[tuple[str, str], int] = {}
    for path in git("ls-files").splitlines():
        p = ROOT / path
        lang = ("CMake", "#DA3434") if p.name == "CMakeLists.txt" else LANGUAGES.get(p.suffix.lower())
        if lang and p.is_file():
            sizes[lang] = sizes.get(lang, 0) + p.stat().st_size
    total = sum(sizes.values()) or 1
    ranked = sorted(sizes.items(), key=lambda kv: -kv[1])
    bar = "".join(f'<span style="width:{size / total * 100:.2f}%;background:{colour}"></span>'
                  for (_, colour), size in ranked)
    items = "".join(f'<li><span class="dot" style="background:{colour}"></span><b>{esc(name)}</b> '
                    f'<span class="muted">{size / total * 100:.1f}%</span></li>' for (name, colour), size in ranked)
    return f'<div class="langbar">{bar}</div><ul class="langs">{items}</ul>'


CSS = """
:root{--bg:#fff;--fg:#1f2328;--muted:#59636e;--border:#d1d9e0;--border-muted:#d1d9e0b3;--subtle:#f6f8fa;
--accent:#0969da;--btn:#1f883d;--topic-bg:#ddf4ff;--code-bg:#818b981f;--row-hover:#f6f8fa;--pl-c:#59636e;
--note:#0969da;--tip:#1a7f37;--important:#8250df;--warning:#9a6700;--caution:#d1242f}
@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#f0f6fc;--muted:#9198a1;--border:#3d444d;
--border-muted:#3d444db3;--subtle:#151b23;--accent:#4493f8;--btn:#238636;--topic-bg:#388bfd1a;--code-bg:#656c7633;
--row-hover:#151b23;--pl-c:#9198a1;--note:#4493f8;--tip:#3fb950;--important:#ab7df8;--warning:#d29922;--caution:#f85149}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans",Helvetica,Arial,sans-serif}
a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}
.muted{color:var(--muted)}
.preview-note{background:var(--subtle);border-bottom:1px solid var(--border);color:var(--muted);font-size:12px;padding:6px 16px;text-align:center}
.top{background:var(--subtle);border-bottom:1px solid var(--border);padding:16px 32px 0}
.repo-title{font-size:20px;display:flex;align-items:center;gap:8px}
.repo-title b{font-weight:600}.repo-title .owner{color:var(--accent)}
.pill{border:1px solid var(--border);border-radius:2em;color:var(--muted);font-size:12px;font-weight:500;padding:0 7px;line-height:18px}
.tabs{display:flex;gap:4px;margin-top:12px;overflow:auto}
.tabs span{padding:8px 12px;border-bottom:2px solid transparent;white-space:nowrap}
.tabs .sel{border-bottom-color:#fd8c73;font-weight:600}
.layout{max-width:1280px;margin:0 auto;padding:24px 32px;display:grid;grid-template-columns:minmax(0,1fr) 296px;gap:24px}
@media (max-width:1011px){.layout{grid-template-columns:minmax(0,1fr)}aside{order:-1}}
.bar{display:flex;align-items:center;gap:8px;margin-bottom:16px}
.btn{border:1px solid var(--border);border-radius:6px;background:var(--subtle);padding:4px 12px;font-weight:500;font-size:14px}
.btn.green{background:var(--btn);color:#fff;border-color:transparent;margin-left:auto}
.box{border:1px solid var(--border);border-radius:6px;overflow:hidden}
.commit{display:flex;gap:8px;align-items:center;background:var(--subtle);padding:12px 16px;border-bottom:1px solid var(--border)}
.avatar{width:20px;height:20px;border-radius:50%;background:linear-gradient(135deg,#6e7781,#afb8c1);display:inline-block}
.commit .msg{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.files{width:100%;border-collapse:collapse}
.files td{padding:8px 16px;border-top:1px solid var(--border);white-space:nowrap}
.files tr:first-child td{border-top:0}.files tr:hover td{background:var(--row-hover)}
.files td.msg{color:var(--muted);width:100%;max-width:0;overflow:hidden;text-overflow:ellipsis}
.files td.when{color:var(--muted);text-align:right}
.ico{vertical-align:text-bottom;margin-right:10px}.ico.dir path{fill:#54aeff}.ico.file path{stroke:var(--muted)}
.readme{margin-top:24px}
.readme-tabs{display:flex;gap:4px;border-bottom:1px solid var(--border);padding:0 8px;position:sticky;top:0;background:var(--bg);z-index:1}
.readme-tabs span{padding:10px 8px;font-weight:500;border-bottom:2px solid transparent}
.readme-tabs .sel{border-bottom-color:#fd8c73;font-weight:600}
aside h2{font-size:16px;margin:0 0 16px;font-weight:600}
aside p.about{font-size:16px;margin:0 0 16px}
.topics{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:16px}
.topics span{background:var(--topic-bg);color:var(--accent);border-radius:2em;padding:0 10px;font-size:12px;font-weight:500;line-height:22px}
aside ul.facts{list-style:none;padding:0;margin:0 0 16px;color:var(--muted)}aside ul.facts li{margin:6px 0}
aside section{border-bottom:1px solid var(--border);padding-bottom:16px;margin-bottom:16px}
.langbar{display:flex;height:8px;border-radius:6px;overflow:hidden;margin-bottom:8px;gap:2px}
.langs{list-style:none;padding:0;margin:0;display:flex;flex-wrap:wrap;gap:4px 16px;font-size:12px}
.langs b{font-weight:600}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
/* Markdown body, after GitHub's styles */
.markdown-body{padding:32px;font-size:16px;line-height:1.5;word-wrap:break-word}
.markdown-body>*:first-child{margin-top:0!important}
.markdown-body p,.markdown-body blockquote,.markdown-body ul,.markdown-body ol,.markdown-body table,.markdown-body pre,.markdown-body .highlight,.markdown-body details,.markdown-body .alert{margin-top:0;margin-bottom:16px}
.markdown-body h1,.markdown-body h2,.markdown-body h3,.markdown-body h4,.markdown-body h5,.markdown-body h6{margin-top:24px;margin-bottom:16px;font-weight:600;line-height:1.25;position:relative}
.markdown-body h1{font-size:2em;padding-bottom:.3em;border-bottom:1px solid var(--border-muted)}
.markdown-body h2{font-size:1.5em;padding-bottom:.3em;border-bottom:1px solid var(--border-muted)}
.markdown-body h3{font-size:1.25em}.markdown-body h4{font-size:1em}.markdown-body h5{font-size:.875em}.markdown-body h6{font-size:.85em;color:var(--muted)}
.markdown-body .anchor{position:absolute;left:-22px;opacity:0;color:var(--muted);font-weight:400;padding-right:6px}
.markdown-body h1:hover .anchor,.markdown-body h2:hover .anchor,.markdown-body h3:hover .anchor,.markdown-body h4:hover .anchor{opacity:1;text-decoration:none}
.markdown-body hr{height:.25em;padding:0;margin:24px 0;background:var(--border);border:0}
.markdown-body img{max-width:100%;box-sizing:content-box;background:transparent}
.markdown-body ul,.markdown-body ol{padding-left:2em}.markdown-body li+li{margin-top:.25em}
.markdown-body li>p{margin-top:16px}.markdown-body ul ul,.markdown-body ol ol,.markdown-body ul ol,.markdown-body ol ul{margin-top:0;margin-bottom:0}
.markdown-body .task-list-item{list-style:none}.markdown-body .task-list-item input{margin:0 .2em .25em -1.4em;vertical-align:middle}
.markdown-body table{display:block;width:max-content;max-width:100%;overflow:auto;border-spacing:0;border-collapse:collapse;font-variant:tabular-nums}
.markdown-body table th{font-weight:600}
.markdown-body table th,.markdown-body table td{padding:6px 13px;border:1px solid var(--border)}
.markdown-body table td>:last-child{margin-bottom:0}
.markdown-body table tr{background:var(--bg);border-top:1px solid var(--border-muted)}
.markdown-body table tr:nth-child(2n){background:var(--subtle)}
.markdown-body table img{background:transparent}
.markdown-body code{padding:.2em .4em;margin:0;font-size:85%;white-space:break-spaces;background:var(--code-bg);border-radius:6px;font-family:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,"Liberation Mono",monospace}
.markdown-body pre{padding:16px;overflow:auto;font-size:85%;line-height:1.45;background:var(--subtle);border-radius:6px;margin:0}
.markdown-body pre code{padding:0;background:transparent;font-size:100%;white-space:pre;border-radius:0}
.markdown-body .pl-c{color:var(--pl-c)}
.markdown-body blockquote{padding:0 1em;color:var(--muted);border-left:.25em solid var(--border);margin-left:0;margin-right:0}
.markdown-body .alert{padding:8px 16px;border-left:.25em solid var(--note)}
.markdown-body .alert-title{font-weight:500;margin-bottom:4px;color:var(--note)}
.markdown-body .alert-tip{border-color:var(--tip)}.markdown-body .alert-tip .alert-title{color:var(--tip)}
.markdown-body .alert-important{border-color:var(--important)}.markdown-body .alert-important .alert-title{color:var(--important)}
.markdown-body .alert-warning{border-color:var(--warning)}.markdown-body .alert-warning .alert-title{color:var(--warning)}
.markdown-body .alert-caution{border-color:var(--caution)}.markdown-body .alert-caution .alert-title{color:var(--caution)}
.markdown-body kbd{display:inline-block;padding:3px 5px;font:11px ui-monospace,monospace;line-height:10px;border:1px solid var(--border);border-radius:6px;box-shadow:inset 0 -1px 0 var(--border)}
.markdown-body sub,.markdown-body sup{font-size:75%}
.markdown-body details summary{cursor:pointer}
"""


def page(readme: Path, body: str) -> str:
    owner = git("config", "user.name") or "you"
    repo = ROOT.name
    branch = git("branch", "--show-current") or "main"
    subject, short, when = (git("log", "-1", "--format=%s%x1f%h%x1f%cr").split("\x1f") + ["", "", ""])[:3]
    commits = git("rev-list", "--count", "HEAD") or "0"
    dirty = bool(git("status", "--porcelain"))
    rows = "".join(
        f'<tr><td>{ICON_DIR if is_dir else ICON_FILE}<a href="#">{esc(name)}</a></td>'
        f'<td class="msg">{esc(msg)}</td><td class="when">{esc(when)}</td></tr>'
        for name, is_dir, msg, when in repo_entries()
    )
    has_licence = any((ROOT / n).is_file() for n in ("LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING"))
    licence_name = ""
    if has_licence:
        name = next(n for n in ("LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING") if (ROOT / n).is_file())
        text = (ROOT / name).read_text(encoding="utf-8", errors="replace").strip()
        licence_name = (text.splitlines()[0] if text else "Licence").replace("License", "license")
    topics = "".join(f"<span>{esc(t)}</span>" for t in TOPICS)
    facts = ["Readme"] + ([licence_name] if licence_name else []) + ["Activity", "0 stars", "1 watching", "0 forks"]
    facts_html = "".join(f"<li>{esc(f)}</li>" for f in facts)
    licence_tab = f"<span>{esc(licence_name)}</span>" if licence_name else ""
    note = ("Local preview of the GitHub front page, made by scripts/preview_readme.py from "
            f"{esc(readme.name)}" + (" (includes uncommitted changes)" if dirty else "") + ". Close to GitHub, not exact.")
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{esc(owner)}/{esc(repo)} — README preview</title><style>{CSS}</style></head>
<body>
<div class="preview-note">{note}</div>
<div class="top">
  <div class="repo-title"><span class="owner">{esc(owner)}</span><span class="muted">/</span><b>{esc(repo)}</b><span class="pill">Public</span></div>
  <div class="tabs"><span class="sel">Code</span><span>Issues</span><span>Pull requests</span><span>Actions</span><span>Projects</span><span>Wiki</span><span>Security</span><span>Insights</span><span>Settings</span></div>
</div>
<div class="layout">
<main>
  <div class="bar"><span class="btn">⎇ {esc(branch)}</span><span class="muted">1 Branch · 0 Tags</span><span class="btn green">Code ▾</span></div>
  <div class="box">
    <div class="commit"><span class="avatar"></span><b>{esc(owner)}</b><span class="msg muted">{esc(subject)}</span>
      <span class="muted">{esc(short)} · {esc(when)}</span><b>{esc(commits)} Commits</b></div>
    <table class="files">{rows}</table>
  </div>
  <div class="box readme">
    <div class="readme-tabs"><span class="sel">README</span>{licence_tab}</div>
    <article class="markdown-body">
{body}
    </article>
  </div>
</main>
<aside>
  <section><h2>About</h2><p class="about">{esc(ABOUT)}</p><div class="topics">{topics}</div>
  <ul class="facts">{facts_html}</ul></section>
  <section><h2>Releases</h2><span class="muted">No releases published</span></section>
  <section><h2>Packages</h2><span class="muted">No packages published</span></section>
  <section style="border:0"><h2>Languages</h2>{language_bar()}</section>
</aside>
</div>
</body></html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("readme", nargs="?", default=str(ROOT / "README.md"))
    parser.add_argument("-o", "--output", default=str(ROOT / "build" / "readme-preview.html"))
    parser.add_argument("--open", action="store_true", help="open the preview in the default browser")
    args = parser.parse_args()

    readme = Path(args.readme).resolve()
    renderer = Renderer(readme.parent)
    body = renderer.blocks(readme.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n"))
    out = Path(args.output).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page(readme, body), encoding="utf-8")
    for missing in dict.fromkeys(renderer.missing):
        print(f"warning: {missing} does not exist", file=sys.stderr)
    print(out)
    if args.open:
        webbrowser.open(out.as_uri())
    return 0


if __name__ == "__main__":
    sys.exit(main())
