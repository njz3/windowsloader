# windowsloader - runs Windows arcade games with emulated cabinet hardware
# Copyright (C) 2026 Benjamin Maurin - njz3
# License: GNU General Public License version 3 (GPL v3)
#
# This program is free software: you can redistribute it and/or modify it under the terms of
# the GNU General Public License version 3 as published by the Free Software Foundation.
# It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
# the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
# file for the full text. The author reserves the right to change the license of this code.
#
# Code taken from MAME, linuxloader, sm2-emu, Flycast or segatools stays under the license of
# its own project; such code is marked with its origin.

"""The interface's catalogues (src/common/lang.h): the texts marked in the sources, wl_tr("...")
and WL_N("..."), gathered in lang/windowsloader.pot, then each lang/<code>.po brought up to date
as msgmerge would: its translations kept, the new texts added empty, the texts gone kept at the
end as obsolete (#~). The same as xgettext --keyword=wl_tr --keyword=WL_N --from-code=UTF-8, then
msgmerge, without them: python alone.

    python scripts/lang_update.py            the template and the catalogues brought up to date
    python scripts/lang_update.py --check    nothing written: 1 when a catalogue misses a text, has
                                             one untranslated or fuzzy, or another %-format
    python scripts/lang_update.py --new CODE "Name"
                                             lang/CODE.po made from the template (pt_BR "Português")
    python scripts/lang_update.py --unmarked the French-looking texts of the sources left unmarked
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LANG = os.path.join(ROOT, "lang")
TEMPLATE = os.path.join(LANG, "windowsloader.pot")
SOURCES = ["src/common", "src/hook", "src/hook/games", "src/launcher"]
KEYWORDS = ("wl_tr", "WL_N")
# Calls whose texts are the log's or the console's, in English, never shown translated
LOG_CALLS = ("wl_log", "wl_log_bytes", "say", "say_error", "lang_log", "fprintf", "printf", "OutputDebugStringA")

HEADER = (
    "Project-Id-Version: windowsloader\n"
    "Language: {lang}\n"
    "MIME-Version: 1.0\n"
    "Content-Type: text/plain; charset=UTF-8\n"
    "Content-Transfer-Encoding: 8bit\n"
    "X-Source-Language: fr\n"
    "{name}"
)

# ------------------------------------------------------------------------------
# The sources' texts
# ------------------------------------------------------------------------------

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "a": "\a", "b": "\b", "f": "\f", "v": "\v", "\\": "\\", '"': '"', "'": "'", "?": "?"}


def c_unescape(body):
    """A C literal's text (between its quotes) as it is in the program"""
    i = 0
    raw = bytearray()
    while i < len(body):
        c = body[i]
        if c != "\\":
            raw += c.encode("utf-8")
            i += 1
            continue
        e = body[i + 1]
        if e == "x":
            j = i + 2
            while j < len(body) and j < i + 4 and body[j] in "0123456789abcdefABCDEF":
                j += 1
            raw.append(int(body[i + 2:j], 16))
            i = j
        elif e in "01234567":
            j = i + 1
            while j < len(body) and j < i + 4 and body[j] in "01234567":
                j += 1
            raw.append(int(body[i + 1:j], 8))
            i = j
        else:
            raw += ESCAPES.get(e, e).encode("utf-8")
            i += 2
    return raw.decode("utf-8", errors="replace")  # bytes ("\xFF\xFF") are not texts to translate


def tokens(src):
    """(kind, text, line): 's' a string literal (its text between the quotes), 'i' an identifier,
    'p' a punctuation; the comments, characters and preprocessor lines left out"""
    i, line, n = 0, 1, len(src)
    at_line_start = True
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
            at_line_start = True
            continue
        if c in " \t\r":
            i += 1
            continue
        if at_line_start and c == "#":  # #include, #define: to the line's end, continued lines too
            while i < n and src[i] != "\n":
                if src[i] == "\\" and src[i + 1:i + 2] == "\n":
                    line += 1
                    i += 1
                i += 1
            continue
        at_line_start = False
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            line += src.count("\n", i, j)
            i = j + 2
        elif c == '"' or (c in "LuU" and src[i + 1:i + 2] == '"') or src.startswith('u8"', i):
            prefix = src[i:src.index('"', i)]
            start = i = src.index('"', i) + 1
            while src[i] != '"':
                i += 2 if src[i] == "\\" else 1
            yield ("S" if prefix else "s"), src[start:i], line
            i += 1
        elif c == "'":
            j = i + 1
            while src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
        elif c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            yield "i", src[i:j], line
            i = j
        elif c.isdigit():
            j = i
            while j < n and (src[j].isalnum() or src[j] in "._'"):
                j += 1
            i = j
        else:
            yield "p", c, line
            i += 1


def source_files():
    for d in SOURCES:
        folder = os.path.join(ROOT, d)
        for name in sorted(os.listdir(folder)):
            if name.endswith(".cpp"):
                yield d + "/" + name


