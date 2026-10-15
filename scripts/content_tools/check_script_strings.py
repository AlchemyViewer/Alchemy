"""The studio's words, checked against the three places they live.

The Script Studio says what it says through keys in strings.xml, each with
the English written a second time in the code as the fallback and what the
tests read; and it takes the engines' messages apart again through tables
in almessagemap.cpp transcribed from the engines' own sources, so that a
translator may work from the same English under the same keys. Nothing in
the build keeps the copies together, so this does:

  * every `"Key", "English"` pair the code passes to alSaid, said, fail,
    note, problem and the like, against the key's text in strings.xml --
    the LSL to SLua converter's notes among them, under Slua;
  * every row of the map's LSL table against Tailslide's logger.cc, by
    error code, with %s and %d read as the marks;
  * every row of the lint table against the emitWarning calls in Luau's
    Linter.cpp, by lint name, likewise;
  * every literal stretch of the type-error table against Luau's
    Error.cpp, whose messages are built rather than formatted, so only
    their pieces can be looked for -- each stretch must be buildable
    from the file's own string literals -- and likewise the lints the map
    matches by shape against Linter.cpp; but a row of the table that is
    the parser's, which writes its messages whole where it knows what
    was meant, against Parser.cpp's, its marks standing for anything;
  * every key in the tables against strings.xml, and every studio key in
    strings.xml against the code, for one that nothing says any more;
  * every key alscriptfixes.cpp offers a fix for, its AL_FIXED_KEYS list,
    against strings.xml, so that a problem renamed where it is made does
    not quietly lose its fix;
  * every editor command the studio's Keys preferences list, from
    alkeymap.cpp, against the panel's name for it, and every such name
    against a command. A command with no name is a missing string, which
    QA mode stops the viewer on. The menus' commands the panel names by
    where they are in the menus, in the menus' own words, which
    alscriptkeymap_test holds to their table;
  * every item of the studio's menus against the command table
    (ALScriptStudioCommands): each item names a command something
    registers, and each command registered with `add` is an item -- one
    reached otherwise is registered with `addUnlisted`. A name the table
    does not have does nothing and is greyed, and a name registered twice
    keeps the first. The names are read as the code builds them, in the
    loops that register several; one built in a way this cannot read is
    reported, not passed over.

Luau is found under vcpkg/buildtrees, the newest checkout of slua, and
Tailslide in its Tailslide folder, where the fork has carried it since
0.732.16, unless named:

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
MAP = os.path.join(ROOT, "indra", "alscript", "core", "almessagemap.cpp")
FIXES = os.path.join(ROOT, "indra", "alscript", "lint", "alscriptfixes.cpp")
EDITOR_KEYS = os.path.join(ROOT, "indra", "llui", "code", "alkeymap.cpp")
KEYS_PANEL = os.path.join(ROOT, "indra", "newview", "skins", "default", "xui", "en", "panel_script_studio_keys.xml")
STUDIO_SKIN = os.path.join(ROOT, "indra", "newview", "skins", "default", "xui", "en", "floater_script_studio.xml")
NEWVIEW = os.path.join(ROOT, "indra", "newview")


def walked(top):
    """A folder and every folder under it but tests, whose words are not
    the studio's."""
    out = []
    for here, folders, _ in os.walk(top):
        folders[:] = sorted(f for f in folders if f != "tests")
        out.append(here)
    return out


# llui's Alchemy widgets are a folder a part (base, text, code, diff, vim,
# ...), and so is alscript (core, lint, lsl, lsl/optimizer, ...); newview
# is one folder, its subfolders not code.
CODE = walked(os.path.join(ROOT, "indra", "llui")) + [NEWVIEW] + walked(os.path.join(ROOT, "indra", "alscript"))
# The keys that are the studio's: what strings.xml groups under these
# prefixes is compared; the rest of the file is the viewer's.
PREFIXES = ("Vim", "Preproc", "Optimizer", "Inliner", "LuauLint", "Luau", "LSL", "Workspace", "Analysis", "XUIEdit", "FindBar", "TabStrip", "ScriptFix", "ScriptAction", "Slua")
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
    "noteOnce": [(1, 2, "")],
    "linted": [(0, 1, "")],
    "noteAt": [(1, 2, "")],
    "titled": [(0, 1, "")],
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


