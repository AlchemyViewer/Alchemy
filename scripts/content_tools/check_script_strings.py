"""The studio's words, checked against the three places they live.

The Script Studio says what it says through keys in strings.xml, each with
the English written a second time in the code as the fallback and what the
tests read; and it takes the engines' messages apart again through tables
in almessagemap.cpp transcribed from the engines' own sources, so that a
translator may work from the same English under the same keys. Nothing in
the build keeps the copies together, so this does:

  * every `"Key", "English"` pair the code passes to alSaid, said, fail,
    note, problem and the like, against the key's text in strings.xml;
  * every row of the map's LSL table against Tailslide's logger.cc, by
    error code, with %s and %d read as the marks;
  * every row of the lint table against the emitWarning calls in Luau's
    Linter.cpp, by lint name, likewise;
  * every literal stretch of the type-error table against Luau's
    Error.cpp, whose messages are built rather than formatted, so only
    their pieces can be looked for -- each stretch must be buildable
    from the file's own string literals -- and likewise the lints the map
    matches by shape against Linter.cpp;
  * every key in the tables against strings.xml, and every studio key in
    strings.xml against the code, for one that nothing says any more.

Tailslide and Luau are found under vcpkg/buildtrees, the newest checkout
of each, unless named:

    python scripts/content_tools/check_script_strings.py
    python scripts/content_tools/check_script_strings.py --tailslide DIR --luau DIR
    python scripts/content_tools/check_script_strings.py --list-luau   # what the lint table lacks

Exits 1 where a copy has drifted. Run it after a vcpkg upgrade of either
engine, and after touching strings.xml or a fallback in the code.
"""
import argparse
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STRINGS = os.path.join(ROOT, "indra", "newview", "skins", "default", "xui", "en", "strings.xml")
MAP = os.path.join(ROOT, "indra", "alscript", "almessagemap.cpp")
CODE = [
    os.path.join(ROOT, "indra", "llui"),
    os.path.join(ROOT, "indra", "alscript"),
    os.path.join(ROOT, "indra", "newview"),
]
# The keys that are the studio's: what strings.xml groups under these
# prefixes is compared; the rest of the file is the viewer's.
PREFIXES = ("Vim", "Preproc", "Optimizer", "Inliner", "LuauLint", "Luau", "LSL", "Workspace", "Analysis", "XUIEdit", "FindBar", "TabStrip")
# The viewer's own under those prefixes: the legacy editor's tooltips.
NOT_OURS = ("LSLTip",)
# The calls that pass a key with its English, and which arguments those
# are: the key's, then the English's -- two of them for a counted form,
# whose key takes A for one and B for many.
CALLS = {
    "alSaid": [(0, 1, "")],
    "said": [(0, 1, "")],
    "fail": [(0, 1, "")],
    "problem": [(1, 2, "")],
    "note": [(1, 2, "")],
    "noteAt": [(1, 2, "")],
    "alSaidCount": [(0, 2, "A"), (0, 3, "B")],
}

FAIL = []


def fail(what):
    FAIL.append(what)
    print("  " + what)


def newest(pattern):
    found = sorted(glob.glob(pattern), key=os.path.getmtime)
    return found[-1] if found else None


def unescape_c(s):
    """A C++ string literal's body as the string it is."""
    return (s.replace("\\'", "'").replace('\\"', '"').replace("\\\\", "\\")
             .replace("\\n", "\n").replace("\\t", "\t"))


def marks_from_printf(fmt):
    """A printf template as the map writes it: each %s, %d, %zu ... a mark
    in order, a %1$s the mark it names, %% a percent."""
    out = []
    n = 0
    i = 0
    while i < len(fmt):
        c = fmt[i]
        if c != "%":
            out.append(c)
            i += 1
            continue
        m = re.match(r"%(\d+)\$[a-z]|%%|%[-+ 0-9.*lz]*[a-zA-Z]", fmt[i:])
        if not m:
            out.append(c)
            i += 1
            continue
        piece = m.group(0)
        if piece == "%%":
            out.append("%")
        elif m.group(1):
            out.append("[%s]" % m.group(1))
        else:
            n += 1
            out.append("[%d]" % n)
        i += len(piece)
    return "".join(out)


def read_strings():
    tree = ET.parse(STRINGS)
    out = {}
    for s in tree.getroot().iter("string"):
        name = s.get("name", "")
        if name.startswith(PREFIXES) and not name.startswith(NOT_OURS):
            out[name] = s.text or ""
    return out