def gather():
    """The marked texts in the order they come: {msgid: [references]}"""
    texts = {}
    for rel in source_files():
        toks = list(tokens(open(os.path.join(ROOT, rel), encoding="utf-8").read()))
        for k, (kind, text, line) in enumerate(toks):
            if kind != "i" or text not in KEYWORDS or k + 2 >= len(toks) or toks[k + 1][1] != "(" or toks[k + 2][0] != "s":
                continue
            j, parts = k + 2, []
            while toks[j][0] == "s":
                parts.append(c_unescape(toks[j][1]))
                j += 1
            if toks[j][1] != ")":
                print(f"{rel}:{line}: {text}(\"...\" ...) with more than its texts: left out", file=sys.stderr)
                continue
            texts.setdefault("".join(parts), []).append(f"{rel}:{line}")
    return texts


def unmarked():
    """The literals with French letters or words outside wl_tr / WL_N and the log's calls"""
    french = re.compile(r"[àâçéèêëîïôûùüÿœÀÂÇÉÈÊËÎÏÔÛÙÜŒ«»]|\b(le|la|les|du|des|une?|est|pas|pour|avec|dans|sur|jeu|aucune?)\b")
    for rel in source_files():
        toks = list(tokens(open(os.path.join(ROOT, rel), encoding="utf-8").read()))
        calls = []  # the calls open: their names
        for k, (kind, text, line) in enumerate(toks):
            if kind == "p" and text == "(":
                calls.append(toks[k - 1][1] if k and toks[k - 1][0] == "i" else "")
            elif kind == "p" and text == ")":
                if calls:
                    calls.pop()
            elif kind == "p" and text in ";{}":
                calls = []
            elif kind == "s" and french.search(c_unescape(text)):
                inside = [c for c in calls if c]
                if any(c in KEYWORDS for c in inside) or any(c in LOG_CALLS for c in inside):
                    continue
                print(f"{rel}:{line}: [{inside[-1] if inside else ''}] {c_unescape(text)[:100]}")


# ------------------------------------------------------------------------------
# The catalogues
# ------------------------------------------------------------------------------


def conversions(s):
    """The %-conversions of a printf format in their order, as lang.cpp compares them"""
    out, i = [], 0
    while i < len(s):
        if s[i] != "%":
            i += 1
            continue
        i += 1
        if s[i:i + 1] == "%":
            i += 1
            continue
        spec = ""
        while i < len(s) and s[i] in "-+ #0'":
            i += 1
        while i < len(s) and s[i] in "0123456789.*":
            spec += "*" if s[i] == "*" else ""
            i += 1
        while i < len(s) and s[i] in "hlLzjtwI":
            spec += s[i]
            if s[i] == "I":
                while i + 1 < len(s) and s[i + 1].isdigit():
                    spec += s[i + 1]
                    i += 1
            i += 1
        if i < len(s):
            spec += s[i]
            i += 1
        out.append(spec)
    return out


def po_unescape(s):
    return re.sub(r'\\(.)', lambda m: ESCAPES.get(m.group(1), m.group(1)), s)


def po_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")


def po_lines(keyword, text, prefix=""):
    """keyword "text", the text cut after each \\n as msgmerge does, never at a width"""
    pieces = re.findall(r"[^\n]*\n|[^\n]+$", text) if text else [""]
    if len(pieces) == 1:
        return [f'{prefix}{keyword} "{po_escape(text)}"']
    return [f'{prefix}{keyword} ""'] + [f'{prefix}"{po_escape(p)}"' for p in pieces]


class Entry:
    def __init__(self):
        self.msgid = None
        self.msgstr = ""
        self.comments = []  # the translator's (# ...)
        self.flags = set()
        self.refs = []
        self.obsolete = False


def read_po(path):
    """The entries of a .po file, its header first (msgid "")"""
    entries, e, field = [], Entry(), None

    def done():
        nonlocal e, field
        if e.msgid is not None:
            entries.append(e)
        e, field = Entry(), None

    for raw in open(path, encoding="utf-8-sig"):
        line = raw.rstrip("\r\n")
        obsolete = line.startswith("#~")
        if obsolete:
            line = line[2:].lstrip()
        if not line.strip():
            continue
        if line.startswith("#"):
            if field == "msgstr":
                done()
            if line.startswith("#,"):
                e.flags |= {f.strip() for f in line[2:].split(",") if f.strip()}
            elif line.startswith("#:"):
                e.refs += line[2:].split()
            elif line.startswith("# ") or line == "#":
                e.comments.append(line)
            continue
        m = re.match(r'(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?)\s+"(.*)"\s*$', line)
        if m:
            key, text = m.group(1), po_unescape(m.group(2))
            if key in ("msgctxt", "msgid") and field == "msgstr":
                done()
            e.obsolete = e.obsolete or obsolete
            if key == "msgid":
                e.msgid, field = text, "msgid"
            elif key in ("msgstr", "msgstr[0]"):
                e.msgstr, field = text, "msgstr"
            else:
                field = "other"
        elif line.startswith('"') and line.endswith('"'):
            text = po_unescape(line[1:-1])
            if field == "msgid":
                e.msgid += text
            elif field == "msgstr":
                e.msgstr += text
    done()
    return entries