def read_fixed_keys():
    """The X(...) entries of alscriptfixes.cpp's AL_FIXED_KEYS list."""
    with open(FIXES, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"#define AL_FIXED_KEYS\(X\)((?:[^\n]*\\\n)*[^\n]*)", text)
    if not m:
        fail("alscriptfixes.cpp has no AL_FIXED_KEYS list")
        return []
    return re.findall(r"X\((\w+)\)", m.group(1))


def read_key_commands():
    """The names the Keys panel looks up: cmd_ and each editor command but
    none. The menus' commands it names by the menus, not by its strings."""
    editor = open(EDITOR_KEYS, encoding="utf-8").read()
    table = re.search(r"NAMES\[\]\s*=\s*\{(.*?)\};", editor, re.S)
    return ["cmd_" + n for n in re.findall(r'"([a-z_]+)"', table.group(1)) if n != "none"] if table else []


SOURCES = {}


def source(path):
    """A file's text, read once."""
    if path not in SOURCES:
        with open(path, encoding="utf-8", errors="replace") as f:
            SOURCES[path] = f.read()
    return SOURCES[path]


def blanked(text, literals=False):
    """The text with its comments blanked, and with `literals` what is
    inside its string and character literals too, every character where
    it was: what is left of a comment's apostrophe or a string's brace is
    a space, so that neither is read as code."""
    out = list(text)

    def blank(begin, end):
        for j in range(begin, min(end, len(text))):
            if out[j] != "\n":
                out[j] = " "

    i, n = 0, len(text)
    while i < n:
        c = text[i]
        raw = re.match(r'R"([^(\s]*)\(', text[i - 1:i + 17]) if c == '"' and i else None
        if raw:
            end = text.find(")%s\"" % raw.group(1), i)
            end = n if end < 0 else end + len(raw.group(1)) + 1
            if literals:
                blank(i + 1, end)
            i = end + 1
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] not in (c, "\n"):
                j += 2 if text[j] == "\\" else 1
            if literals:
                blank(i + 1, j)
            i = j + 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            blank(i, end)
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            blank(i, end)
            i = end
        else:
            i += 1
    return "".join(out)


def closing(shape, at):
    """Where the bracket at `at` closes, in text blanked of its comments
    and literals."""
    depth = 0
    for i in range(at, len(shape)):
        if shape[i] in "([{":
            depth += 1
        elif shape[i] in ")]}":
            depth -= 1
            if depth == 0:
                return i
    return len(shape)


def opening(shape, at):
    """Where the bracket closing at `at` opens, likewise."""
    depth = 0
    for i in range(at, -1, -1):
        if shape[i] in ")]}":
            depth += 1
        elif shape[i] in "([{":
            depth -= 1
            if depth == 0:
                return i
    return 0


def split_top(text, sep):
    """The text split at each `sep` that is nobody's: not a string's, not
    inside a bracket. A `:` of a `::` is no separator."""
    shape = blanked(text, literals=True)
    parts, depth, begin = [], 0, 0
    for i, c in enumerate(shape):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == sep and depth == 0 and not (sep == ":" and ":" in (shape[i - 1:i], shape[i + 1:i + 2])):
            parts.append(text[begin:i].strip())
            begin = i + 1
    parts.append(text[begin:].strip())
    return parts


NAMING = {}


def naming_function(name):
    """What a function that names an enum value by a switch says for each
    -- `case Algorithm::Patience: return "patience";` -- read from its
    definition in the code, by the value's last part, with `default` for
    the rest. Empty where there is no such function."""
    if name in NAMING:
        return NAMING[name]
    NAMING[name] = {}
    define = re.compile(r"\b(?:\w+::)*%s\s*\([^()]*\)\s*(?:const\s*)?\{" % re.escape(name))
    for folder in CODE:
        for path in sorted(glob.glob(os.path.join(folder, "*.cpp"))):
            if not define.search(source(path)):
                continue
            text = blanked(source(path))
            m = define.search(text)
            if not m:
                continue
            body = text[m.end():closing(blanked(text, literals=True), m.end() - 1)]
            waiting = []
            for step in re.finditer(r'\bcase\s+((?:\w+::)*\w+)\s*:|\b(default)\s*:|\breturn\s+"((?:[^"\\]|\\.)*)"\s*;', body):
                if step.group(3) is None:
                    waiting.append((step.group(1) or step.group(2)).split("::")[-1])
                else:
                    NAMING[name].update((label, unescape_c(step.group(3))) for label in waiting)
                    waiting = []
            return NAMING[name]
    return NAMING[name]