def read_map():
    """The tables of almessagemap.cpp: (code, key, text) rows for LSL,
    (name, key, text) for the lints and for the lints matched by shape,
    (key, text) for the errors."""
    src = open(MAP, encoding="utf-8").read()

    def table(name):
        m = re.search(r"%s\[\]\s*=\s*\{(.*?)\n\s*\};" % re.escape(name), src, re.S)
        return m.group(1) if m else ""

    lsl = re.findall(r'\{\s*(\d+),\s*"([A-Za-z0-9]+)",\s*"((?:[^"\\]|\\.)*)"\s*\}', table("LSL_ROWS"))
    lint = re.findall(r'\{\s*"([A-Za-z]+)",\s*"([A-Za-z0-9]+)",\s*"((?:[^"\\]|\\.)*)"\s*\}', table("LINT_ROWS"))
    shape = re.findall(r'\{\s*"([A-Za-z]+)",\s*"([A-Za-z0-9]+)",\s*"((?:[^"\\]|\\.)*)"\s*\}', table("LINT_SHAPE_ROWS"))
    err = re.findall(r'\{\s*"([A-Za-z0-9]+)",\s*"((?:[^"\\]|\\.)*)"\s*\}', table("ERROR_ROWS"))
    return ([(int(c), k, unescape_c(t)) for c, k, t in lsl],
            [(n, k, unescape_c(t)) for n, k, t in lint],
            [(n, k, unescape_c(t)) for n, k, t in shape],
            [(k, unescape_c(t)) for k, t in err])


def arguments(text, at):
    """The arguments of the call whose ( is at `at`, split at the commas
    that are nobody's: not a string's, not a nested call's or brace's."""
    args = []
    depth = 0
    i = at + 1
    begin = i
    quote = None
    while i < len(text):
        c = text[i]
        if quote:
            if c == "\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append(text[begin:i].strip())
                return args
            depth -= 1
        elif c == "," and depth == 0:
            args.append(text[begin:i].strip())
            begin = i + 1
        i += 1
    return args


def literal_or_branches(arg):
    """A string literal as [(None, text)], or a `c ? "a" : "b"` as its two
    branches [(0, a), (1, b)]; nothing for anything else."""
    lit = r'"((?:[^"\\]|\\.)*)"'
    m = re.fullmatch(lit, arg, re.S)
    if m:
        return [(None, unescape_c(m.group(1)))]
    m = re.fullmatch(r".*?\?\s*" + lit + r"\s*:\s*" + lit, arg, re.S)
    if m:
        return [(0, unescape_c(m.group(1))), (1, unescape_c(m.group(2)))]
    return []


def read_code_pairs():
    """Every key the code passes with its English: the arguments of the
    calls in CALLS, each a literal or the two branches of a conditional,
    paired branch by branch."""
    pairs = []
    uses = set()
    call_re = re.compile(r"(?<![A-Za-z0-9_])(" + "|".join(re.escape(c) for c in CALLS) + r")\s*\(")
    for folder in CODE:
        for path in sorted(glob.glob(os.path.join(folder, "*.cpp")) + glob.glob(os.path.join(folder, "*.h"))):
            text = open(path, encoding="utf-8", errors="replace").read().replace("\\x01", " ")
            rel = os.path.relpath(path, ROOT)
            # A key anywhere in a literal: whole, or between the marks a
            # worker's message carries it in.
            for m in re.finditer(r"(?<![A-Za-z0-9_])((?:" + "|".join(PREFIXES) + r")[A-Za-z0-9]*)(?![A-Za-z0-9_])", text):
                uses.add(m.group(1))
            for m in call_re.finditer(text):
                args = arguments(text, m.end() - 1)
                for key_at, text_at, form in CALLS[m.group(1)]:
                    if len(args) <= max(key_at, text_at):
                        continue
                    keys = literal_or_branches(args[key_at])
                    texts = literal_or_branches(args[text_at])
                    if not keys or not texts:
                        continue
                    for branch, key in keys:
                        if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", key) or not key.startswith(PREFIXES):
                            continue
                        for tb, english in texts:
                            if branch is None or tb is None or branch == tb:
                                pairs.append((rel, key + form, english))
    return pairs, uses


def read_tailslide(folder):
    """Tailslide's message tables, by code: the errors from E_ERROR, the
    warnings from W_WARNING, each table's rows in the enum's order."""
    src = open(os.path.join(folder, "libtailslide", "logger.cc"), encoding="utf-8").read()
    out = {}
    for name, base in (("_sErrorMessages", 10000), ("_sWarningMessages", 20000)):
        m = re.search(r"%s\[[^\]]*\]\s*=\s*\{(.*?)\};" % name, src, re.S)
        if not m:
            fail("Tailslide's %s not found in logger.cc" % name)
            continue
        body = re.sub(r"//[^\n]*", "", m.group(1))
        rows = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
        for i, row in enumerate(rows):
            out[base + i] = marks_from_printf(unescape_c(row))
    return out


def read_luau_lints(folder):
    """Every emitWarning in Linter.cpp: the lint's name and its template
    as the map would write it."""
    src = open(os.path.join(folder, "Analysis", "src", "Linter.cpp"), encoding="utf-8").read()
    out = {}
    for m in re.finditer(r'emitWarning\(\s*\*?context,\s*LintWarning::Code_([A-Za-z]+),\s*[^,]+,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', src):
        name = m.group(1)
        fmt = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)))
        out.setdefault(name, set()).add(marks_from_printf(unescape_c(fmt)))
    return out


def literals_of(template):
    return [p for p in re.split(r"\[[1-9]\]", template)]