def header_of(entries, lang, name):
    for e in entries:
        if e.msgid == "" and not e.obsolete:
            lines = [l for l in e.msgstr.split("\n") if l]
            keys = {l.split(":", 1)[0] for l in lines}
            for need in ("Project-Id-Version: windowsloader", f"Language: {lang}", "MIME-Version: 1.0",
                         "Content-Type: text/plain; charset=UTF-8", "Content-Transfer-Encoding: 8bit", "X-Source-Language: fr"):
                if need.split(":", 1)[0] not in keys:
                    lines.append(need)
            return "".join(l + "\n" for l in lines), e.comments
    return HEADER.format(lang=lang, name=f"X-Language-Name: {name}\n" if name else ""), []


def write_po(path, header, top, entries):
    out = list(top)
    out += ['msgid ""'] + po_lines("msgstr", header) + [""]
    for e in entries:
        if e.obsolete:
            continue
        out += e.comments
        if e.refs:
            out.append("#: " + " ".join(e.refs))
        if e.flags:
            out.append("#, " + ", ".join(sorted(e.flags)))
        out += po_lines("msgid", e.msgid) + po_lines("msgstr", e.msgstr) + [""]
    for e in entries:
        if e.obsolete:
            out += e.comments + po_lines("msgid", e.msgid, "#~ ") + po_lines("msgstr", e.msgstr, "#~ ") + [""]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out).rstrip("\n") + "\n")


def template_entries(texts):
    entries = []
    for msgid, refs in texts.items():
        e = Entry()
        e.msgid, e.refs = msgid, refs
        if conversions(msgid):
            e.flags.add("c-format")
        entries.append(e)
    return entries


def merge(template, old):
    """msgmerge's way: the template's order and references, the catalogue's translations"""
    known = {e.msgid: e for e in old if e.msgid}
    used, out = set(), []
    for t in template:
        e = Entry()
        e.msgid, e.refs = t.msgid, t.refs
        e.flags = set(t.flags)
        o = known.get(t.msgid)
        if o:
            e.msgstr, e.comments = o.msgstr, o.comments
            e.flags |= o.flags & {"fuzzy"}
            used.add(t.msgid)
        out.append(e)
    for o in old:
        if o.msgid and o.msgid not in used and o.msgstr:
            o.obsolete, o.refs = True, []
            out.append(o)
    return out


def catalogues():
    return sorted(n for n in os.listdir(LANG) if n.endswith(".po")) if os.path.isdir(LANG) else []


def check(texts):
    bad = 0
    for name in catalogues():
        entries = [e for e in read_po(os.path.join(LANG, name)) if e.msgid and not e.obsolete]
        have = {e.msgid for e in entries}
        for msgid in texts:
            if msgid not in have:
                print(f"{name}: missing (run scripts/lang_update.py): {msgid[:90]!r}")
                bad += 1
        for e in entries:
            if not e.msgstr:
                print(f"{name}: untranslated: {e.msgid[:90]!r}")
                bad += 1
            elif "fuzzy" in e.flags:
                print(f"{name}: fuzzy: {e.msgid[:90]!r}")
                bad += 1
            elif conversions(e.msgid) != conversions(e.msgstr):
                print(f"{name}: other %-formats: {e.msgid[:60]!r} -> {e.msgstr[:60]!r}")
                bad += 1
        print(f"{name}: {len(entries)} texts", "OK" if not bad else "")
    return bad


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    args = sys.argv[1:]
    if "--unmarked" in args:
        unmarked()
        return 0
    texts = gather()
    if "--check" in args:
        return 1 if check(texts) else 0
    os.makedirs(LANG, exist_ok=True)
    template = template_entries(texts)
    top = ["# windowsloader's interface texts: the template of lang/<code>.po (src/common/lang.h), made by",
           "# scripts/lang_update.py from the sources' wl_tr(\"...\") and WL_N(\"...\"). The texts are French.",
           "# Copyright (C) 2026 Benjamin Maurin - njz3. GPL v3."]
    write_po(TEMPLATE, HEADER.format(lang="", name=""), top, template)
    print(f"{os.path.relpath(TEMPLATE, ROOT)}: {len(template)} texts")
    if "--new" in args:
        i = args.index("--new")
        code = args[i + 1]
        name = args[i + 2] if i + 2 < len(args) else ""
        path = os.path.join(LANG, code + ".po")
        if os.path.exists(path):
            sys.exit(f"{path} exists already")
        write_po(path, HEADER.format(lang=code, name=f"X-Language-Name: {name}\n" if name else ""),
                 [f"# windowsloader's interface in {name or code} (src/common/lang.h)."], template)
    for name in catalogues():
        path = os.path.join(LANG, name)
        old = read_po(path)
        header, top = header_of(old, name[:-3], "")
        merged = merge(template, [e for e in old if e.msgid != ""])
        write_po(path, header, top, merged)
        todo = sum(1 for e in merged if not e.obsolete and (not e.msgstr or "fuzzy" in e.flags))
        print(f"lang/{name}: {len(merged)} texts, {todo} to translate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