def evaluate(expr, env):
    """A command's name as the code builds it, where it can be read: a
    literal, a name in `env` (what a loop binds, or a copy of it), a
    `std::string(...)` of either, a naming function of an enum value
    (naming_function), and a `+` of any of those. An enum value comes
    back as ("value", its name), for a naming function to take; anything
    else, as None."""
    expr = expr.strip()
    while expr.startswith("(") and closing(blanked(expr, literals=True), 0) == len(expr) - 1:
        expr = expr[1:-1].strip()
    terms = split_top(expr, "+")
    if len(terms) > 1:
        said = [evaluate(term, env) for term in terms]
        return "".join(said) if all(isinstance(s, str) for s in said) else None
    if re.fullmatch(r'(?:"(?:[^"\\]|\\.)*"\s*)+', expr):
        return "".join(unescape_c(s) for s in re.findall(r'"((?:[^"\\]|\\.)*)"', expr))
    m = re.fullmatch(r"std::(?:string|string_view)\s*[({](.*)[)}]", expr, re.S)
    if m:
        return evaluate(m.group(1), env)
    if re.fullmatch(r"\w+", expr):
        return env.get(expr)
    m = re.fullmatch(r"(?:\w+::)*(\w+)\s*\((.*)\)", expr, re.S)
    if m:
        value = evaluate(m.group(2), env)
        names = naming_function(m.group(1)) if isinstance(value, tuple) else {}
        return names.get(value[1].split("::")[-1], names.get("default")) if names else None
    if re.fullmatch(r"(?:\w+::)+\w+", expr):
        return ("value", expr)
    return None


def blocks_of(shape):
    """Every {...} of text blanked of its comments and literals, as
    (where it opens, where it closes)."""
    out, stack = [], []
    for i, c in enumerate(shape):
        if c == "{":
            stack.append(i)
        elif c == "}" and stack:
            out.append((stack.pop(), i))
    return out


def scopes(text, shape, blocks, at):
    """What names things at `at`, outermost first, from the function it is
    in: the function, ("function", its parameters' names); each range-for
    over a braced list, ("for", [{name: value expression}, one a turn]),
    a pair's or tuple's names bound together; and each block's locals
    declared in it before `at` with what they copy, ("local", name,
    expression). `shape` is the text blanked of its comments and
    literals, `text` of its comments, `blocks` the shape's blocks_of."""
    out = []
    around = sorted((b for b in blocks if b[0] < at < b[1]), reverse=True)
    for k, (start, _) in enumerate(around):
        # The block's own locals, before `at` or the block inside it that
        # holds `at`, and not inside any other block of its own.
        until = around[k - 1][0] if k else at
        local = r"(?<![\w:])(?:const\s+)?(?:std::string_view|std::string|auto|char)\b\s*[&*]?\s*(\w+)\s*=\s*([^;]+);"
        for m in reversed(list(re.finditer(local, shape[start + 1:until]))):
            if not any(start < inner < start + 1 + m.start() < inner_end for inner, inner_end in blocks):
                out.append(("local", m.group(1), text[start + 1 + m.start(2):start + 1 + m.end(2)]))
        end = start
        while end and shape[end - 1].isspace():
            end -= 1
        while True:
            word = re.search(r"\b(?:const|override|noexcept|mutable)$", shape[max(0, end - 9):end])
            if not word:
                break
            end -= len(word.group(0))
            while end and shape[end - 1].isspace():
                end -= 1
        if not end or shape[end - 1] != ")":
            continue
        begin = opening(shape, end - 1)
        before = shape[max(0, begin - 256):begin].rstrip()
        word = re.search(r"(\w+)$", before)
        inside = text[begin + 1:end - 1]
        if word and word.group(1) == "for":
            parts = split_top(inside, ":")
            if len(parts) != 2 or not parts[1].startswith("{"):
                continue
            binding = re.search(r"\[([^\]]*)\]\s*$", parts[0])
            names = [n.strip() for n in binding.group(1).split(",")] if binding else re.findall(r"\w+", parts[0])[-1:]
            turns = []
            for item in split_top(parts[1][1:-1], ","):
                values = [item]
                if binding:
                    m = re.fullmatch(r"(?:std::(?:pair|tuple)\s*)?\{(.*)\}", item, re.S)
                    values = split_top(m.group(1), ",") if m else []
                if len(values) == len(names):
                    turns.append(dict(zip(names, values)))
            out.append(("for", turns))
        elif word and word.group(1) not in ("if", "while", "switch", "catch") and not before.endswith("]"):
            params = [split_top(p, "=")[0] for p in split_top(inside, ",")]
            out.append(("function", [re.findall(r"\w+", p)[-1] for p in params if re.search(r"\w", p)]))
            break
    return list(reversed(out))