def buildable(piece, literals):
    """Whether a stretch of an error's shape can be put together from the
    string literals of Error.cpp, which builds its messages from such
    pieces -- " value", "s", ", but " -- with numbers and blanks between.
    The longest literal that fits is taken each time; a stretch nothing
    covers is the drift."""
    at = 0
    while at < len(piece):
        if piece[at] in " 0123456789":
            at += 1
            continue
        rest = piece[at:]
        step = 0
        for lit in literals:
            for form in (lit, lit.strip(), lit.lstrip(), lit.rstrip()):
                if form and rest.startswith(form) and len(form) > step:
                    step = len(form)
        if step == 0:
            return False
        at += step
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tailslide", help="a Tailslide checkout (with libtailslide/logger.cc)")
    ap.add_argument("--luau", help="a Luau checkout (with Analysis/src/Linter.cpp)")
    ap.add_argument("--list-luau", action="store_true", help="list the lint templates the map does not have")
    args = ap.parse_args()

    tailslide = args.tailslide or newest(os.path.join(ROOT, "vcpkg", "buildtrees", "tailslide", "src", "*"))
    luau = args.luau or newest(os.path.join(ROOT, "vcpkg", "buildtrees", "slua", "src", "*")) \
        or newest(os.path.join(ROOT, "vcpkg", "buildtrees", "luau", "src", "*"))

    strings = read_strings()
    lsl_rows, lint_rows, shape_rows, err_rows = read_map()
    pairs, uses = read_code_pairs()

    print("code against strings.xml (%d pairs)" % len(pairs))
    for rel, key, english in pairs:
        if key not in strings:
            fail("%s: %s has no string in strings.xml" % (rel, key))
        elif strings[key] != english:
            fail("%s: %s\n    code:  %r\n    xml:   %r" % (rel, key, english, strings[key]))

    print("the map's keys against strings.xml")
    for key, text in [(k, t) for _, k, t in lsl_rows] + [(k, t) for _, k, t in lint_rows + shape_rows] + err_rows:
        if key not in strings:
            fail("%s has no string in strings.xml" % key)
        elif strings[key] != text:
            fail("%s\n    map:   %r\n    xml:   %r" % (key, text, strings[key]))

    print("strings.xml against the code")
    map_keys = {k for _, k, _ in lsl_rows} | {k for _, k, _ in lint_rows + shape_rows} | {k for k, _ in err_rows}
    for key in sorted(strings):
        # A counted form's key is made at run time from its stem.
        stem = key[:-1] if key[-1] in "ABC" else key
        if key not in uses and stem not in uses and key not in map_keys:
            fail("%s is in strings.xml and nothing says it" % key)

    if tailslide:
        print("the LSL table against Tailslide at %s" % os.path.relpath(tailslide, ROOT))
        theirs = read_tailslide(tailslide)
        for code, key, text in lsl_rows:
            if code not in theirs:
                fail("%s: Tailslide has no message %d" % (key, code))
            elif theirs[code] != text:
                fail("%s (%d)\n    map:       %r\n    tailslide: %r" % (key, code, text, theirs[code]))
        ours = {c for c, _, _ in lsl_rows}
        for code, text in sorted(theirs.items()):
            if code not in ours and text and text not in ("ERROR", "WARN", "[1]"):
                print("  (not mapped) %d %r" % (code, text))
    else:
        print("no Tailslide checkout found; --tailslide DIR to name one")

    if luau:
        print("the lint table against Luau at %s" % os.path.relpath(luau, ROOT))
        theirs = read_luau_lints(luau)
        for name, key, text in lint_rows:
            if name not in theirs:
                fail("%s: Luau has no lint %s" % (key, name))
            elif text not in theirs[name]:
                fail("%s (%s) is not a template of the lint:\n    map:  %r\n    luau: %s" % (key, name, text, "\n          ".join(repr(t) for t in sorted(theirs[name]))))
        if args.list_luau:
            ours = {t for _, _, t in lint_rows}
            for name in sorted(theirs):
                for text in sorted(theirs[name]):
                    if text not in ours:
                        print("  (not mapped) %s %r" % (name, text))
        print("the error table's pieces against Luau's Error.cpp, the shaped lints' against Linter.cpp")
        error_src = open(os.path.join(luau, "Analysis", "src", "Error.cpp"), encoding="utf-8").read()
        lint_src = open(os.path.join(luau, "Analysis", "src", "Linter.cpp"), encoding="utf-8").read()
        for src, rows in ((error_src, err_rows), (lint_src, [(k, t) for _, k, t in shape_rows])):
            # The file's literals, a printf template's cut at its %s.
            literals = set()
            for m in re.findall(r'"((?:[^"\\]|\\.)*)"', src):
                literals.update(re.split(r"%(?:\d+\$)?[-+ 0-9.*lz]*[a-zA-Z]", unescape_c(m)))
            literals = sorted(literals, key=len, reverse=True)
            for key, text in rows:
                for piece in literals_of(text):
                    if piece.strip() and not buildable(piece, literals):
                        fail("%s: %r cannot be built from the source's literals" % (key, piece))
    else:
        print("no Luau checkout found; --luau DIR to name one")

    if FAIL:
        print("\n%d drifted" % len(FAIL))
        return 1
    print("\nall agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