def read_studio_commands():
    """The names the studio's command table is given, as the code gives
    them: the first argument of each `add` or `addUnlisted` call on the
    table, and of each `addEditorCommand` or `addUnlistedEditorCommand`,
    read as evaluate reads it -- once a turn of the range-for loops around
    the call, which bind what it is built from, as `for (const auto&
    [name, step] : { std::pair{ "next_problem", 1 }, ... })` or `for
    (const char* what : { "whitespace", ... })` around `"compare_ignore_"
    + named` of a copy of it. A call whose name is a parameter of the
    function it is in registers its callers' names, not one of its own.
    Returns (listed, unlisted, twice, unread): twice each name registered
    more than once, with the file:line of each; unread the file:line and
    expression of each call whose name cannot be read."""
    listed, unlisted, unread = [], [], []
    where = {}
    call = re.compile(r"(?:\bcommands\(\)|\bmCommands|\bcommands)\s*(?:\.|->)\s*(add|addUnlisted)\s*\(|(?<!::)\b(add(?:Unlisted)?EditorCommand)\s*\(")
    for path in sorted(glob.glob(os.path.join(NEWVIEW, "*.cpp"))):
        # The table's own files: those that name it, or whose header does.
        header = os.path.splitext(path)[0] + ".h"
        if not any("ALScriptStudioCommands" in source(p) for p in (path, header) if os.path.exists(p)):
            continue
        text = blanked(source(path))
        shape = blanked(text, literals=True)
        blocks = blocks_of(shape)
        for m in call.finditer(shape):
            args = arguments(text, m.end() - 1)
            if not args or not args[0]:
                continue
            expr = args[0]
            envs, params, bound = [{}], [], set()
            for scope in scopes(text, shape, blocks, m.start()):
                if scope[0] == "function":
                    params = scope[1]
                elif scope[0] == "for" and scope[1]:
                    envs = [dict(env, **{name: evaluate(value, env) for name, value in turn.items()}) for env in envs for turn in scope[1]]
                    bound.update(scope[1][0])
                elif scope[0] == "local":
                    for env in envs:
                        env[scope[1]] = evaluate(scope[2], env)
                    bound.add(scope[1])
            names = [evaluate(expr, env) for env in envs]
            at = "%s:%d" % (os.path.relpath(path, ROOT), text.count("\n", 0, m.start()) + 1)
            if all(isinstance(name, str) for name in names):
                (unlisted if "Unlisted" in (m.group(1) or m.group(2)) else listed).extend(names)
                for name in names:
                    where.setdefault(name, []).append(at)
            elif not (expr in params and expr not in bound):
                unread.append((at, " ".join(expr.split())))
    twice = sorted((name, ats) for name, ats in where.items() if len(ats) > 1)
    return set(listed), set(unlisted), twice, unread


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
    """A string literal as [(None, text)] -- adjacent literals, "a" "b",
    joined as the compiler joins them -- or a `c ? "a" : "b"` as its two
    branches [(0, a), (1, b)]; nothing for anything else."""
    lits = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'

    def joined(text):
        return "".join(unescape_c(m) for m in re.findall(r'"((?:[^"\\]|\\.)*)"', text))

    m = re.fullmatch(lits, arg, re.S)
    if m:
        return [(None, joined(m.group(1)))]
    m = re.fullmatch(r".*?\?\s*" + lits + r":\s*" + lits, arg, re.S)
    if m:
        return [(0, joined(m.group(1))), (1, joined(m.group(2)))]
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
            text = source(path).replace("\\x01", " ")
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
    warnings from W_WARNING, each table's rows in the enum's order. The
    library's folder is tailslide/ in the fork's tree and libtailslide/ in
    Tailslide's own."""
    logger = os.path.join(folder, "tailslide", "logger.cc")
    if not os.path.exists(logger):
        logger = os.path.join(folder, "libtailslide", "logger.cc")
    src = open(logger, encoding="utf-8").read()
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
    ap.add_argument("--tailslide", help="a Tailslide checkout (with tailslide/logger.cc or libtailslide/logger.cc)")
    ap.add_argument("--luau", help="a Luau checkout (with Analysis/src/Linter.cpp)")
    ap.add_argument("--list-luau", action="store_true", help="list the lint templates the map does not have")
    args = ap.parse_args()

    slua = newest(os.path.join(ROOT, "vcpkg", "buildtrees", "slua", "src", "*"))
    tailslide = args.tailslide
    if not tailslide and slua and os.path.isdir(os.path.join(slua, "Tailslide")):
        tailslide = os.path.join(slua, "Tailslide")
    luau = args.luau or slua or newest(os.path.join(ROOT, "vcpkg", "buildtrees", "luau", "src", "*"))

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
    for key, text in [(k, t) for _, k, t in lint_rows + shape_rows] + err_rows:
        if key not in strings:
            fail("%s has no string in strings.xml" % key)
        elif strings[key] != text:
            fail("%s\n    map:   %r\n    xml:   %r" % (key, text, strings[key]))
    # LSL's rows are Tailslide's words, which the map must match to take
    # a message apart; the skin says them in plainer English of its own,
    # with any of the words the map took out and none it did not.
    for _, key, text in lsl_rows:
        if key not in strings:
            fail("%s has no string in strings.xml" % key)
        elif not set(re.findall(r"\[[1-9]\]", strings[key])) <= set(re.findall(r"\[[1-9]\]", text)):
            fail("%s says words the map does not take\n    map:   %r\n    xml:   %r" % (key, text, strings[key]))

    fixed = read_fixed_keys()
    print("the keys the fixes are for against strings.xml (%d)" % len(fixed))
    for key in fixed:
        if key not in strings:
            fail("alscriptfixes.cpp offers a fix for %s, which strings.xml has no string for" % key)

    commands = read_key_commands()
    panel = {e.get("name"): e.text or "" for e in ET.parse(KEYS_PANEL).getroot().iter("panel.string")}
    print("the Keys panel's names against the commands it lists (%d)" % len(commands))
    if not commands:
        fail("no commands read from alkeymap.cpp")
    for name in commands:
        if name not in panel:
            fail("panel_script_studio_keys.xml has no %s" % name)
    for name in sorted(panel):
        if name.startswith("cmd_") and name not in commands:
            fail("panel_script_studio_keys.xml names %s, which is no command" % name)

    listed, unlisted, twice, unread = read_studio_commands()
    skin = open(STUDIO_SKIN, encoding="utf-8").read()
    items = set(re.findall(r'function="ScriptStudio\.(?:Menu|Enable|Check)"\s+parameter="([^"]*)"', skin))
    print("the studio's menus against its command table (%d items, %d commands)" % (len(items), len(listed | unlisted)))
    if not listed:
        fail("no commands read from the studio's add...Commands functions")
    for at, expr in unread:
        fail("%s registers a command as %s, a name this cannot read; teach read_studio_commands its form" % (at, expr))
    for name, ats in twice:
        fail("the command %s is registered %d times, and the table keeps only the first to run: %s" % (name, len(ats), ", ".join(ats)))
    for name in sorted(items - listed):
        fail("floater_script_studio.xml has an item for %s, which %s" % (name, "is registered unlisted" if name in unlisted else "no command is"))
    for name in sorted(listed - items):
        fail("the command %s is registered with add and is no item of the menus; addUnlisted, if it is reached otherwise" % name)

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
        print("the error table's pieces against Luau's Error.cpp, or whole against Parser.cpp; the shaped lints' against Linter.cpp")
        error_src = open(os.path.join(luau, "Analysis", "src", "Error.cpp"), encoding="utf-8").read()
        lint_src = open(os.path.join(luau, "Analysis", "src", "Linter.cpp"), encoding="utf-8").read()
        parser_src = open(os.path.join(luau, "Ast", "src", "Parser.cpp"), encoding="utf-8").read()
        # The parser's rows: each is one of its messages, written whole with
        # what the marks stand for in place -- `Unexpected '&&'; did you
        # mean 'and'?` -- which Error.cpp's pieces would never build.
        said_whole = [unescape_c(m) for m in re.findall(r'"((?:[^"\\]|\\.)*)"', parser_src)]
        built_rows = [(key, text) for key, text in err_rows
                      if not any(re.fullmatch("(?:.+)".join(re.escape(p) for p in literals_of(text)), m, re.S) for m in said_whole)]
        for src, rows in ((error_src, built_rows), (lint_src, [(k, t) for _, k, t in shape_rows])):
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
