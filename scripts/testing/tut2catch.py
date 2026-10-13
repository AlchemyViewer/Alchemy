#!/usr/bin/env python3
"""Converts TUT test files to idiomatic Catch2 v3, in place.

Each file is read through clang's own AST of it, so what a call means is
decided by the types clang resolved, not by its text:

  structure    every test<N> of a group becomes
                 TEST_CASE_METHOD(<data>, "<group>: <name>", "[<group tag>]")
               with the group's data struct as the fixture, built fresh for
               each case as TUT built it for each test; or a plain
               TEST_CASE("<group>: <name>", "[<group tag>]") where the struct
               holds nothing and nothing else names it, and the struct goes.
               The name is, first, one --names gives (for #if twins, a
               list, one a twin in declaration order); then the test's
               set_test_name literal; then the first sentence of the
               comment written above the test, where that is prose; else
               #N. " #N" is added to a name another case of the file has
               (ignoring case), unless the two are in different branches
               of one #if, never compiled together. TUT ran a file's groups
               in the byte order of their names, each group's tests by
               number; Catch2 runs cases as declared, so a test declared
               before one TUT ran ahead of it moves later in the file,
               never earlier, to just after it. A move stays in its
               namespace (a later block of it is the same scope) and #if
               branch; where one cannot, the order is left (each group's
               tests still put in numeric order where they can be) and the
               file marked. The group's typedefs, its
               test_group object and a class derived from test_group only
               to name it go; a class derived from it that holds more (a
               constructor's body, a destructor, members) stays, with its
               object, marked. namespace tut, once nothing names tut:: and
               no type production code befriends as tut:: is in it, becomes
               an anonymous namespace, taking in one directly inside it.
  assertions   CHECK, which lets a case go on and so reports every failure
               of a run; REQUIRE where going on could only crash or bury the
               first failure: a condition that guards what follows (a
               pointer, null or not; an optional or a smart pointer, tested
               through its operator bool or compared with nullptr; a
               position compared with end() or npos; a size, an emptiness,
               a presence: has(), has_value(), found() and the like,
               answered with a bool or an integer, not a vector's float
               length()); a
               condition that calls what makes it (a non-const member
               function returning bool, or a call returning bool that fills
               an argument passed by reference or address), where that
               object or argument is a local declared without an
               initializer, written by nothing since, that the test reads
               after it (LLMatrix4a m; ensure(m.setInverse(n))), so
               indeterminate had the call failed; a term of a split && chain whose variable a later
               term of it leans on for its safety, dereferencing it or
               indexing by it (TUT's ensure stopped the test at the first
               term that failed; v.mV[VX] == 0 && v.mV[VY] == 0 stays two
               CHECKs); in a loop or a lambda; and in a helper outside a
               test's body. A plain compare of values stays CHECK. In a
               destructor, where a throw would end the process, CHECK
               always. With that macro (X below):
               ensure(c) -> X(c); ensure(a && b && ...) -> X(a); X(b); ...
               one assertion a term, so that each prints its operands
               (whole, where a term after the first changes something, ++,
               -- or an assignment, which TUT's && ran only past the terms
               before it);
               ensure(msg, c) -> UNSCOPED_INFO(msg) then X(c), or, where c
               split, { INFO(msg); X(a); X(b); } so the message goes with
               each; braced where the call was a lone statement of an if,
               else or loop; ensure_not -> X_FALSE; ensure_equals -> X(a ==
               b), except where both values are LLSD (X_THAT with
               ll_test::LLSDEquals, the deep compare TUT's overload made)
               or LLURI (their asString()s, as TUT compared them);
               ensure_not_equals -> X(a != b); ensure_distance ->
               ll_test::WithinDistance<T>, which, as TUT's did, fails a
               value on the border and works in T, the type TUT's
               template took; ensure_approximately_equals_range ->
               WithinAbs, the actual, the expected and the delta narrowed
               to F32 where they are not, as TUT's parameters narrowed them
               (a NaN, which TUT's let pass, fails it);
               ensure_approximately_equals -> ll_test::WithinBits, at the
               precision of the overload TUT chose; ensure_contains,
               _does_not_contain, _starts_with, _ends_with -> the string
               matchers; ensure_memory_matches -> ll_test::MemoryMatches;
               fail -> FAIL; skip -> SKIP; throw tut::failure(m) -> FAIL(m);
               get_test_name() -> ll_test::currentTestName(); a catch of
               tut::failure that rethrows -> ll_test::Failure. A condition
               is written so that Catch2 decomposes it and prints its
               operands: parentheses the test wrote around the whole go; a
               literal compared on the left moves right; x == true is X(x)
               and x == false X_FALSE(x); strcmp(a, b) == 0 compares
               std::string_views. Operands are parenthesised where a macro
               or an operator would split or rebind them; a condition
               Catch2 cannot decompose (||, ?:) is parenthesised whole.
               None of this reaches into a macro the test used: a && or
               parentheses a macro made (WRAP(a && b), a && in a macro's
               body) are not split or dropped, and the condition goes as
               the test wrote it, macro and all.
  includes     lltut.h and tut.hpp give way to the Catch2 headers the file
               now uses, and altestmatchers.h, altestname.h or
               altestfailure.h where it uses them.

Left as they are: #if guards (a test or call in a branch this host does not
compile is converted from its text where its types do not matter), code
under #if 0 and in comments, #include "*.cpp" lines, and every type the
production code names as a friend.

What it cannot decide it leaves unconverted, marked // TM-CHECK: <why> (or
/* TM-CHECK: */ inside a macro), and it exits 1 listing every mark; 2 if a
file could not be read at all, or the converter itself failed on one (its
traceback is that file's error, and the other files go on). It never
drops a test or an assertion: --mapping writes, as {"tests": [...],
"files": [...]}, one entry per test, with the TUT-side assertion calls in
its body and the Catch2 assertions now in it, and one per file, with the
sha256 of the TUT source it read (tut_sha256) and of the Catch2 text it
wrote, or would have under --dry-run (catch2_sha256), so that a file edited
since shows, and the TUT assertion calls outside every test's body, in
code not under #if 0 (tut_outside_calls), with the Catch2 assertions they
became (catch2_outside_expected), all of which scripts/testing/tut_parity.py
checks.

Needs the build tree's compile_commands.json (CMake writes it; the presets
turn it on) and the compiler it names, which must be clang: the AST comes
from clang++ -fsyntax-only -Xclang -ast-dump=json with the file's own flags.

A test header #included by a test file (llcorehttp's) is read through the
file that includes it. TUT2CATCH_DUMP=<file> writes the file's own AST
declarations there, as JSON, to see what the converter saw. A clang AST of a viewer test runs to gigabytes of
JSON; only the file's own declarations are parsed, but each file still
takes from seconds to a minute, so --jobs helps.

    python3 -I scripts/testing/tut2catch.py indra/llmath/tests/v3math_test.cpp --mapping /tmp/map.json
    python3 -I scripts/testing/tut2catch.py --dry-run --jobs 6 indra/*/tests/*_test.cpp
"""
import argparse
import bisect
import concurrent.futures
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import threading
import traceback

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_BUILD = "build-Darwin-ninja-os"
DEFAULT_CONFIG = "RelWithDebInfo"

# TUT's free assertion functions, and the members of test_object a test calls.
TUT_FUNCTIONS = {
    "ensure", "ensure_not", "ensure_equals", "ensure_not_equals", "ensure_distance",
    "ensure_approximately_equals", "ensure_approximately_equals_range", "ensure_memory_matches",
    "ensure_contains", "ensure_does_not_contain", "ensure_starts_with", "ensure_ends_with",
    "ensure_errno", "fail", "skip",
}
TUT_MEMBERS = {"set_test_name", "get_test_name"}
# Whose conversion does not depend on the types of the arguments.
TYPE_FREE = {"ensure", "ensure_not", "fail", "skip"}
# What counts as an assertion on each side, for the parity count.
TUT_ASSERTING = TUT_FUNCTIONS
CATCH_ASSERTION = re.compile(
    rb"(?<![\w.>:])(?:REQUIRE|REQUIRE_FALSE|REQUIRE_THAT|CHECK|CHECK_FALSE|CHECK_THAT|FAIL|FAIL_CHECK|SKIP)\s*\(")
TUT_INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*[<"](?:[^>"]*/)?(?:lltut\.h|tut\.hpp)[>"][^\n]*\n?', re.M)

IMPLICIT = {"ImplicitCastExpr", "MaterializeTemporaryExpr", "CXXBindTemporaryExpr", "ExprWithCleanups",
            "ConstantExpr", "SubstNonTypeTemplateParmExpr", "FullExpr"}
BIN_PREC = {
    ".*": 14, "->*": 14, "*": 13, "/": 13, "%": 13, "+": 12, "-": 12, "<<": 11, ">>": 11, "<=>": 10,
    "<": 9, ">": 9, "<=": 9, ">=": 9, "==": 8, "!=": 8, "&": 7, "^": 6, "|": 5, "&&": 4, "||": 3,
    "=": 2, "*=": 2, "/=": 2, "%=": 2, "+=": 2, "-=": 2, "<<=": 2, ">>=": 2, "&=": 2, "^=": 2, "|=": 2,
    ",": 1,
}
STATEMENT_PARENTS = {"CompoundStmt", "CaseStmt", "DefaultStmt", "LabelStmt", "AttributedStmt"}
BODY_PARENTS = {"IfStmt", "ForStmt", "WhileStmt", "DoStmt", "CXXForRangeStmt"}
FUNCTION_KINDS = {"FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl", "CXXDestructorDecl",
                  "CXXConversionDecl", "LambdaExpr"}
LOOP_KINDS = {"ForStmt", "WhileStmt", "DoStmt", "CXXForRangeStmt"}
# What a condition asks of an object when what follows relies on the answer:
# that it holds something, or how much.
GUARD_MEMBERS = {"size", "empty", "length", "count", "isDefined", "isUndefined", "has", "isMap", "isArray",
                 "notNull", "isNull", "has_value", "found"}
# What a member answering how much or whether answers with.
INTEGRAL = re.compile(r"(?:(?:unsigned|signed|short|long|int|char|bool|wchar_t|char8_t|char16_t|char32_t|"
                      r"__int128)\s*)+")
# A container's ends, which a position found in it is compared with: at one,
# there is nothing to read.
END_MEMBERS = {"end", "cend", "rend", "crend"}
# The arithmetic types: a constant of one is no state a later term relies on.
ARITHMETIC = re.compile(r"(?:(?:unsigned|signed|short|long|int|char|float|double|bool|wchar_t|char8_t|char16_t|"
                        r"char32_t|size_t|F32|F64|S8|U8|S16|U16|S32|U32|S64|U64)\s*)+")
LITERAL_KINDS = {"IntegerLiteral", "FloatingLiteral", "CXXBoolLiteralExpr", "CharacterLiteral", "StringLiteral",
                 "CXXNullPtrLiteralExpr", "GNUNullExpr", "FixedPointLiteral"}
# A comment that names nothing: a separator, or a note to whoever reads on.
NOT_A_NAME = re.compile(r"^(?:[-=*/#~_ ]*|(?:TODO|FIXME|XXX|HACK|NB|NOTE|BUG)\b.*)$", re.I)


class ConvertError(Exception):
    pass


# --------------------------------------------------------------------------
# The source text
# --------------------------------------------------------------------------
def mask_literal(data, out, start, end):
    """Masks a literal's inside, but for each line splice in it."""
    for k in range(start, end):
        if data[k] == 0x0A or (data[k] == 0x5C and k + 1 < len(data) and data[k + 1] == 0x0A):
            continue
        out[k] = 0x5F


def mask_source(data):
    """The text with comments blanked (newlines kept) and the insides of string
    and character literals replaced, so a search of it finds only code. Every
    offset means what it meant, and every line: a line splice in a literal,
    a backslash and its newline, stays, as one in code does."""
    out = bytearray(data)
    n = len(data)
    i = 0
    while i < n:
        c = data[i]
        if c == 0x2F and i + 1 < n and data[i + 1] == 0x2F:  # //
            j = i
            while j < n and data[j] != 0x0A:
                if data[j] == 0x5C and j + 1 < n and data[j + 1] == 0x0A:
                    j += 2
                    continue
                j += 1
            for k in range(i, j):
                if out[k] != 0x0A:
                    out[k] = 0x20
            i = j
        elif c == 0x2F and i + 1 < n and data[i + 1] == 0x2A:  # /*
            j = data.find(b"*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != 0x0A:
                    out[k] = 0x20
            i = j
        elif c == 0x22:  # "
            # A raw string: R"delim( ... )delim", after an optional prefix.
            k = i - 1
            if k >= 0 and data[k] == ord("R"):
                start = k
                while start > 0 and chr(data[start - 1]) in "u8UL":
                    start -= 1
                if start == 0 or not (chr(data[start - 1]).isalnum() or data[start - 1] == 0x5F):
                    paren = data.find(b"(", i)
                    delim = data[i + 1:paren]
                    end = data.find(b")" + delim + b'"', paren)
                    if paren >= 0 and end >= 0 and len(delim) <= 16:
                        for m in range(i + 1, end + len(delim) + 1):
                            if out[m] != 0x0A:
                                out[m] = 0x5F
                        i = end + len(delim) + 2
                        continue
            j = i + 1
            while j < n and data[j] != 0x22 and data[j] != 0x0A:
                if data[j] == 0x5C:
                    j += 1
                j += 1
            mask_literal(data, out, i + 1, min(j, n))
            i = j + 1
        elif c == 0x27:  # '
            # A digit separator, inside a number, is no literal.
            k = i - 1
            while k >= 0 and (chr(data[k]).isalnum() or data[k] in (0x5F, 0x27, 0x2E)):
                k -= 1
            if k + 1 < i and chr(data[k + 1]).isdigit():
                i += 1
                continue
            j = i + 1
            while j < n and data[j] != 0x27 and data[j] != 0x0A:
                if data[j] == 0x5C:
                    j += 1
                j += 1
            mask_literal(data, out, i + 1, min(j, n))
            i = j + 1
        else:
            i += 1
    return bytes(out)


def c_unescape(body):
    """The value of the inside of a narrow string literal."""
    out = []
    i = 0
    simple = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'", "a": "\a",
              "b": "\b", "f": "\f", "v": "\v", "?": "?"}
    while i < len(body):
        ch = body[i]
        if ch == "\\" and i + 1 < len(body):
            nxt = body[i + 1]
            if nxt == "x":
                m = re.match(r"[0-9A-Fa-f]+", body[i + 2:])
                out.append(chr(int(m.group(0), 16)) if m else "x")
                i += 2 + (len(m.group(0)) if m else 0)
                continue
            if nxt in "01234567":
                m = re.match(r"[0-7]{1,3}", body[i + 1:])
                out.append(chr(int(m.group(0), 8)))
                i += 1 + len(m.group(0))
                continue
            out.append(simple.get(nxt, nxt))
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def c_escape(text):
    return (text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t"))


class Source:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.masked = mask_source(self.data)
        self.line_starts = [0] + [m.end() for m in re.finditer(rb"\n", self.data)]
        self.directives = []  # (start, end, keyword, rest) of each preprocessor line
        for m in re.finditer(rb"^[ \t]*#[ \t]*(\w+)", self.masked, re.M):
            end = m.end()
            while True:
                nl = self.masked.find(b"\n", end)
                if nl < 0:
                    nl = len(self.masked)
                    break
                if self.masked[nl - 1:nl] == b"\\":
                    end = nl + 1
                    continue
                break
            self.directives.append((m.start(), nl, m.group(1).decode(), self.masked[m.end():nl].decode("latin-1")))
        self.directive_starts = [d[0] for d in self.directives]
        self.regions = []  # (start, end, "dead" | "inactive") of conditional branches

    def line_of(self, off):
        return bisect.bisect_right(self.line_starts, off)

    def line_start(self, off):
        return self.line_starts[self.line_of(off) - 1]

    def line_end(self, off):
        nl = self.data.find(b"\n", off)
        return len(self.data) if nl < 0 else nl

    def indent_at(self, off):
        start = self.line_start(off)
        m = re.match(rb"[ \t]*", self.data[start:])
        return self.data[start:start + m.end()]

    def starts_line(self, off):
        return self.data[self.line_start(off):off].strip() == b""

    def directive_at(self, off):
        i = bisect.bisect_right(self.directive_starts, off) - 1
        if i >= 0 and self.directives[i][0] <= off < self.directives[i][1]:
            return self.directives[i]
        return None

    def skip_space(self, off):
        while off < len(self.masked) and self.masked[off] in b" \t\r\n":
            off += 1
        return off

    def skip_space_back(self, off):
        while off > 0 and self.masked[off - 1] in b" \t\r\n":
            off -= 1
        return off

    def matching(self, off):
        """The offset of the bracket that closes the one at off."""
        pairs = {ord("("): ord(")"), ord("["): ord("]"), ord("{"): ord("}")}
        stack = []
        i = off
        while i < len(self.masked):
            c = self.masked[i]
            if c in pairs:
                stack.append(pairs[c])
            elif c in (0x29, 0x5D, 0x7D):
                if not stack or stack.pop() != c:
                    return -1
                if not stack:
                    return i
            i += 1
        return -1

    def conditionals_at(self, off):
        """The #if chains around off, outermost first: each as (where its #if
        is, which of its branches holds off, counting from 0)."""
        stack = []
        for start, end, kw, _ in self.directives:
            if start > off:
                break
            if kw in ("if", "ifdef", "ifndef"):
                stack.append((start, 0))
            elif kw in ("elif", "else", "elifdef", "elifndef") and stack:
                stack[-1] = (stack[-1][0], stack[-1][1] + 1)
            elif kw == "endif" and stack:
                stack.pop()
        return tuple(stack)

    def state(self, off):
        """dead (under #if 0), inactive (a branch this host does not compile) or active."""
        result = "active"
        for start, end, what in self.regions:
            if start <= off < end:
                if what == "dead":
                    return "dead"
                result = "inactive"
        return result

    def find_conditionals(self, active_lines):
        """Each #if branch's extent, and whether it is dead or not compiled here."""
        stack = []
        branches = []  # (start, end, keyword, condition, nested ranges)
        for start, end, kw, rest in self.directives:
            if kw in ("if", "ifdef", "ifndef"):
                stack.append({"branches": [], "open": (end, kw, rest.strip()), "dead_before": False})
            elif kw in ("elif", "elifdef", "elifndef", "else", "endif") and stack:
                top = stack[-1]
                bstart, bkw, brest = top["open"]
                top["branches"].append((bstart, start, bkw, brest))
                if kw == "endif":
                    stack.pop()
                    branches.extend(top["branches"])
                else:
                    top["open"] = (end, kw, rest.strip())
        for bstart, bend, kw, cond in branches:
            if kw in ("if", "elif") and re.fullmatch(r"\(?\s*(0|false)\s*\)?", cond):
                self.regions.append((bstart, bend, "dead"))
                continue
            # A branch is compiled if a line of its own code reached the
            # preprocessor's output. Lines of nested branches are theirs.
            nested = [(s, e) for s, e, _, _ in branches if bstart < s and e <= bend]
            has_code = False
            compiled = False
            for line in range(self.line_of(bstart) + 1, self.line_of(max(bstart, bend - 1)) + 1):
                ls = self.line_starts[line - 1]
                le = self.line_end(ls)
                if any(s <= ls < e for s, e in nested) or self.directive_at(ls):
                    continue
                if self.masked[ls:le].strip():
                    has_code = True
                    if line in active_lines:
                        compiled = True
                        break
            if has_code and not compiled:
                self.regions.append((bstart, bend, "inactive"))


# --------------------------------------------------------------------------
# The compile database and clang
# --------------------------------------------------------------------------
class CompileDatabase:
    """Each file's compile command in one configuration. A file compiled by
    more than one target -- a TUT test and, mid-migration, its Catch2
    conversion -- is read as its TUT target compiles it, the one with TUT's
    headers on its path."""

    def __init__(self, build_dir, config):
        path = os.path.join(build_dir, "compile_commands.json")
        if not os.path.exists(path):
            raise ConvertError(f"no {path}: configure the build tree first")
        with open(path) as f:
            entries = json.load(f)
        self.entries = {}
        tut = os.path.join("externals", "tut") + " "
        for e in entries:
            key = os.path.normpath(os.path.join(e["directory"], e["file"]))
            command = e.get("command") or shlex.join(e["arguments"])
            # Ranked on the raw text: splitting thousands of commands costs
            # seconds, and one is all a file needs.
            rank = (f'-DCMAKE_INTDIR=\\"{config}\\"' in command or f'-DCMAKE_INTDIR="{config}"' in command,
                    tut in command or command.rstrip().endswith(tut.strip()))
            have = self.entries.get(key)
            if have is None or rank > have[2]:
                self.entries[key] = (e["directory"], command, rank)

    def command(self, path):
        """(directory, clang arguments, the file compiled): a header of tests,
        such as llcorehttp's, is read through the file that includes it."""
        path = os.path.normpath(path)
        entry = self.entries.get(path)
        compiled = path
        if entry is None:
            folder = os.path.dirname(path)
            include = re.compile(r'^\s*#\s*include\s*"(?:[^"]*/)?' + re.escape(os.path.basename(path)) + '"', re.M)
            for candidate in sorted(self.entries):
                if os.path.dirname(candidate) != folder or candidate == path:
                    continue
                try:
                    with open(candidate, encoding="utf-8", errors="replace") as f:
                        if include.search(f.read()):
                            entry, compiled = self.entries[candidate], candidate
                            break
                except OSError:
                    continue
        if entry is None:
            raise ConvertError(f"{path} is in no compile command: is it built by any target?")
        directory, args = entry[0], shlex.split(entry[1])
        out = ["clang++"]
        skip = False
        for a in args[1:]:
            if skip:
                skip = False
                continue
            if a in ("-o", "-c", "-MF", "-MT", "-MQ"):
                skip = True
                continue
            if a in ("-MD", "-MMD", "-fcolor-diagnostics") or a.startswith("-fdiagnostics-color"):
                continue
            if os.path.normpath(os.path.join(directory, a)) == compiled:
                continue
            out.append(a)
        return directory, out, compiled


def split_ast_items(stream):
    """The AST's top-level declarations, as JSON bytes, without parsing the
    whole dump: they are the items of the translation unit's "inner" array,
    and clang writes each between lines indented four spaces."""
    sep = b"\n    },\n    {\n"
    buf = b""
    started = False
    while True:
        chunk = stream.read(1 << 24)
        if not chunk:
            break
        buf += chunk
        if not started:
            k = buf.find(b'  "inner": [\n    {\n')
            if k < 0:
                continue
            buf = buf[k + len(b'  "inner": [\n'):]
            started = True
        pos = 0
        while True:
            k = buf.find(sep, pos)
            if k < 0:
                break
            yield buf[pos:k + 6]
            pos = k + 7
        buf = buf[pos:]
    if started:
        k = buf.rfind(b"\n    }\n")
        if k >= 0:
            yield buf[:k + 6]


FILE_ATTR = re.compile(rb'"file": "((?:[^"\\]|\\.)*)"')


def last_file(text, current):
    """The file of the last location written in text: clang writes a
    location's file only when it changes, so this is the file in force after
    it. An includedFrom's file is not a location's."""
    end = len(text)
    while True:
        i = text.rfind(b'"file": "', 0, end)
        if i < 0:
            return current
        j = i
        while j > 0 and text[j - 1] in b" \t\r\n":
            j -= 1
        if text.endswith(b'"includedFrom": {', 0, j):
            end = i
            continue
        return json.loads(b'"' + FILE_ATTR.match(text, i).group(1) + b'"')


def item_files(text, current):
    """The files of a top-level item's own location: spelling and expansion."""
    i = text.find(b'"loc": {')
    if i < 0:
        return set()
    j = text.find(b'"range": ', i)
    head = text[i + len(b'"loc": '):j if j > 0 else i + 4096].decode("utf-8", "replace")
    loc, _ = json.JSONDecoder().raw_decode(head)
    files = set()
    state = [current]

    def visit(obj):
        if "offset" in obj:
            state[0] = obj.get("file", state[0])
            files.add(state[0])
        for key in ("spellingLoc", "expansionLoc"):
            if key in obj:
                visit(obj[key])
    visit(loc)
    return files


def annotate(obj, state):
    """Gives every location in obj its file, as "_f", walking in the order
    clang wrote them."""
    if isinstance(obj, dict):
        if "offset" in obj:
            state[0] = obj.get("file", state[0])
            obj["_f"] = os.path.normpath(state[0]) if state[0] else None
        for key, value in obj.items():
            if key == "includedFrom":
                continue
            if isinstance(value, (dict, list)):
                annotate(value, state)
    elif isinstance(obj, list):
        for value in obj:
            annotate(value, state)


TUT_DECL = re.compile(
    rb'"id": "(0x[0-9a-f]+)",\s*"kind": "(?:FunctionDecl|CXXMethodDecl|FunctionTemplateDecl)",'
    rb'(?:(?!"kind": ).)*?"name": "(\w+)"', re.S)


def is_tut_header(path):
    path = os.path.normpath(path)
    return path.endswith(os.path.join("test", "lltut.h")) or (os.sep + os.path.join("tut", "tut")) in path


def run_clang(db, path):
    """The main file's top-level declarations, parsed, the ids of TUT's own
    functions, and the lines of the main file the preprocessor kept."""
    directory, args, compiled = db.command(path)
    main = os.path.normpath(path)
    preprocess = subprocess.Popen(args + ["-E", compiled], cwd=directory, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL)
    dump = subprocess.Popen(args + ["-fsyntax-only", "-Xclang", "-ast-dump=json", compiled], cwd=directory,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    errors = []
    reader = threading.Thread(target=lambda: errors.append(dump.stderr.read()))
    reader.start()
    items = []
    tut_ids = {}
    current = None
    for text in split_ast_items(dump.stdout):
        files = {os.path.normpath(f) for f in item_files(text, current) if f}
        if main in files:
            items.append((current, json.loads(text)))
        elif any(is_tut_header(f) for f in files):
            for m in TUT_DECL.finditer(text):
                name = m.group(2).decode()
                if name in TUT_FUNCTIONS or name in TUT_MEMBERS:
                    tut_ids[m.group(1).decode()] = name
        current = last_file(text, current)
    dump.wait()
    reader.join()
    if dump.returncode != 0:
        stderr = b"".join(errors).decode("utf-8", "replace")
        preprocess.kill()
        preprocess.wait()
        raise ConvertError(f"clang could not parse {compiled}:\n{stderr[-4000:]}")
    active_lines = set()
    marker = re.compile(rb'^# (\d+) "((?:[^"\\]|\\.)*)"')
    line = 0
    in_main = False
    for raw in preprocess.stdout.read().split(b"\n"):
        m = marker.match(raw)
        if m:
            line = int(m.group(1))
            in_main = os.path.normpath(os.path.join(directory, m.group(2).decode("utf-8", "replace"))) == main
            continue
        if in_main and raw.strip():
            active_lines.add(line)
        line += 1
    preprocess.wait()
    for current_file, item in items:
        annotate(item, [current_file])
    return [item for _, item in items], tut_ids, active_lines


def demangle(names):
    if not names:
        return {}
    for tool in (["xcrun", "c++filt"], ["c++filt"], ["llvm-cxxfilt"]):
        try:
            out = subprocess.run(tool, input="\n".join(names), capture_output=True, text=True, check=True)
            return dict(zip(names, out.stdout.splitlines()))
        except (OSError, subprocess.CalledProcessError):
            continue
    raise ConvertError("no c++filt to demangle with")


# --------------------------------------------------------------------------
# Reading the AST
# --------------------------------------------------------------------------
def skip_implicit(node):
    while True:
        kind = node.get("kind")
        inner = [c for c in node.get("inner", []) if isinstance(c, dict) and "kind" in c]
        if kind in IMPLICIT and inner:
            node = inner[0]
            continue
        if kind in ("CXXConstructExpr", "CXXMemberCallExpr") and len(inner) == 1 and \
                node.get("range") and inner[0].get("range") and \
                loc_key(node["range"]["begin"]) == loc_key(inner[0]["range"]["begin"]) and \
                loc_key(node["range"]["end"]) == loc_key(inner[0]["range"]["end"]):
            node = inner[0]
            continue
        return node


def loc_key(loc):
    if "spellingLoc" in loc:
        loc = loc["spellingLoc"]
    return (loc.get("_f"), loc.get("offset"))


def value_type(node):
    t = node.get("type", {})
    text = t.get("desugaredQualType") or t.get("qualType") or ""
    text = re.sub(r"\b(const|volatile)\b", "", text).replace("&", "")
    return re.sub(r"\s+", " ", text).strip()


def precedence(node):
    node = skip_implicit(node)
    kind = node.get("kind")
    if kind in ("BinaryOperator", "CompoundAssignOperator"):
        return BIN_PREC.get(node.get("opcode"), 16)
    if kind in ("ConditionalOperator", "BinaryConditionalOperator", "CXXThrowExpr"):
        return 2
    if kind == "CXXRewrittenBinaryOperator":
        return 8
    if kind == "CXXOperatorCallExpr":
        inner = [c for c in node.get("inner", []) if "kind" in c]
        callee = skip_implicit(inner[0]) if inner else {}
        name = callee.get("referencedDecl", {}).get("name", "")
        op = name[len("operator"):] if name.startswith("operator") else ""
        if op in ("()", "[]", "->", "++", "--"):
            return 16
        if len(inner) == 2:
            return 15
        return BIN_PREC.get(op, 16)
    if kind in ("UnaryOperator", "CStyleCastExpr", "CXXNewExpr", "CXXDeleteExpr", "UnaryExprOrTypeTraitExpr"):
        return 15
    return 16


def headers_for(name, kind):
    """What a converted call needs included, beyond the test macros."""
    if kind == "equals-llsd" or name in ("ensure_approximately_equals", "ensure_memory_matches", "ensure_distance"):
        return {'"altestmatchers.h"'}
    if name == "ensure_approximately_equals_range":
        return {"<catch2/matchers/catch_matchers_floating_point.hpp>"}
    if name in ("ensure_contains", "ensure_does_not_contain", "ensure_starts_with", "ensure_ends_with"):
        return {"<catch2/matchers/catch_matchers_string.hpp>"}
    return set()


def decomposable(node):
    """Whether REQUIRE(node) compiles as Catch2 decomposes it: not &&, ||,
    ?:, an assignment or a comma at the top, and one comparison at most."""
    node = skip_implicit(node)
    p = precedence(node)
    if p <= 4:
        return False
    if 5 <= p <= 9:
        inner = [c for c in node.get("inner", []) if "kind" in c]
        if node.get("kind") == "CXXOperatorCallExpr":
            inner = inner[1:]
        if inner and precedence(inner[0]) <= 9:
            return False
    return True


def unparenthesised(node):
    """node, without the parentheses a test wrote around a whole Catch2 can
    decompose, and without the implicit nodes around that."""
    node = skip_implicit(node)
    while node.get("kind") == "ParenExpr":
        inner = [c for c in node.get("inner", []) if "kind" in c]
        if not inner or not decomposable(inner[0]):
            break
        node = skip_implicit(inner[0])
    return node


def operands(node):
    """A comparison's two operands, implicit nodes and all, or None."""
    node = skip_implicit(node)
    inner = [c for c in node.get("inner", []) if "kind" in c]
    if node.get("kind") == "BinaryOperator" and len(inner) == 2:
        return inner
    return None


def is_literal(node):
    node = skip_implicit(node)
    if node.get("kind") == "UnaryOperator" and node.get("opcode") in ("-", "+"):
        inner = [c for c in node.get("inner", []) if "kind" in c]
        return bool(inner) and is_literal(inner[0])
    if node.get("kind") == "DeclRefExpr" and node.get("referencedDecl", {}).get("kind") == "EnumConstantDecl":
        return True
    return node.get("kind") in LITERAL_KINDS


def strcmp_args(node):
    """The two arguments of a strcmp() call, or None."""
    node = skip_implicit(node)
    if node.get("kind") != "CallExpr":
        return None
    inner = [c for c in node.get("inner", []) if "kind" in c]
    if len(inner) != 3:
        return None
    callee = inner[0]
    while callee.get("kind") in ("ImplicitCastExpr", "ParenExpr") and callee.get("inner"):
        callee = callee["inner"][0]
    if callee.get("kind") != "DeclRefExpr" or callee.get("referencedDecl", {}).get("name") not in \
            ("strcmp", "__builtin_strcmp"):
        return None
    return inner[1:]


def value_args(name, args):
    """The arguments a TUT call asserts about, without its message."""
    if name in ("ensure", "ensure_not"):
        return args[-1:]
    if name in ("ensure_equals", "ensure_not_equals"):
        return args[-2:]
    if name in ("ensure_distance", "ensure_approximately_equals", "ensure_approximately_equals_range"):
        return args[-3:]
    if name in ("ensure_contains", "ensure_does_not_contain", "ensure_starts_with", "ensure_ends_with"):
        return args[1:]
    if name == "ensure_memory_matches":
        return args[-4:]
    return []


def name_from_comment(text):
    """A case's name from the comment above its test: the first sentence of
    the comment's first paragraph, or None where the comment is not prose."""
    lines = []
    for raw in text.splitlines():
        s = raw.strip()
        s = re.sub(r"^(?://+!?|/\*+!?|\*+(?!/))\s?", "", s)
        s = re.sub(r"\*+/\s*$", "", s).strip()
        s = re.sub(r"^@(?:brief|test|note)\s+", "", s)
        lines.append(s)
    paragraph = []
    for s in lines:
        if not s:
            if paragraph:
                break
            continue
        paragraph.append(s)
    text = re.sub(r"\s+", " ", " ".join(paragraph)).strip()
    if not text or NOT_A_NAME.match(text):
        return None
    # Commented-out code, or a test's number, names nothing.
    if re.search(r";\s*$|[{}]|==|->|::\w+\(|^\w+\s*\(.*\)\s*;?$|\btest\s*<\s*\d+\s*>|^#", text):
        return None
    if len(re.findall(r"[A-Za-z]{2,}", text)) < 2:
        return None
    # The first sentence: a full stop before a space or the end, not the one
    # in "e.g." or after an initial.
    sentence = text
    for m in re.finditer(r"[.!?](?=\s|$)", text):
        if re.search(r"(?:\be\.g|\bi\.e|\betc|\bvs|\bcf|\b[A-Za-z])$", text[:m.start()]):
            continue
        sentence = text[:m.start() + (1 if text[m.start()] in "!?" else 0)]
        break
    sentence = sentence.rstrip(" .:;,").strip()
    if len(sentence) > 100:
        cut = sentence[:100]
        for sep in ("; ", ": ", " -- ", ", "):
            i = cut.rfind(sep)
            if i >= 40:
                sentence = cut[:i]
                break
        else:
            sentence = cut.rsplit(" ", 1)[0]
        sentence = sentence.rstrip(" .:;,")
    return sentence or None


def is_null_message(node):
    node = skip_implicit(node)
    if node.get("kind") in ("GNUNullExpr", "CXXNullPtrLiteralExpr"):
        return True
    if node.get("kind") == "IntegerLiteral" and node.get("value") == "0":
        return True
    if node.get("kind") == "StringLiteral" and node.get("value") == '""':
        return True
    return False


class Location:
    """Where a node's text is in the main file, if it is. how says what the
    text is: file (plain), macro-arg (written in a macro's argument, so in
    place), macro-body (written in a #define of the file) or foreign (made
    by a macro defined elsewhere: only where it was used is known)."""

    def __init__(self, source, loc, main):
        self.offset = None
        self.length = 0
        self.how = None
        self.expansion = None  # (offset, length) where a macro was used
        if not loc:
            return
        if "spellingLoc" in loc:
            spelling, expansion = loc["spellingLoc"], loc["expansionLoc"]
            if expansion.get("_f") == main and "offset" in expansion:
                self.expansion = (expansion["offset"], expansion.get("tokLen", 0))
            if spelling.get("_f") == main and "offset" in spelling:
                self.offset, self.length = spelling["offset"], spelling.get("tokLen", 0)
                self.how = "macro-body" if source.directive_at(self.offset) else "macro-arg"
            elif self.expansion:
                self.offset, self.length = self.expansion
                self.how = "foreign"
            return
        if loc.get("_f") == main and "offset" in loc:
            self.offset, self.length = loc["offset"], loc.get("tokLen", 0)
            self.how = "file"

    def in_text(self):
        """Where the node starts in the text as written: a macro used in the
        file counts from where it was used."""
        if self.how == "macro-body" and self.expansion:
            return self.expansion[0]
        return self.offset


# --------------------------------------------------------------------------
# What the file holds
# --------------------------------------------------------------------------
class Test:
    def __init__(self):
        self.n = None
        self.data = None          # the fixture, as clang names it
        self.fixture = None       # as the converted file spells it
        self.group = None
        self.start = None         # template<> ...
        self.header_end = None    # after test<N>()
        self.body_start = None    # {
        self.body_end = None      # after }
        self.namespaces = ()
        self.names = []           # set_test_name calls: (call, literal text or None, top-level?)
        self.inactive = False
        self.macro = False
        self.node = None
        self.catch2_name = None
        self.tut_name = ""


class Group:
    def __init__(self, name, data, limit):
        self.name = name
        self.data = data
        self.limit = limit
        self.tests = []


class Plan:
    """A piece of the file replaced: [start, end) by what text() renders."""

    def __init__(self, start, end, render):
        self.start = start
        self.end = end
        self.render = render

    def text(self, conv):
        return self.render(conv, self)


class Converter:
    def __init__(self, source, items, tut_ids, active_lines, demangled):
        self.src = source
        self.main = os.path.normpath(source.path)
        self.items = items
        self.tut_ids = tut_ids
        self.demangled = demangled
        self.plans = []
        self.checks = []          # (offset, why)
        self.tests = []
        self.groups = {}          # data -> Group
        self.typedefs = {}        # name -> data type of a test_object / test_group typedef
        self.removed_typedef_ids = set()
        self.removals = []        # (start, end)
        self.calls = {}           # offset -> [(kind info)]
        self.handled_nodes = set()
        self.headers = {"<catch2/catch_test_macros.hpp>"}
        self.notes = []
        self.counted = {}         # offset of a TUT assertion call -> True
        self.derived_groups = {}  # class derived from test_group -> (data, limit, name)
        self.removed_spans = []
        self.slot_at = {}         # chunk start -> the plan that puts tests there
        self.emitted = {}         # offset of a converted call -> the Catch2 assertions it became
        self.records = {}         # qualified name of a class -> its CXXRecordDecl
        self.name_overrides = {}  # (group, n) -> a case's name, from --names
        self.friend_names = set() # tut:: types production code befriends
        self.tut_namespaces = []  # NamespaceDecl nodes named tut, with their parents
        self.body_reads = {}      # id of a test's body -> (offset, declaration) of what it reads
        self.body_bare = {}       # id of a test's body -> the variables it declares without an initializer
        self.body_writes = {}     # id of a test's body -> (offset, variable) of what it writes
        self.live = None          # the masked text with code under #if 0 blanked too
        source.find_conditionals(active_lines)

    # ---- helpers -------------------------------------------------------
    def check(self, offset, why):
        self.checks.append((offset, why))

    def text(self, start, end):
        return self.src.data[start:end].decode("utf-8", "surrogateescape")

    def loc(self, loc):
        return Location(self.src, loc, self.main)

    def node_range(self, node):
        """[start, end) of a node in the main file, or None."""
        rng = node.get("range")
        if not rng:
            return None
        begin, end = self.loc(rng.get("begin")), self.loc(rng.get("end"))
        if begin.offset is None or end.offset is None:
            return None
        return begin.offset, end.offset + end.length, begin.how, end.how

    # ---- the walk ------------------------------------------------------
    def analyse(self):
        for item in self.items:
            self.walk(item, [])

    def walk(self, node, parents):
        kind = node.get("kind")
        if kind == "CXXMethodDecl" and node.get("name") == "test" and "test_object" in node.get("mangledName", ""):
            self.on_test(node, parents)
        elif kind in ("TypedefDecl", "TypeAliasDecl"):
            self.on_typedef(node, parents)
        elif kind == "VarDecl":
            self.on_var(node, parents)
        elif kind == "CXXRecordDecl":
            if node.get("completeDefinition") and node.get("name"):
                spaces = [p.get("name") or "(anonymous namespace)" for p in parents if p.get("kind") == "NamespaceDecl"]
                self.records.setdefault("::".join(spaces + [node["name"]]), node)
            self.on_record(node, parents)
        elif kind == "NamespaceDecl" and node.get("name") == "tut":
            self.tut_namespaces.append((node, parents))
        elif kind in ("CallExpr", "CXXMemberCallExpr"):
            self.on_call(node, parents)
        elif kind == "CXXThrowExpr":
            self.on_throw(node, parents)
        elif kind == "CXXCatchStmt":
            self.on_catch(node, parents)
        elif kind in ("DeclRefExpr", "MemberExpr", "UnresolvedLookupExpr") and id(node) not in self.handled_nodes:
            self.on_reference(node, parents)
        for child in node.get("inner", []):
            if isinstance(child, dict):
                self.walk(child, parents + [node])

    def namespaces_of(self, parents):
        return tuple(p.get("name", "") for p in parents if p.get("kind") == "NamespaceDecl")

    # ---- tests ---------------------------------------------------------
    def on_test(self, node, parents):
        body = next((c for c in node.get("inner", []) if c.get("kind") == "CompoundStmt"), None)
        if body is None:
            return
        n = next((c.get("value") for c in node.get("inner", []) if c.get("kind") == "TemplateArgument"), None)
        test = Test()
        test.node = node
        test.n = int(n) if n is not None else None
        test.mangled = node.get("mangledName")
        rng = self.node_range(node)
        brng = self.node_range(body)
        if rng is None or brng is None or rng[2] != "file" or brng[2] != "file":
            test.macro = True
            where = self.loc(node.get("range", {}).get("begin")).offset or self.loc(node.get("loc")).offset or 0
            test.start = where
            self.check(where, f"test<{test.n}> is made by a macro: convert it by hand")
            self.tests.append(test)
            return
        test.start, test.body_start, test.body_end = rng[0], brng[0], brng[1]
        test.namespaces = self.namespaces_of(parents)
        m = re.compile(rb"test\s*<\s*\d+\s*>\s*\(\s*\)").search(self.src.masked, test.start, test.body_start)
        if not m:
            raise ConvertError(f"{self.src.path}: cannot find test<{test.n}>() in its definition")
        test.header_end = m.end()
        self.tests.append(test)

    def on_typedef(self, node, parents):
        t = node.get("type", {})
        full = t.get("desugaredQualType") or t.get("qualType") or ""
        m = re.match(r"^(?:tut::)?test_(object|group)<(.*)>$", full)
        if not m:
            return
        args = split_template_args(m.group(2))
        self.typedefs[node.get("name")] = args[0]
        self.removed_typedef_ids.add(node.get("id"))
        self.remove_decl(node)

    def on_var(self, node, parents):
        t = node.get("type", {})
        full = t.get("desugaredQualType") or t.get("qualType") or ""
        m = re.match(r"^(?:tut::)?test_group<(.*)>$", full)
        derived = self.derived_groups.get(full) or self.derived_groups.get(full.rpartition("::")[2])
        if not m and derived is None:
            return
        plain = True
        if m:
            args = split_template_args(m.group(1))
            data, limit = args[0], int(args[1]) if len(args) > 1 else 50
            literal = find_kind(node, "StringLiteral")
            name = literal.get("value") if literal else None
        else:
            data, limit, name, plain = derived
        where = self.node_range(node)[0] if self.node_range(node) else 0
        if not name or not name.startswith('"'):
            self.check(where, "a test_group whose name is not a string literal")
            return
        name = c_unescape(name[1:-1])
        if data in self.groups:
            self.check(where, f"two groups run the tests of {data}")
        self.groups[data] = Group(name, data, limit)
        if plain:
            self.remove_decl(node)

    def on_record(self, node, parents):
        """A class derived from test_group: one that only names the group in
        its constructor, as llvocache's does, goes with the group; one that
        holds more (a constructor's body, a destructor, members), which TUT
        ran around the group's tests, stays, and its object with it, marked
        for converting by hand."""
        for base in node.get("bases", []):
            t = base.get("type", {})
            full = t.get("desugaredQualType") or t.get("qualType") or ""
            m = re.match(r"^(?:tut::)?test_group<(.*)>$", full)
            if not m:
                continue
            args = split_template_args(m.group(1))
            data = args[0] if "::" in args[0] else "tut::" + args[0]
            literal = find_kind(node, "StringLiteral")
            name = literal.get("value") if literal else None
            plain = names_only(node)
            qualified = "::".join([n for n in self.namespaces_of(parents) if n] + [node.get("name", "")])
            for key in (qualified, node.get("name", "")):
                self.derived_groups[key] = (data, int(args[1]) if len(args) > 1 else 50, name, plain)
            if plain:
                self.remove_decl(node)
            else:
                rng = self.node_range(node)
                self.check(rng[0] if rng else 0,
                           f"{node.get('name')} derives from test_group and holds more than the group's name, "
                           f"which TUT ran around the group's tests: convert it by hand")

    def remove_decl(self, node):
        rng = self.node_range(node)
        if rng is None:
            return
        start, end = rng[0], rng[1]
        after = self.src.skip_space(end)
        if self.src.masked[after:after + 1] == b";":
            end = after + 1
        self.removals.append((start, end, "decl"))
        self.handled_nodes.add(id(node))
        for child in iter_nodes(node):
            self.handled_nodes.add(id(child))

    # ---- calls ---------------------------------------------------------
    def callee_of(self, node):
        inner = [c for c in node.get("inner", []) if isinstance(c, dict) and "kind" in c]
        if not inner:
            return None, None, []
        callee = inner[0]
        stripped = callee
        while stripped.get("kind") in ("ImplicitCastExpr", "ParenExpr") and stripped.get("inner"):
            stripped = stripped["inner"][0]
        args = [a for a in inner[1:] if a.get("kind") != "CXXDefaultArgExpr"]
        if stripped.get("kind") == "DeclRefExpr":
            ref = stripped.get("referencedDecl", {})
            if ref.get("id") in self.tut_ids and ref.get("name") in TUT_FUNCTIONS:
                return ref.get("name"), stripped, args
        elif stripped.get("kind") == "MemberExpr":
            if stripped.get("referencedMemberDecl") in self.tut_ids and stripped.get("name") in TUT_MEMBERS:
                return stripped.get("name"), stripped, args
        elif stripped.get("kind") == "UnresolvedLookupExpr":
            lookups = stripped.get("lookups", [])
            if any(l.get("id") in self.tut_ids for l in lookups) and stripped.get("name") in TUT_FUNCTIONS:
                return stripped.get("name"), stripped, args
        elif stripped.get("kind") in ("CXXDependentScopeMemberExpr", "UnresolvedMemberExpr"):
            if stripped.get("member", stripped.get("name")) in TUT_MEMBERS:
                return stripped.get("member", stripped.get("name")), stripped, args
        return None, None, []

    def on_call(self, node, parents):
        name, callee, args = self.callee_of(node)
        if name is None:
            return
        self.handled_nodes.add(id(callee))
        for sub in iter_nodes(callee):
            self.handled_nodes.add(id(sub))
        rng = self.node_range(node)
        begin = self.loc(node.get("range", {}).get("begin"))
        if rng is None or begin.how is None:
            where = begin.offset if begin.offset is not None else 0
            self.check(where, f"{name}() comes from a macro defined elsewhere")
            return
        record = {"node": node, "name": name, "args": args, "parents": parents, "range": rng,
                  "how": begin.how, "callee": callee}
        self.calls.setdefault(rng[0], []).append(record)

    def on_throw(self, node, parents):
        inner = [c for c in node.get("inner", []) if "kind" in c]
        if not inner:
            return
        value = skip_implicit(inner[0])
        if value_type(value) not in ("tut::failure", "failure"):
            return
        rng = self.node_range(node)
        if rng is None:
            return
        for sub in iter_nodes(node):
            self.handled_nodes.add(id(sub))
        if value.get("kind") not in ("CXXConstructExpr", "CXXFunctionalCastExpr", "CXXTemporaryObjectExpr"):
            self.calls.setdefault(rng[0], []).append({"node": node, "name": "throw-held", "args": [],
                                                      "parents": parents, "range": rng, "how": "file",
                                                      "callee": None})
            return
        # The message: the first node under the construction that starts
        # after its opening parenthesis.
        open_paren = self.src.masked.find(b"(", rng[0], rng[1])
        message = None
        for sub in iter_nodes(value):
            loc = self.loc(sub.get("range", {}).get("begin"))
            if sub is not value and loc.offset is not None and loc.offset > open_paren:
                message = sub
                break
        self.calls.setdefault(rng[0], []).append({"node": node, "name": "throw",
                                                  "args": [message] if message else [],
                                                  "parents": parents, "range": rng, "how": "file",
                                                  "callee": None})

    def on_catch(self, node, parents):
        inner = [c for c in node.get("inner", []) if "kind" in c]
        if not inner or inner[0].get("kind") != "VarDecl":
            return
        decl = inner[0]
        caught = re.sub(r"^tut::", "", value_type(decl))
        if caught not in ("failure", "tut_error", "skip_failure"):
            return
        rng = self.node_range(decl)
        if rng is None:
            return
        body = inner[1] if len(inner) > 1 else {}
        rethrows = any(n.get("kind") == "CXXThrowExpr" and not [c for c in n.get("inner", []) if "kind" in c]
                       for n in iter_nodes(body))
        for sub in iter_nodes(decl):
            self.handled_nodes.add(id(sub))
        if caught != "failure":
            self.check(rng[0], f"catches tut::{caught}, which Catch2 has no counterpart of")
            return
        if decl.get("name") and (decl.get("isReferenced") or decl.get("isUsed")):
            self.check(rng[0], "uses the tut::failure it catches; Catch2's failure carries no message")
            return
        if not rethrows:
            self.check(rng[0], "catches a tut::failure without rethrowing it; Catch2 has recorded the failure already")
            return
        m = re.compile(rb"(?:\btut\s*::\s*)?\bfailure\b").search(self.src.masked, rng[0], rng[1])
        if not m:
            self.check(rng[0], "cannot find tut::failure in the catch")
            return
        self.headers.add('"altestfailure.h"')
        self.plans.append(Plan(m.start(), m.end(), lambda conv, plan: "ll_test::Failure"))

    def on_reference(self, node, parents):
        kind = node.get("kind")
        ref = None
        if kind == "DeclRefExpr":
            ref = node.get("referencedDecl", {}).get("id")
        elif kind == "MemberExpr":
            ref = node.get("referencedMemberDecl")
        elif kind == "UnresolvedLookupExpr":
            ids = [l.get("id") for l in node.get("lookups", [])]
            ref = next((i for i in ids if i in self.tut_ids), None)
        if ref in self.tut_ids:
            rng = self.node_range(node)
            if rng:
                self.check(rng[0], f"{self.tut_ids[ref]} is used other than by a call")

    # ---- deciding each call ------------------------------------------
    def resolve_calls(self):
        for offset, records in sorted(self.calls.items()):
            first = records[0]
            name = first["name"]
            if name in TUT_ASSERTING or name == "throw":
                self.counted[offset] = True
            how = first["how"]
            if how == "macro-body":
                self.convert_in_macro(offset, records)
                continue
            if how == "foreign":
                self.check(offset, f"{name}() comes from a macro defined elsewhere")
                continue
            kinds = {self.conversion_kind(r) for r in records}
            if len(kinds) > 1 and "dependent" in kinds:
                # a template's own definition beside its instantiations
                kinds.discard("dependent")
            if len(kinds) > 1:
                self.check(offset, f"{name}() means different things in different instantiations: "
                                   + ", ".join(sorted(kinds)))
                continue
            kind = kinds.pop()
            # an instantiation's record, where the types are known
            first = next((r for r in records if self.conversion_kind(r) == kind), first)
            self.convert_call(first, kind)

    def conversion_kind(self, record):
        """How a call converts, from the types clang gave its arguments."""
        name, args = record["name"], record["args"]
        callee = record["callee"] or {}
        if callee.get("kind") in ("UnresolvedLookupExpr", "CXXDependentScopeMemberExpr", "UnresolvedMemberExpr") \
                and name not in TYPE_FREE and name not in TUT_MEMBERS:
            return "dependent"
        if name == "ensure_equals" and len(args) in (2, 3):
            types = [value_type(a) for a in args[-2:]]
            if types[0] == types[1] == "LLSD":
                return "equals-llsd"
            if types[0] == types[1] == "LLURI":
                return "equals-uri"
            return "equals"
        if name == "ensure_approximately_equals":
            return "bits-" + (self.parameter_float(record) or "?")
        if name == "ensure_distance":
            return "distance-" + (self.distance_type(record) or "?")
        return name

    def distance_type(self, record):
        """The type TUT's ensure_distance<T> worked in, as clang resolved it:
        that of its last parameter, const T&."""
        callee = record["callee"] or {}
        qual = callee.get("referencedDecl", {}).get("type", {}).get("qualType", "")
        m = re.match(r"^void \((.*)\)$", qual)
        if not m:
            return None
        last = split_template_args(m.group(1))[-1]
        t = re.sub(r"\s*&$", "", last.strip())
        t = re.sub(r"^const\s+|\s+const$", "", t).replace("(anonymous namespace)::", "").strip()
        # A comma would split the macro's arguments.
        return t if t and "," not in t else None

    def parameter_float(self, record):
        callee = record["callee"] or {}
        qual = callee.get("referencedDecl", {}).get("type", {}).get("qualType", "")
        m = re.match(r"^void \((.*)\)$", qual)
        if not m:
            return None
        params = [p.strip() for p in split_template_args(m.group(1))]
        expected = params[-2] if len(params) >= 3 else ""
        return {"F32": "F32", "float": "F32", "F64": "F64", "double": "F64"}.get(expected)

    def context(self, record):
        """block (a statement among others), body (the lone statement of an
        if, else or loop) or expr, with the parent that decided it."""
        child = record["node"]
        for parent in reversed(record["parents"]):
            kind = parent.get("kind")
            if kind in IMPLICIT:
                child = parent
                continue
            if kind in STATEMENT_PARENTS:
                return "block", parent
            if kind in BODY_PARENTS:
                inner = [c for c in parent.get("inner", []) if "kind" in c]
                if kind == "IfStmt" and inner and child is not inner[0]:
                    return "body", parent
                if kind == "DoStmt" and inner and child is inner[0]:
                    return "body", parent
                if kind not in ("IfStmt", "DoStmt") and inner and child is inner[-1]:
                    return "body", parent
            return "expr", parent
        return "expr", None

    def in_destructor(self, record):
        for parent in reversed(record["parents"]):
            if parent.get("kind") == "LambdaExpr":
                return False
            if parent.get("kind") in FUNCTION_KINDS:
                return parent.get("kind") == "CXXDestructorDecl"
        return False

    def strength(self, record, nodes):
        """CHECK, which lets the case go on past a failure and so reports every
        failure of a run, or REQUIRE, which ends it, where going on could
        only crash or bury the first failure under its consequences:
          - in a destructor, CHECK, since a throw would end the process;
          - outside a test's own body (a helper, which any caller may call
            in a loop) and in a loop or lambda of one, REQUIRE, so that the
            first element that fails is the one reported;
          - where the condition guards what follows (see guards()), or
            calls what makes it (see prepares()), REQUIRE;
          - otherwise CHECK, a plain compare of values among it."""
        if self.in_destructor(record):
            return "CHECK"
        test = self.test_containing(record["range"][0])
        body = test_body(test) if test else None
        if body is None:
            return "REQUIRE"
        for parent in reversed(record["parents"]):
            if parent is body:
                break
            if parent.get("kind") in LOOP_KINDS or parent.get("kind") == "LambdaExpr":
                return "REQUIRE"
        if any(self.guards(n) or self.prepares(n, record) for n in nodes if n):
            return "REQUIRE"
        return "CHECK"

    @staticmethod
    def guards(node):
        """Whether a condition or value tells what the code after it relies
        on: a pointer, compared with null or tested as a bool; an object
        tested through its operator bool (an optional, a smart pointer, a
        stream; not an atomic, whose bool is its value) or compared with
        nullptr through an operator of its own; a position
        compared with its container's end() or with npos; or a member that
        says how much an object holds or whether it holds anything
        (has_value() and found() among them), answering with a bool or an
        integer: a float length() is a magnitude, not a size. Default arguments are not
        looked in: a NULL a callee defaults to is nothing the test
        compared."""
        if value_type(unparenthesised(node)).endswith("*"):
            return True
        stack = [node]
        while stack:
            sub = stack.pop()
            kind = sub.get("kind")
            if kind == "CXXDefaultArgExpr":
                continue
            if kind == "ImplicitCastExpr" and sub.get("castKind") == "PointerToBoolean":
                return True
            if kind == "MemberExpr" and sub.get("name") == "operator bool":
                # An atomic's bool is its value, not whether it holds one.
                base = [c for c in sub.get("inner", []) if "kind" in c]
                if not (base and re.search(r"atomic", base[0].get("type", {}).get("qualType", ""), re.I)):
                    return True
            if any(is_end(c) for c in compared(sub)):
                return True
            # A member, called or read, answering how much or whether: with
            # a bool or an integer, as a size or a count does; a float
            # length() is a magnitude.
            member = None
            if kind == "CXXMemberCallExpr":
                member = next((c for c in sub.get("inner", []) if "kind" in c), {})
            elif kind == "MemberExpr" and sub.get("type", {}).get("qualType") != "<bound member function type>":
                member = sub
            if member and member.get("kind") == "MemberExpr" and member.get("name") in GUARD_MEMBERS and \
                    INTEGRAL.fullmatch(value_type(sub)):
                # isNull() and notNull() guard only on what points: on a
                # vector or a UUID they are values like any other.
                if member.get("name") not in ("isNull", "notNull"):
                    return True
                base = [c for c in member.get("inner", []) if "kind" in c]
                if base and re.search(r"Pointer|ptr\b|Handle|\*", value_type(skip_implicit(base[0]))):
                    return True
            stack.extend(c for c in sub.get("inner", []) if isinstance(c, dict) and "kind" in c)
        return False

    def prepares(self, node, record):
        """Whether a condition calls what makes state the test reads after
        it, state that is indeterminate where the call failed: a call
        returning bool, to a non-const member function of an object, or
        with an argument it may fill (one passed by non-const reference, or
        by address), where that object or argument is a local of the test
        declared without an initializer and written by nothing since, and
        its body reads it again after the assertion, as with LLMatrix4a
        inverse; ensure(inverse.setInverse(m)), or S32 value;
        ensure(convertToS32(s, value)). A local initialised or written
        before (LLMatrix4a m; m.setIdentity()), a member or a parameter
        holds a value whatever the call did."""
        test = self.test_containing(record["range"][0])
        body = test_body(test) if test else None
        if body is None:
            return False
        bare = self.bare_locals(body)
        made = set()
        stack = [node]
        while stack:
            sub = stack.pop()
            kind = sub.get("kind")
            if kind in ("CXXDefaultArgExpr", "LambdaExpr"):
                continue
            inner = [c for c in sub.get("inner", []) if isinstance(c, dict) and "kind" in c]
            if kind in ("CallExpr", "CXXMemberCallExpr") and inner and value_type(sub) == "bool":
                callee = inner[0]
                if kind == "CXXMemberCallExpr" and callee.get("kind") == "MemberExpr" and \
                        callee.get("name") != "operator bool":
                    base = [c for c in callee.get("inner", []) if "kind" in c]
                    if base and not const_object(base[0], callee.get("isArrow")):
                        made.add(written_root(base[0]))
                for arg in inner[1:]:
                    if fills(arg):
                        made.add(written_root(arg))
            stack.extend(inner)
        # Bare, and not written between its declaration and this call.
        at = record["range"][0]
        made = {d for d in made if d in bare and
                not any(bare[d] < where < at and decl == d for where, decl in self.writes(body))}
        if not made:
            return False
        after = record["range"][1]
        return any(at >= after and decl in made for at, decl in self.reads(body))

    def bare_locals(self, body):
        """The variables a test's body declares without an initializer, each
        with where its declaration ends, once per body."""
        cache = self.body_bare
        if id(body) not in cache:
            found = {}
            for n in iter_nodes(body):
                if n.get("kind") == "VarDecl" and declared_bare(n):
                    rng = self.node_range(n)
                    if rng:
                        found[n.get("id")] = rng[1]
            cache[id(body)] = found
        return cache[id(body)]

    def writes(self, body):
        """(offset, variable) of every write a test's body makes to a
        variable of its own, as written_root() finds it: an assignment, an
        increment, a non-const member call on it, a call it is filled by.
        Once per body."""
        cache = self.body_writes
        if id(body) not in cache:
            found = []
            for n in iter_nodes(body):
                kind = n.get("kind")
                inner = [c for c in n.get("inner", []) if isinstance(c, dict) and "kind" in c]
                targets = []
                if kind in ("BinaryOperator", "CompoundAssignOperator") and n.get("opcode", "").endswith("=") \
                        and n.get("opcode") not in ("==", "!=", "<=", ">=") and inner:
                    targets.append(inner[0])
                elif kind == "UnaryOperator" and n.get("opcode") in ("++", "--") and inner:
                    targets.append(inner[0])
                elif kind == "CXXOperatorCallExpr" and len(inner) >= 2:
                    name = skip_implicit(inner[0]).get("referencedDecl", {}).get("name", "")
                    if re.fullmatch(r"operator(?:[-+*/%^&|]|<<|>>)?=|operator(?:\+\+|--)", name):
                        targets.append(inner[1])
                elif kind == "CXXMemberCallExpr" and inner and inner[0].get("kind") == "MemberExpr":
                    base = [c for c in inner[0].get("inner", []) if "kind" in c]
                    if base and not const_object(base[0], inner[0].get("isArrow")):
                        targets.append(base[0])
                if kind in ("CallExpr", "CXXMemberCallExpr", "CXXConstructExpr"):
                    targets += [a for a in (inner if kind == "CXXConstructExpr" else inner[1:]) if fills(a)]
                for target in targets:
                    decl = written_root(target)
                    where = self.loc(n.get("range", {}).get("begin")).in_text()
                    if decl and where is not None:
                        found.append((where, decl))
            cache[id(body)] = found
        return cache[id(body)]

    def reads(self, body):
        """(offset, declaration) of every variable and field a test's body
        names, once per body."""
        cache = self.body_reads
        if id(body) not in cache:
            found = []
            for sub in iter_nodes(body):
                decl = variable_of(sub)
                if decl:
                    at = self.loc(sub.get("range", {}).get("begin")).in_text()
                    if at is not None:
                        found.append((at, decl))
            cache[id(body)] = found
        return cache[id(body)]

    def plain_span(self, node):
        """[start, end) of a node written in this file's text, or None."""
        rng = self.node_range(node)
        if rng is None or rng[2] not in ("file", "macro-arg") or rng[3] not in ("file", "macro-arg"):
            return None
        return rng[0], rng[1]

    @staticmethod
    def expansion(loc):
        """Where the macro whose expansion holds loc was used, or None for a
        place in the file's own text."""
        if loc and "expansionLoc" in loc:
            return loc["expansionLoc"].get("_f"), loc["expansionLoc"].get("offset")
        return None

    def call_span(self, node, record):
        """[start, end) of a node written in its call's own text, or None:
        where the call is in the file, no end of the node is a macro's;
        where the call is in a macro's argument, the node is in it too. A
        node one end of which a macro made, as the ( of WRAP(a && b) or
        the 2 of ID(2), has no text of its own to rewrite."""
        span = self.plain_span(node)
        rng = node.get("range") or {}
        if span is None:
            return None
        own = self.expansion(record["node"].get("range", {}).get("begin"))
        if self.expansion(rng.get("begin")) != own or self.expansion(rng.get("end")) != own:
            return None
        return span

    def written_parens(self, node, record):
        """Whether a ParenExpr's parentheses are the call's own text, not a
        macro's."""
        span = self.call_span(node, record)
        return span is not None and self.src.masked[span[0]:span[0] + 1] == b"(" and \
            self.src.masked[span[1] - 1:span[1]] == b")"

    def bare(self, node, record):
        """node, without the parentheses the test wrote around a whole Catch2
        can decompose (a macro's stay), and without the implicit nodes
        around that."""
        node = skip_implicit(node)
        while node.get("kind") == "ParenExpr" and self.written_parens(node, record):
            inner = [c for c in node.get("inner", []) if "kind" in c]
            if not inner or not decomposable(inner[0]):
                break
            node = skip_implicit(inner[0])
        return node

    def and_terms(self, node, record):
        """The terms of a && chain, each as the test wrote it, or [node].
        Only a && and parentheses written in the call's own text are split
        or seen through: one a macro made (WRAP(a && b), or the && of a
        macro's body) would take the macro with it."""
        node = skip_implicit(node)
        while node.get("kind") == "ParenExpr" and self.written_parens(node, record):
            inner = [c for c in node.get("inner", []) if "kind" in c]
            if not inner:
                break
            inside = skip_implicit(inner[0])
            if inside.get("kind") != "BinaryOperator" or inside.get("opcode") != "&&":
                break
            node = inside
        if node.get("kind") == "BinaryOperator" and node.get("opcode") == "&&":
            inner = [c for c in node.get("inner", []) if "kind" in c]
            spans = [self.call_span(c, record) for c in inner]
            if len(inner) == 2 and None not in spans and \
                    self.src.masked[spans[0][1]:spans[1][0]].strip() in (b"&&", b"and"):
                return self.and_terms(inner[0], record) + self.and_terms(inner[1], record)
        return [node]

    def assertion(self, node, span, negate, record):
        """(suffix, text) of the one assertion a condition is: "" or "_FALSE"
        after CHECK or REQUIRE, and what goes between its parentheses.
        Parentheses the test wrote around a whole Catch2 decomposes go, so
        that a failure prints its operands; a literal compared on the left
        moves right; a bool compared with true or false is asserted as
        itself; and strcmp() is compared as std::string_view, which prints."""
        suffix = "_FALSE" if negate else ""
        whole = self.render(*span)
        node = self.bare(node, record)
        nspan = self.call_span(node, record)
        if nspan is None:
            return suffix, whole if decomposable(node) else "(" + whole + ")"
        pair = operands(node)
        op = node.get("opcode") if pair else None
        if op in ("==", "!="):
            left, right = (skip_implicit(p) for p in pair)
            spans = [self.call_span(p, record) for p in pair]
            if None not in spans:
                for lit, other, other_span in ((left, right, spans[1]), (right, left, spans[0])):
                    if lit.get("kind") == "CXXBoolLiteralExpr" and value_type(other) == "bool":
                        truth = (lit.get("value") is True) == (op == "==")
                        other_node = self.bare(other, record)
                        inner = self.render(*(self.call_span(other_node, record) or other_span))
                        if not decomposable(other_node) and not wrapped(inner):
                            inner = "(" + inner + ")"
                        return ("" if truth != negate else "_FALSE"), inner
                for zero, call in ((left, right), (right, left)):
                    args = strcmp_args(call)
                    if args and zero.get("kind") == "IntegerLiteral" and zero.get("value") == "0":
                        text = self.string_views(args, op, record)
                        if text:
                            return suffix, text
                if is_literal(left) and not is_literal(right):
                    return suffix, f"{self.render(*spans[1])} {op} {self.render(*spans[0])}"
        if node.get("kind") == "UnaryOperator" and node.get("opcode") == "!":
            inner = [c for c in node.get("inner", []) if "kind" in c]
            args = strcmp_args(inner[0]) if inner else None
            text = self.string_views(args, "==", record) if args else None
            if text:
                return suffix, text
            # !x is x, asserted false, so that x prints.
            operand = self.bare(inner[0], record) if inner else None
            ospan = self.call_span(operand, record) if operand else None
            if ospan:
                text = self.render(*ospan)
                return ("" if negate else "_FALSE"), text if decomposable(operand) else "(" + text + ")"
        args = strcmp_args(node)
        text = self.string_views(args, "!=", record) if args else None
        if text:
            return suffix, text
        text = self.render(*nspan)
        return suffix, text if decomposable(node) else "(" + text + ")"

    def string_views(self, args, op, record):
        spans = [self.call_span(a, record) for a in args]
        if None in spans:
            return None
        texts = []
        for arg, span in zip(args, spans):
            text = self.render(*span)
            texts.append(text if skip_implicit(arg).get("kind") == "StringLiteral" else f"std::string_view({text})")
        # Two literals compared as they are would be two arrays' addresses.
        if not any(t.startswith("std::string_view(") for t in texts):
            texts[0] = f"std::string_view({texts[0]})"
        if not texts[0].startswith("std::string_view("):
            texts.reverse()
        self.headers.add("<string_view>")
        return f"{texts[0]} {op} {texts[1]}"

    def arg_ranges(self, record):
        """[start, end) of each argument."""
        start, end = record["range"][0], record["range"][1]
        if self.src.data[end - 1:end] != b")":
            return None
        if record["name"] == "throw":
            open_paren = self.src.masked.find(b"(", start, end)
            return [(self.src.skip_space(open_paren + 1), self.src.skip_space_back(end - 1))]
        begins = []
        own = self.expansion(record["node"].get("range", {}).get("begin"))
        for arg in record["args"]:
            loc = arg.get("range", {}).get("begin")
            where = self.loc(loc)
            begin = where.in_text()
            # An argument a macro begins, as ID(x) does with x, begins where
            # the macro was used, not at the x inside it.
            if where.how == "macro-arg" and where.expansion and self.expansion(loc) != own:
                begin = where.expansion[0]
            if begin is None or not (start < begin < end):
                return None
            begins.append(begin)
        if begins != sorted(begins):
            return None
        ranges = []
        for i, b in enumerate(begins):
            if i + 1 < len(begins):
                stop = self.src.skip_space_back(begins[i + 1])
                if self.src.masked[stop - 1:stop] != b",":
                    return None
                stop = self.src.skip_space_back(stop - 1)
            else:
                stop = self.src.skip_space_back(end - 1)
            if stop <= b:
                return None
            ranges.append((b, stop))
        return ranges

    def separator(self, ranges, i):
        """What stood between argument i and i + 1, without its comma: the line
        break and comments, or a space."""
        text = self.text(ranges[i][1], ranges[i + 1][0])
        comma = self.src.masked.find(b",", ranges[i][1], ranges[i + 1][0]) - ranges[i][1]
        text = text[:comma] + text[comma + 1:]
        if text.strip() or "\n" in text:
            return text if text[:1].isspace() else " " + text
        return " "

    def operand(self, ranges, i, node, wrap_at, guard=False):
        """Argument i's text, parenthesised if its operator binds no tighter
        than wrap_at, or, with guard, if a macro taking it would split it."""
        text = self.render(ranges[i][0], ranges[i][1])
        if precedence(node) <= wrap_at or (guard and needs_macro_guard(self.src.masked[ranges[i][0]:ranges[i][1]])):
            return "(" + text + ")"
        return text

    def condition(self, span, node):
        """A condition's text for REQUIRE, parenthesised where Catch2 cannot
        decompose it (&&, ||, ?:, an assignment, a comparison of a
        comparison). Parentheses the test wrote stay: what they hold is
        evaluated as TUT evaluated it, and its operands are not printed, so a
        type with no operator<< linked still links."""
        text = self.render(*span)
        return text if decomposable(node) else "(" + text + ")"

    def convert_call(self, record, kind):
        name = record["name"]
        start, end = record["range"][0], record["range"][1]
        args = record["args"]
        if kind == "dependent":
            self.check(start, f"{name}() in a template, and no instantiation shows its types")
            return
        if name == "get_test_name":
            self.headers.add('"altestname.h"')
            self.plans.append(Plan(start, end, lambda conv, plan: "ll_test::currentTestName()"))
            return
        if name == "set_test_name":
            return  # folded into the case's name by name_tests
        if name == "ensure_errno":
            self.check(start, "ensure_errno has no Catch2 counterpart")
            return
        if name == "throw-held":
            self.check(start, "throws a tut::failure it holds, which has no Catch2 counterpart")
            return
        context, _ = self.context(record)
        if context == "expr":
            self.check(start, f"{name}() is part of an expression, where a Catch2 assertion, a statement, cannot go")
            return
        ranges = self.arg_ranges(record) if args or name == "throw" else []
        if ranges is None or (name == "throw" and not args):
            self.check(start, f"cannot place the arguments of {name}(); a macro around them?")
            return
        check_form = self.in_destructor(record)
        if check_form and name in ("fail", "skip", "throw"):
            self.check(start, f"{name}() in a destructor would throw out of it")
            return
        if kind == "bits-?":
            self.check(start, "cannot tell which ensure_approximately_equals overload was called")
            return
        if kind == "distance-?":
            self.check(start, "cannot tell, or cannot write in a macro's argument, the type ensure_distance worked in")
            return
        build = self.builder(name, kind, record, ranges, self.strength(record, value_args(name, args)))
        if build is not None:
            self.headers.update(headers_for(name, kind))
            if any(strcmp_args(sub) for a in args for sub in iter_nodes(a)):
                self.headers.add("<string_view>")
        if build is None:
            self.check(start, f"{name}() with {len(args)} arguments is not a form this converter knows")
            return
        block = getattr(build, "block", False)
        stmt_end = end
        if context == "body" or block:
            after = self.src.skip_space(end)
            if self.src.masked[after:after + 1] != b";":
                self.check(start, f"{name}() as a statement is not followed by its semicolon")
                return
            stmt_end = after + 1

        def render(conv, plan, build=build, context=context, start=start, block=block):
            statements = build()
            flat = [s for st in statements for s in (st[1] if isinstance(st, tuple) else [st])]
            conv.emitted[start] = sum(1 for s in flat if CATCH_ASSERTION.match(s.encode()))
            if context == "body":
                if len(flat) > 1:
                    return "{ " + "; ".join(flat) + "; }"
                return flat[0] + ";"
            indent = conv.src.indent_at(start).decode()
            if block:
                unit = "\t" if indent.startswith("\t") else "    "
                return "{\n" + "".join(f"{indent}{unit}{s};\n" for s in flat) + indent + "}"
            if conv.src.starts_line(start):
                return (";\n" + indent).join(flat)
            return "; ".join(flat)
        self.plans.append(Plan(start, stmt_end, render))

    def builder(self, name, kind, record, ranges, req):
        """A function giving the statements a call becomes, or None."""
        args = record["args"]
        n = len(args)

        def message(i):
            if is_null_message(args[i]):
                return []
            text = self.render(*ranges[i])
            if precedence(args[i]) <= 11 or needs_macro_guard(self.src.masked[ranges[i][0]:ranges[i][1]]):
                text = "(" + text + ")"
            return [f"UNSCOPED_INFO({text})"]

        def streamed(i):
            text = self.render(*ranges[i])
            return "(" + text + ")" if precedence(args[i]) <= 11 else text

        if name in ("ensure", "ensure_not"):
            if n not in (1, 2):
                return None
            condition = args[n - 1]
            # A && chain is as many assertions, each printing its operands,
            # each as strong as its own term needs.
            terms = self.and_terms(condition, record) if name == "ensure" else [condition]
            # A later term that changes something ran only where the ones
            # before it held: split, it would run regardless.
            if len(terms) > 1 and (any(self.call_span(t, record) is None for t in terms) or
                                   any(side_effect(t) for t in terms[1:])):
                terms = [condition]

            # TUT's ensure stopped the test at the first term that failed;
            # a term ends the case where a later one leans on what it read
            # for its own safety (dereferences it, indexes by it), so that
            # going on past its failure could crash or read garbage.
            chained = [False] * len(terms)
            if len(terms) > 1 and not self.in_destructor(record):
                later = set()
                for i in range(len(terms) - 1, -1, -1):
                    chained[i] = bool(variables(terms[i], constants=False) & later)
                    later |= leaned_on(terms[i])

            def build():
                out = []
                for i, term in enumerate(terms):
                    span = ranges[n - 1] if len(terms) == 1 else self.call_span(term, record)
                    suffix, text = self.assertion(term, span, name == "ensure_not", record)
                    if len(terms) == 1:
                        strength = req
                    else:
                        strength = "REQUIRE" if chained[i] else self.strength(record, [term])
                    out.append(f"{strength}{suffix}({text})")
                msg = message(0) if n == 2 else []
                if msg and len(out) > 1:
                    # The message is about all of them: INFO, scoped to them.
                    return [("block", ["INFO" + msg[0][len("UNSCOPED_INFO"):]] + out)]
                return msg + out
            build.block = n == 2 and not is_null_message(args[0]) and len(terms) > 1
            return build
        if name in ("fail", "skip", "throw"):
            if n != 1:
                return None
            macro = "SKIP" if name == "skip" else "FAIL"
            return lambda: [f"{macro}({streamed(0)})"]
        if name in ("ensure_equals", "ensure_not_equals"):
            if n not in (2, 3):
                return None
            a, e = n - 2, n - 1

            def build():
                msg = message(0) if n == 3 else []
                sep = self.separator(ranges, a)
                if kind == "equals-llsd":
                    actual = self.operand(ranges, a, args[a], 1, guard=True)
                    expected = self.operand(ranges, e, args[e], 1)
                    return msg + [f"{req}_THAT({actual},{sep}ll_test::LLSDEquals({expected}))"]
                if kind == "equals-uri":
                    actual = self.operand(ranges, a, args[a], 15)
                    expected = self.operand(ranges, e, args[e], 15)
                    return msg + [f"{req}({actual}.asString() =={sep}{expected}.asString())"]
                op = "==" if name == "ensure_equals" else "!="
                actual = self.operand(ranges, a, args[a], 9)
                expected = self.operand(ranges, e, args[e], 9)
                return msg + [f"{req}({actual} {op}{sep}{expected})"]
            return build
        if name in ("ensure_distance", "ensure_approximately_equals_range"):
            if n not in ((4,) if name == "ensure_approximately_equals_range" else (3, 4)):
                return None
            a = n - 3

            def build():
                msg = message(0) if n == 4 else []
                actual = self.operand(ranges, a, args[a], 1, guard=True)
                expected = self.operand(ranges, a + 1, args[a + 1], 1)
                margin = self.operand(ranges, a + 2, args[a + 2], 1)
                if name == "ensure_distance":
                    # TUT's: strictly inside, worked in its own type.
                    matcher = f"ll_test::WithinDistance<{kind.split('-', 1)[1]}>("
                else:
                    # TUT's parameters were F32: all three were narrowed.
                    narrow = [value_type(skip_implicit_casts(args[a + i])) not in ("float", "F32") for i in range(3)]
                    actual, expected, margin = (f"F32({t})" if cast else t
                                                for t, cast in zip((actual, expected, margin), narrow))
                    matcher = "Catch::Matchers::WithinAbs("
                return msg + [f"{req}_THAT({actual},{self.separator(ranges, a)}{matcher}"
                              f"{expected},{self.separator(ranges, a + 1)}{margin}))"]
            return build
        if name == "ensure_approximately_equals":
            if n not in (3, 4):
                return None
            a = n - 3
            ftype = kind.split("-", 1)[1]

            def build():
                msg = message(0) if n == 4 else []
                actual = self.operand(ranges, a, args[a], 1, guard=True)
                expected = self.operand(ranges, a + 1, args[a + 1], 1)
                given = value_type(skip_implicit_casts(args[a + 1]))
                if {"float": "F32", "double": "F64"}.get(given) != ftype:
                    expected = f"{ftype}({expected})"
                bits = self.operand(ranges, a + 2, args[a + 2], 1)
                return msg + [f"{req}_THAT({actual},{self.separator(ranges, a)}ll_test::WithinBits("
                              f"{expected},{self.separator(ranges, a + 1)}{bits}))"]
            return build
        if name in ("ensure_contains", "ensure_does_not_contain", "ensure_starts_with", "ensure_ends_with"):
            if n != 3:
                return None
            matcher = {"ensure_contains": "Catch::Matchers::ContainsSubstring",
                       "ensure_does_not_contain": "!Catch::Matchers::ContainsSubstring",
                       "ensure_starts_with": "Catch::Matchers::StartsWith",
                       "ensure_ends_with": "Catch::Matchers::EndsWith"}[name]

            def build():
                actual = self.operand(ranges, 1, args[1], 1, guard=True)
                part = self.operand(ranges, 2, args[2], 1)
                return message(0) + [f"{req}_THAT({actual},{self.separator(ranges, 1)}{matcher}({part}))"]
            return build
        if name == "ensure_memory_matches":
            if n not in (4, 5):
                return None
            a = n - 4

            def build():
                msg = message(0) if n == 5 else []
                parts = [self.operand(ranges, a + i, args[a + i], 1) for i in range(4)]
                return msg + [f"{req}_THAT(ll_test::Memory({parts[0]}, {parts[1]}),{self.separator(ranges, a + 1)}"
                              f"ll_test::MemoryMatches({parts[2]}, {parts[3]}))"]
            return build
        return None

    def convert_in_macro(self, offset, records):
        """A call written in a #define of this file: its arguments are the
        macro's text, so they are split as text, and every expansion must
        agree on what the call means and use it as a statement among others."""
        name = records[0]["name"]
        kinds = {self.conversion_kind(r) for r in records}
        if len(kinds) > 1 and "dependent" in kinds:
            kinds.discard("dependent")
        contexts = {self.context(r)[0] for r in records}
        if len(kinds) != 1 or contexts != {"block"}:
            self.check(offset, f"{name}() in a macro whose uses differ, or are not statements in a block")
            return
        kind = kinds.pop()
        if kind in ("dependent", "bits-?", "distance-?") or name in TUT_MEMBERS or name == "ensure_errno":
            self.check(offset, f"{name}() in a macro, as {kind}")
            return
        parts = self.text_call(offset)
        if parts is None:
            self.check(offset, f"cannot split the arguments of {name}() in its macro")
            return
        end, texts = parts
        statements = text_statements(name, texts, macro=True, kind=kind)
        if statements is None:
            self.check(offset, f"{name}() with {len(texts)} arguments in a macro")
            return
        if any(self.in_destructor(r) for r in records):
            statements = [re.sub(r"^REQUIRE", "CHECK", st) for st in statements]
        self.headers.update(headers_for(name, kind))
        self.plans.append(Plan(offset, end, lambda conv, plan, s=statements: "; ".join(s)))

    def text_call(self, offset):
        """(end, [argument texts]) of a call, read from its text, or None
        where the text alone cannot tell its arguments apart."""
        open_paren = self.src.masked.find(b"(", offset)
        close = self.src.matching(open_paren) if open_paren >= 0 else -1
        if close < 0:
            return None
        inside = self.src.masked[open_paren + 1:close]
        depth = 0
        segments = []
        start = 0
        for i, c in enumerate(inside):
            if c in b"([{":
                depth += 1
            elif c in b")]}":
                depth -= 1
            elif c == 0x2C and depth == 0:
                segments.append((start, i))
                start = i + 1
        segments.append((start, len(inside)))
        # A comma between template arguments looks like one between call
        # arguments.
        if len(segments) > 1 and b"<" in inside and b">" in inside:
            return None
        texts = [self.text(open_paren + 1 + s, open_paren + 1 + e).strip() for s, e in segments]
        if texts == [""]:
            texts = []
        return close + 1, texts

    # ---- what the AST does not hold ----------------------------------
    def convert_uncompiled(self):
        """Tests and calls in branches this host does not compile: by their
        text, where their types do not matter."""
        header = re.compile(rb"template\s*<\s*>\s*template\s*<\s*>\s*void\s+((?:::)?\w+(?:\s*::\s*\w+)*)"
                            rb"\s*::\s*test\s*<\s*(\d+)\s*>\s*\(\s*\)")
        known = {t.start for t in self.tests}
        for m in header.finditer(self.src.masked):
            if m.start() in known or self.src.state(m.start()) != "inactive":
                continue
            parts = [p.decode() for p in re.split(rb"\s*::\s*", m.group(1)) if p]
            cls = parts[-1]
            if cls == "object" and len(parts) > 1:
                cls = parts[-2]  # test_group<X>::object, by the group's typedef
            data = self.typedefs.get(cls) or self.text_typedefs().get(cls) or self.text_groups().get(cls)
            open_brace = self.src.skip_space(m.end())
            close = self.src.matching(open_brace) if self.src.masked[open_brace:open_brace + 1] == b"{" else -1
            test = Test()
            test.n = int(m.group(2))
            test.inactive = True
            test.start = m.start()
            test.header_end = m.end()
            test.body_start = open_brace
            test.body_end = close + 1 if close >= 0 else None
            test.data = data
            test.namespaces = self.namespaces_at(m.start())
            if data is None or close < 0:
                self.check(m.start(), f"test<{test.n}>, not compiled here, belongs to no group this converter found")
                test.macro = True
            self.tests.append(test)
        names = "|".join(sorted(TUT_FUNCTIONS | TUT_MEMBERS, key=len, reverse=True))
        call = re.compile(rb"(?<![\w.>:])(?:tut\s*::\s*)?(" + names.encode() + rb")\s*\(")
        throw = re.compile(rb"(?<![\w])throw\s+(?:tut\s*::\s*)?failure\s*\(")
        handled = set(self.calls)
        found = [(m.start(), m.group(1).decode()) for m in call.finditer(self.src.masked)]
        found += [(m.start(), "throw") for m in throw.finditer(self.src.masked)]
        for offset, name in sorted(found):
            if offset in handled or self.src.state(offset) != "inactive":
                continue
            before = self.src.masked[self.src.line_start(offset):offset].decode("latin-1")
            if re.search(r"\b(void|bool|int|auto|static|inline|std::string|const)\s*[&*]?\s*$", before):
                continue  # a declaration, not a call
            self.convert_text_call(offset, name)

    def remove_uncompiled_groups(self):
        """A group's typedefs and object in a branch this host does not
        compile: by their text."""
        masked = self.src.masked
        taken = [(a, b) for a, b, _ in self.removals]

        def remove(m):
            if self.src.state(m.start()) == "inactive" and not any(a <= m.start() < b for a, b in taken):
                self.removals.append((m.start(), m.end(), "decl"))
                taken.append((m.start(), m.end()))
        groups = set()
        for m in re.finditer(rb"typedef\s+(?:tut\s*::\s*)?test_group\s*<[^;{}]*>\s*(\w+)\s*;", masked):
            remove(m)
            if self.src.state(m.start()) == "inactive":
                groups.add(m.group(1))
        for m in re.finditer(rb"typedef\s+(?:tut\s*::\s*)?(\w+)\s*::\s*object\s+\w+\s*;", masked):
            if m.group(1) in groups:
                remove(m)
        for m in re.finditer(rb"(?:tut\s*::\s*)?(\w+)\s+\w+\s*\(\s*\"[^\"\n]*\"\s*\)\s*;", masked):
            if m.group(1) in groups:
                remove(m)
        for m in re.finditer(rb"(?:tut\s*::\s*)?test_group\s*<[^;{}]*>\s*\w+\s*\(\s*\"[^\"\n]*\"\s*\)\s*;", masked):
            remove(m)

    def convert_text_call(self, offset, name):
        if name == "set_test_name":
            return  # folded into the case's name by name_tests
        if name == "get_test_name":
            end = self.src.matching(self.src.masked.find(b"(", offset))
            self.headers.add('"altestname.h"')
            self.plans.append(Plan(offset, end + 1, lambda conv, plan: "ll_test::currentTestName()"))
            return
        if name in TUT_ASSERTING or name == "throw":
            self.counted[offset] = True
        if name not in TYPE_FREE and name != "throw":
            self.check(offset, f"{name}(), in code not compiled here, converts by its arguments' types")
            return
        parts = self.text_call(offset)
        if parts is None:
            self.check(offset, f"cannot split the arguments of {name}(), in code not compiled here")
            return
        end, texts = parts
        after = self.src.skip_space(end)
        if self.src.masked[after:after + 1] != b";":
            self.check(offset, f"{name}(), in code not compiled here, is not a statement")
            return
        statements = text_statements(name, texts, macro=False)
        if statements is None:
            self.check(offset, f"{name}() with {len(texts)} arguments, in code not compiled here")
            return
        # The lone statement of an if, else or loop, by what precedes it.
        lead = self.src.masked[max(0, offset - 400):offset].decode("latin-1").rstrip()
        lone = re.search(r"(\)|\belse|\bdo)$", lead) is not None
        if len(statements) > 1 and lone:
            text = "{ " + "; ".join(statements) + "; }"
            self.plans.append(Plan(offset, after + 1, lambda conv, plan, t=text: t))
        else:
            joiner = ";\n" + self.src.indent_at(offset).decode() if self.src.starts_line(offset) else "; "
            text = joiner.join(statements)
            self.plans.append(Plan(offset, end, lambda conv, plan, t=text: t))

    def text_groups(self):
        """typedef test_group<X> G: G -> X, from the text."""
        found = {}
        for m in re.finditer(rb"typedef\s+(?:tut\s*::\s*)?test_group\s*<\s*([\w:]+)\s*(?:,\s*\d+\s*)?>\s*(\w+)\s*;",
                             self.src.masked):
            data = m.group(1).decode()
            found[m.group(2).decode()] = data if "::" in data else "tut::" + data
        return found

    def text_typedefs(self):
        if hasattr(self, "_text_typedefs"):
            return self._text_typedefs
        found = {}
        groups = {}
        for m in re.finditer(rb"typedef\s+(?:tut\s*::\s*)?test_group\s*<\s*([\w:]+)\s*(?:,\s*\d+\s*)?>\s*(\w+)\s*;",
                             self.src.masked):
            groups[m.group(2).decode()] = m.group(1).decode()
        for m in re.finditer(rb"typedef\s+(?:tut\s*::\s*)?(\w+)\s*::\s*object\s+(\w+)\s*;", self.src.masked):
            if m.group(1).decode() in groups:
                found[m.group(2).decode()] = "tut::" + groups[m.group(1).decode()]
        self._text_typedefs = found
        return found

    def namespaces_at(self, offset):
        stack = []
        for m in re.finditer(rb"\bnamespace\s*(\w*)\s*\{|[{}]", self.src.masked[:offset]):
            if m.group(0).startswith(b"namespace"):
                stack.append(m.group(1).decode())
            elif m.group(0) == b"{":
                stack.append(None)
            elif stack:
                stack.pop()
        return tuple(s for s in stack if s is not None)

    # ---- tests, named and placed ---------------------------------------
    def resolve_tests(self):
        demangled = self.demangled
        for test in self.tests:
            if test.data is None and getattr(test, "mangled", None):
                full = demangled.get(test.mangled, "")
                m = re.match(r"^void tut::test_object<(.*)>::test<\d+>\(\)$", full)
                if m:
                    test.data = m.group(1)
            group = self.find_group(test.data) if test.data else None
            if group is None and test.data:
                # a group only text can see
                group = self.text_group(test.data)
            test.group = group
            if group is None:
                if not test.macro:
                    self.check(test.start, f"test<{test.n}> belongs to no group this converter found")
                    test.macro = True
                continue
            group.tests.append(test)
            test.fixture = self.spell_fixture(test.data, test.namespaces)
        self.collect_names()

    def find_group(self, data):
        """The group whose data is data, which the group may have named
        without its namespaces."""
        plain = data.replace("(anonymous namespace)::", "")
        for key, group in self.groups.items():
            k = key.replace("(anonymous namespace)::", "")
            if k == plain or ("::" not in k and plain.rpartition("::")[2] == k):
                return group
        return None

    def text_group(self, data):
        short = data.split("::")[-1]
        for m in re.finditer(rb"typedef\s+(?:tut\s*::\s*)?test_group\s*<\s*([\w:]+)\s*(?:,\s*(\d+)\s*)?>\s*(\w+)\s*;",
                             self.src.masked):
            if m.group(1).decode().split("::")[-1] != short:
                continue
            typedef = m.group(3).decode()
            var = re.search(rb"(?:tut\s*::\s*)?" + typedef.encode() + rb"\s+(\w+)\s*\(\s*\"", self.src.masked)
            if not var:
                continue
            q = var.end() - 1
            close = self.src.data.find(b'"', q + 1)
            while close > 0 and self.src.data[close - 1:close] == b"\\":
                close = self.src.data.find(b'"', close + 1)
            name = c_unescape(self.text(q + 1, close))
            group = Group(name, data, int(m.group(2)) if m.group(2) else 50)
            self.groups[data] = group
            for start, end in ((m.start(), m.end()), (var.start(), self.src.masked.find(b";", var.end()) + 1)):
                self.removals.append((start, end, "decl"))
            return group
        return None

    def spell_fixture(self, data, namespaces):
        name = data.replace("(anonymous namespace)::", "")
        prefix = "::".join(n for n in namespaces if n)
        while prefix:
            if name.startswith(prefix + "::"):
                return name[len(prefix) + 2:]
            prefix = prefix.rpartition("::")[0]
        return name

    def collect_names(self):
        """Each test's set_test_name, from the AST or, uncompiled, its text."""
        for offset, records in self.calls.items():
            for r in records:
                if r["name"] != "set_test_name":
                    continue
                test = self.test_containing(offset)
                if test is None:
                    self.check(offset, "set_test_name outside a test's body")
                    continue
                arg = r["args"][0] if r["args"] else None
                literal = skip_implicit(arg) if arg else None
                top = self.context(r)[0] == "block" and self.context(r)[1] is test_body(test)
                test.names.append((offset, r, literal if literal and literal.get("kind") == "StringLiteral"
                                   else None, top))
        name_call = re.compile(rb"(?<![\w.>])set_test_name\s*\(")
        for test in self.tests:
            if not test.inactive or test.body_end is None:
                continue
            for m in name_call.finditer(self.src.masked, test.body_start, test.body_end):
                test.names.append((m.start(), None, "text", True))

    def test_containing(self, offset):
        for test in self.tests:
            if test.body_start is not None and test.body_end is not None and test.body_start <= offset < test.body_end:
                return test
        return None

    def name_tests(self):
        for data, group in self.groups.items():
            seen = []  # (the #if branches around a case, its name)
            twins = {}  # test<N> -> how many of its #if twins came before
            for test in sorted(group.tests, key=lambda t: (t.n, -1 if t.start is None else t.start)):
                twin = twins.get(test.n, 0)
                twins[test.n] = twin + 1
                if test.macro:
                    test.tut_name = None  # left as it is: no case, and parity says so
                    continue
                text_name = None
                if len(test.names) > 1:
                    self.check(test.start, f"test<{test.n}> names itself more than once")
                elif test.names:
                    offset, record, literal, top = test.names[0]
                    if not top or literal is None:
                        self.check(offset, "set_test_name that is not a statement of the test's body, or not a literal")
                    else:
                        call_end = self.src.matching(self.src.masked.find(b"(", offset))
                        inside = self.text(self.src.masked.find(b"(", offset) + 1, call_end)
                        pieces = re.findall(r'"((?:[^"\\\n]|\\.)*)"', inside)
                        stripped = re.sub(r'"((?:[^"\\\n]|\\.)*)"', "", inside).strip()
                        if not pieces or stripped:
                            self.check(offset, "set_test_name with a literal this converter cannot read")
                        else:
                            text_name = "".join(c_unescape(p) for p in pieces)
                            test.name_literal_inner = "".join(pieces)
                            self.remove_statement(offset, call_end + 1)
                test.tut_name = text_name if text_name is not None else ("" if not test.names else None)
                # A name given in --names wins; then set_test_name's; then the
                # first sentence of the comment written above the test.
                given = self.name_overrides.get((group.name, test.n))
                if isinstance(given, list):
                    given = given[twin] if twin < len(given) else None
                if given is None and text_name is None:
                    given = self.comment_name(test)
                if given is not None:
                    text_name = given
                    test.name_literal_inner = c_escape(given)
                base = f"{group.name}: {text_name}" if text_name is not None else f"{group.name}: #{test.n}"
                # Only tests in different branches of one #if are never
                # compiled together; any other two may be, so their names
                # must differ.
                where = self.src.conditionals_at(test.start)
                name = base
                if any(other == name.lower() and not exclusive(where, at) for at, other in seen):
                    name = f"{base} #{test.n}"
                seen.append((where, name.lower()))
                test.catch2_name = name
                if text_name is not None and name == base:
                    test.catch2_literal = '"' + c_escape(group.name + ": ") + test.name_literal_inner + '"'
                else:
                    test.catch2_literal = '"' + c_escape(name) + '"'

    def comment_name(self, test):
        """A name from the comment lines right above a test, or None."""
        if test.start is None:
            return None
        start = self.src.line_start(test.start)
        top = start
        while top > 0:
            prev = self.src.line_start(top - 1)
            if self.src.masked[prev:top].strip() == b"" and self.src.data[prev:top].strip():
                top = prev
                continue
            break
        text = self.text(top, start)
        if not text.strip():
            text = "\n".join(self.comments_in(test.start, test.header_end))
        return name_from_comment(text)

    def remove_statement(self, start, end):
        after = self.src.skip_space(end)
        if self.src.masked[after:after + 1] == b";":
            end = after + 1
        self.removals.append((start, end, "statement"))

    # ---- the plans ----------------------------------------------------
    def plan_fixtures(self):
        """A group whose data struct holds nothing, and which nothing else in
        the file names, is plain TEST_CASEs: the struct goes."""
        for data, group in self.groups.items():
            group.fixtureless = False
            tests = [t for t in group.tests if not t.macro and t.catch2_name]
            if not tests or any(t.inactive for t in group.tests):
                continue
            node = self.records.get(tests[0].data) or self.records.get(data)
            if node is None:
                short = data.replace("(anonymous namespace)::", "").rpartition("::")[2]
                found = [n for k, n in self.records.items() if k.rpartition("::")[2] == short]
                node = found[0] if len(found) == 1 else None
            if node is None or node.get("bases") or node.get("name") in self.friend_names:
                continue
            if [c for c in node.get("inner", []) if "kind" in c and not c.get("isImplicit")]:
                continue
            rng = self.node_range(node)
            if rng is None or rng[2] != "file":
                continue
            spans = [(rng[0], rng[1])] + [(s, e) for s, e, _ in self.removals] + \
                [(t.start, t.header_end) for t in group.tests if t.start is not None and t.header_end]
            named = [m.start() for m in re.finditer(rb"\b" + re.escape(node["name"].encode()) + rb"\b",
                                                    self.src.masked)]
            if any(not any(s <= at < e for s, e in spans) for at in named):
                continue
            group.fixtureless = True
            self.remove_decl(node)

    def plan_structure(self):
        for test in self.tests:
            if test.macro or test.group is None or test.catch2_name is None:
                continue
            tag = group_tag(test.group.name)
            if getattr(test.group, "fixtureless", False):
                test.header_text = f"TEST_CASE({test.catch2_literal},"
                header = f'TEST_CASE({test.catch2_literal}, "[{tag}]")'
            else:
                if "," in test.fixture:
                    self.check(test.start, f"the fixture {test.fixture} has a comma, which TEST_CASE_METHOD would split")
                    continue
                test.header_text = f"TEST_CASE_METHOD({test.fixture}, {test.catch2_literal},"
                header = f'TEST_CASE_METHOD({test.fixture}, {test.catch2_literal}, "[{tag}]")'
            # A comment inside template<> template<> void X::test<N>() stays,
            # on a line of its own above the case.
            indent = self.src.indent_at(test.start).decode()
            comments = [c.strip() for c in self.comments_in(test.start, test.header_end)]
            header = "".join(f"{c}\n{indent}" for c in comments) + header
            self.plans.append(Plan(test.start, test.header_end, lambda conv, plan, h=header: h))
            if test.n > test.group.limit:
                self.notes.append(f"{test.group.name} test<{test.n}> is past the group's limit of "
                                  f"{test.group.limit}: TUT never ran it, Catch2 will")
        spans = []
        for start, end, what in self.removals:
            ls = self.src.line_start(start)
            le = self.src.line_end(end)
            if self.src.masked[ls:start].strip() == b"" and self.src.masked[end:le].strip() == b"":
                start, end = ls, min(le + 1, len(self.src.data))
                if what == "decl":
                    start = self.comment_above(start)
            spans.append((start, end))
        self.removed_spans = self.tidy_removals(spans)
        self.remove_empty_namespaces()
        self.removed_spans = self.tidy_removals(self.removed_spans)
        for start, end in self.removed_spans:
            self.plans.append(Plan(start, end, lambda conv, plan: ""))

    def can_unwrap(self, converted):
        """Whether namespace tut is only TUT's left: nothing names tut::, no
        type production code befriends as tut:: is here, and no call is left
        unconverted that would look for TUT in it."""
        if self.checks or not self.tut_namespaces:
            return False
        masked = mask_source(converted.encode("utf-8", "surrogateescape"))
        if re.search(rb"\btut\s*::|\busing\s+namespace\s+tut\b", masked):
            return False
        return not any(re.search(rb"\b" + re.escape(n.encode()) + rb"\b", masked) for n in self.friend_names)

    def plan_unwrap(self):
        """namespace tut becomes an anonymous namespace, and an anonymous
        namespace written directly inside it, on lines of its own, merges
        into it."""
        taken = [(p.start, p.end) for p in self.plans if p.start != p.end]

        def free(s, e):
            return not any(a < e and s < b for a, b in taken)
        size = len(self.src.data)
        for node, parents in self.tut_namespaces:
            rng = self.node_range(node)
            if rng is None or rng[2] != "file" or any(p.get("kind") == "NamespaceDecl" for p in parents):
                continue
            m = re.compile(rb"namespace\s+tut\b").match(self.src.masked, rng[0])
            if not m or not free(m.start(), m.end()):
                continue
            self.plans.append(Plan(m.start(), m.end(), lambda conv, plan: "namespace"))
            # A comment on its closing brace that says whose it was says so no
            # longer.
            note = re.compile(rb"//[ \t]*(?:end[ \t]+(?:of[ \t]+)?)?(?:namespace[ \t]+)?tut\b[^\n]*").search(
                self.src.data, rng[1], self.src.line_end(rng[1] - 1))
            if note and free(note.start(), note.end()):
                self.plans.append(Plan(note.start(), note.end(), lambda conv, plan: "// anonymous namespace"))
            for child in node.get("inner", []):
                if child.get("kind") != "NamespaceDecl" or child.get("name"):
                    continue
                crng = self.node_range(child)
                if crng is None or crng[2] != "file":
                    continue
                opener = self.src.masked.find(b"{", crng[0])
                close = crng[1]
                if opener < 0 or not re.fullmatch(rb"namespace\s*", self.src.masked[crng[0]:opener]):
                    continue
                os_, oe = self.src.line_start(crng[0]), self.src.line_end(opener)
                cs, ce = self.src.line_start(close - 1), self.src.line_end(close - 1)
                if self.src.masked[os_:crng[0]].strip() or self.src.masked[opener + 1:oe].strip() or \
                        self.src.masked[cs:close - 1].strip() or self.src.masked[close:ce].strip():
                    continue
                oe, ce = min(oe + 1, size), min(ce + 1, size)
                if not (free(os_, oe) and free(cs, ce)):
                    continue
                self.plans.append(Plan(os_, oe, lambda conv, plan: ""))
                self.plans.append(Plan(cs, ce, lambda conv, plan: ""))
                taken += [(os_, oe), (cs, ce)]

    def comment_above(self, start):
        """Where the comment lines right above a removed declaration begin,
        if they stand apart from what precedes them, as a comment about the
        group does: they go with it."""
        top = start
        while top > 0:
            prev = self.src.line_start(top - 1)
            if self.src.masked[prev:top].strip() == b"" and self.src.data[prev:top].strip():
                top = prev
                continue
            break
        if top == start:
            return start
        if top == 0:
            return top
        before = self.src.line_start(top - 1)
        line = self.src.masked[before:top].strip()
        return top if line == b"" or line.endswith(b"{") else start

    def comments_in(self, start, end):
        """The comments between start and end, as written: a test's header
        holds no string literal to mistake for one."""
        return [m.group(0).decode("utf-8", "surrogateescape")
                for m in re.finditer(rb"//[^\n]*|/\*.*?\*/", self.src.data[start:end], re.S)]

    def tidy_removals(self, spans):
        """Whole-line removals merged where only blank lines part them, and
        the blank lines they leave doubled made single."""
        def blank(ls):
            return ls < len(self.src.data) and self.src.data[ls:self.src.line_end(ls)].strip() == b""
        spans = sorted(spans)
        merged = []
        for start, end in spans:
            if merged:
                ps, pe = merged[-1]
                if start <= pe or (self.src.data[pe:start].strip() == b"" and pe == self.src.line_start(pe)
                                   and start == self.src.line_start(start)):
                    merged[-1] = (ps, max(pe, end))
                    continue
            merged.append((start, end))
        out = []
        for start, end in merged:
            whole = start == self.src.line_start(start) and (end == len(self.src.data) or
                                                             end == self.src.line_start(end))
            if whole and start > 0:
                prev = self.src.line_start(start - 1)
                prev_text = self.src.masked[prev:start].strip()
                if (blank(prev) or prev_text.endswith(b"{")) and blank(end):
                    end = min(self.src.line_end(end) + 1, len(self.src.data))
            out.append((start, end))
        return out

    def remove_empty_namespaces(self):
        """An anonymous namespace that held only the group goes with it."""
        for item in self.items:
            for node in iter_nodes(item):
                if node.get("kind") != "NamespaceDecl" or node.get("name"):
                    continue
                rng = self.node_range(node)
                if rng is None or rng[2] != "file":
                    continue
                inner = [c for c in node.get("inner", []) if "kind" in c and not c.get("isImplicit")]
                spans = [self.node_range(c) for c in inner]
                if not inner or not all(s and any(a <= s[0] and s[1] <= b for a, b in self.removed_spans)
                                        for s in spans):
                    continue
                start, end = rng[0], rng[1]
                le = self.src.line_end(end)
                if self.src.masked[end:le].strip() == b"":
                    end = min(le + 1, len(self.src.data))
                ls = self.src.line_start(start)
                if self.src.masked[ls:start].strip() == b"":
                    start = ls
                self.removed_spans = [(a, b) for a, b in self.removed_spans if not (start <= a and b <= end)]
                self.removed_spans.append((start, end))

    def plan_order(self):
        """TUT ran a file's groups in the byte order of their names (its
        runner keeps them in a std::map keyed by name) and each group's
        tests by number; Catch2 runs the cases as declared. So, walking
        TUT's run order, a test declared before the one TUT ran just ahead
        of it moves later in the file, never earlier, to just after the
        latest-declared of those placed so far. A move stays in its own
        namespace, though it may pass into a later block of it, and in its
        #if branch: where one would have to leave them, the cases' order is
        left as declared, but for each group's tests, put in their numeric
        order where they can be, and the file is marked, since its cases
        run in another order than TUT ran them."""
        # A test this host does not compile is in another #if branch: never
        # declared beside these, it has no place in their order.
        tests = [t for t in self.tests if not t.macro and t.body_end is not None and t.catch2_name
                 and not t.inactive and t.group is not None]
        run = sorted(tests, key=lambda t: (t.group.name.encode("utf-8"), t.n))
        if run == sorted(tests, key=lambda t: t.start):
            return
        slot, stuck = self.slots(run)
        if not stuck:
            self.move(run, slot)
            self.notes.append("tests moved into TUT's run order: groups by name, each group's tests by number")
            return
        first = min(stuck, key=lambda t: t.start)
        groups = ", ".join(dict.fromkeys(t.group.name for t in run))
        self.check(first.start, f"TUT ran this file's groups by name ({groups}), each group's tests by number, and "
                                f"{first.group.name} test<{first.n}> cannot move to its place in that order out "
                                f"of its namespace or #if: these cases run in another order than TUT's")
        self.notes.append("the cases run in another order than TUT ran them: a move out of a namespace or an #if "
                          "was needed")
        for group in self.groups.values():
            own = [t for t in run if t.group is group]
            if [t.n for t in sorted(own, key=lambda t: t.start)] == [t.n for t in own]:
                continue
            slot, stuck = self.slots(own)
            if stuck:
                for t in stuck:
                    self.check(t.start, f"test<{t.n}> is declared out of TUT's numeric order, and cannot move to "
                                        f"its place out of its namespace or #if")
                continue
            self.move(own, slot)
            self.notes.append(f"{group.name}: tests moved into TUT's numeric order")

    def slots(self, run):
        """For tests in the order TUT ran them, the test each goes just after
        (itself, where it stays), and those that would leave their
        namespace or #if branch to get there."""
        slot = {}
        latest = None
        for test in run:
            if latest is None or test.start > latest.start:
                latest = test
            slot[id(test)] = latest
        stuck = [t for t in run if slot[id(t)] is not t and self.nesting(t.start) != self.nesting(slot[id(t)].start)]
        return slot, stuck

    def move(self, run, slot):
        """Plans that put each test just after its slot, in run order."""
        assigned = {}
        for test in run:
            assigned.setdefault(id(slot[id(test)]), []).append(test)
        chunks = {id(t): self.chunk(t) for t in run}
        for test in sorted(run, key=lambda t: t.start):
            members = assigned.get(id(test), [])
            if members == [test]:
                continue
            parts = [chunks[id(m)] for m in members]
            start, end = chunks[id(test)]
            if not members:
                # A test moved away takes the blank line after it along,
                # where nothing else is planned there.
                after = self.src.line_end(end)
                if end < len(self.src.data) and self.src.data[end:after].strip() == b"" and \
                        not any(end <= p.start <= after or (p.start < end < p.end) for p in self.plans):
                    end = min(after + 1, len(self.src.data))
            plan = Plan(start, end, None)
            plan.render = lambda conv, plan, parts=parts: "\n".join(
                conv.render(s, e, skip=conv.slot_at.get(s)) for s, e in parts)
            self.slot_at[plan.start] = plan
            self.plans.append(plan)

    def nesting(self, offset):
        """Where offset is, for a move: the namespaces around it, by name,
        where it is directly in one, since every block of a namespace is
        one scope and a later block of it has declared all an earlier one
        did; else the brace of the block it is in; and its #if branches."""
        if self.live is None:
            # Code under #if 0 need not even be C++: none of its braces count.
            live = bytearray(self.src.masked)
            for start, end, what in self.src.regions:
                if what == "dead":
                    live[start:end] = re.sub(rb"[^\n]", b" ", bytes(live[start:end]))
            self.live = bytes(live)
        stack = []  # (where a { is, the namespace it opens or None)
        last = 0
        for m in re.finditer(rb"[{};]", self.live[:offset]):
            if m.group(0) == b"{":
                head = b" ".join(line for line in self.live[last:m.start()].split(b"\n")
                                 if not line.strip().startswith(b"#"))
                name = re.fullmatch(rb"\s*(?:inline\s+)?namespace\s*([\w:]*)\s*", head)
                stack.append((m.start(), name.group(1).decode() if name else None))
            elif m.group(0) == b"}" and stack:
                stack.pop()
            last = m.end()
        if any(name is None for _, name in stack):
            return ("block", stack[-1][0]), self.branch_of(offset)
        path = tuple(part for _, name in stack for part in (name.split("::") if name else [""]))
        return ("namespace", path), self.branch_of(offset)

    def chunk(self, test):
        """A test's text with the comment lines right above it, through the end
        of its last line."""
        start = self.src.line_start(test.start)
        while start > 0:
            prev = self.src.line_start(start - 1)
            if self.src.masked[prev:start].strip() == b"" and self.src.data[prev:start].strip():
                start = prev
                continue
            break
        end = self.src.line_end(test.body_end - 1)
        return start, min(end + 1, len(self.src.data))

    def branch_of(self, offset):
        """The #if branches around offset, by where each starts."""
        stack = []
        for start, end, kw, _ in self.src.directives:
            if start > offset:
                break
            if kw in ("if", "ifdef", "ifndef"):
                stack.append(end)
            elif kw in ("elif", "else", "elifdef", "elifndef") and stack:
                stack[-1] = end
            elif kw == "endif" and stack:
                stack.pop()
        return tuple(stack)

    def plan_includes(self):
        found = list(TUT_INCLUDE.finditer(self.src.data))
        found = [m for m in found if self.src.state(m.start()) != "dead"]
        headers = sorted(h for h in self.headers if h.startswith("<")) + \
            sorted(h for h in self.headers if h.startswith('"'))
        lines = "".join(f"#include {h}\n" for h in headers)
        if found:
            first = found[0]
            self.plans.append(Plan(first.start(), first.end(), lambda conv, plan: lines))
            for m in found[1:]:
                self.plans.append(Plan(m.start(), m.end(), lambda conv, plan: ""))
        else:
            includes = list(re.finditer(rb"^[ \t]*#[ \t]*include[^\n]*\n", self.src.data, re.M))
            first_code = min((t.start for t in self.tests if t.start is not None), default=len(self.src.data))
            before = [m for m in includes if m.end() <= first_code and self.src.state(m.start()) == "active"]
            at = before[-1].end() if before else 0
            self.plans.append(Plan(at, at, lambda conv, plan: lines))
            self.notes.append("no direct include of lltut.h: the Catch2 headers go after the last include")

    def plan_checks(self):
        by_line = {}
        for offset, why in self.checks:
            by_line.setdefault(self.src.line_start(offset), []).append((offset, why))
        for start, entries in by_line.items():
            whys = "; ".join(dict.fromkeys(w for _, w in entries))
            if self.src.directive_at(start):
                offset = min(o for o, _ in entries)
                self.plans.append(Plan(offset, offset, lambda conv, plan, w=whys: f"/* TM-CHECK: {w} */ "))
            else:
                indent = self.src.indent_at(start).decode()
                self.plans.append(Plan(start, start, lambda conv, plan, w=whys, i=indent: f"{i}// TM-CHECK: {w}\n"))

    # ---- rendering -----------------------------------------------------
    def render(self, start, end, skip=None):
        """The text of [start, end) with every plan inside it carried out."""
        out = []
        pos = start
        for plan in self.sorted_plans:
            if plan is skip or plan.start < pos:
                continue
            if plan.start == plan.end:
                if plan.start >= end:
                    continue
            elif plan.end > end:
                if plan.start >= end:
                    break
                continue
            out.append(self.text(pos, plan.start))
            out.append(plan.text(self))
            pos = plan.end
        out.append(self.text(pos, end))
        return "".join(out)

    def finish(self):
        self.sorted_plans = sorted(self.plans, key=lambda p: (p.start, 0 if p.start == p.end else 1, -p.end))
        stack = []
        for plan in self.sorted_plans:
            if plan.start == plan.end:
                continue
            while stack and stack[-1].end <= plan.start:
                stack.pop()
            if stack and plan.end > stack[-1].end:
                raise ConvertError(f"{self.src.path}: edits overlap at line {self.src.line_of(plan.start)}")
            stack.append(plan)
        return self.render(0, len(self.src.data))

    def counts(self, converted):
        """Per test: the TUT assertion calls in its body before, the Catch2
        assertions in it after, and the Catch2 assertions those calls became;
        and for the file, the TUT assertion calls outside every test's body
        (a helper's, a fixture's, a lambda's at namespace scope) and the
        Catch2 assertions those became. Calls are counted as in the bodies:
        in a branch this host compiles or not, never under #if 0 or in a
        comment, so a recount of the converted file finds as many."""
        tut = {}
        expected = {}
        inside = set()
        for test in self.tests:
            if test.body_start is None or test.body_end is None:
                continue
            calls = [o for o in self.counted if test.body_start <= o < test.body_end]
            inside.update(calls)
            tut[id(test)] = len(calls)
            # A split && chain is more than one assertion for one TUT call.
            expected[id(test)] = sum(self.emitted.get(o, 1) for o in calls)
        data = converted.encode("utf-8", "surrogateescape")
        catch = {id(t): n for t, n in count_catch_assertions(data, [
            (t, getattr(t, "header_text", f"TEST_CASE_METHOD({t.fixture}, {t.catch2_literal},"))
            for t in self.tests if t.catch2_name and not t.macro])}
        outside = [o for o in self.counted if o not in inside]
        return tut, catch, expected, len(outside), sum(self.emitted.get(o, 1) for o in outside)


def count_catch_assertions(data, cases):
    """For each (key, header text) in cases, the Catch2 assertions in the body
    that follows the header in data: code under #if 0 left out."""
    masked = mask_source(data)
    dead = dead_regions(masked)
    out = []
    for key, header in cases:
        at = data.find(header.encode("utf-8", "surrogateescape"))
        if at < 0:
            out.append((key, None))
            continue
        brace = masked.find(b"{", at + len(header))
        depth = 0
        i = brace
        while i < len(masked):
            if masked[i] == 0x7B:
                depth += 1
            elif masked[i] == 0x7D:
                depth -= 1
                if depth == 0:
                    break
            i += 1
        found = [m.start() for m in CATCH_ASSERTION.finditer(masked, brace, i + 1)]
        out.append((key, sum(1 for f in found if not any(s <= f < e for s, e in dead))))
    return out


def dead_regions(masked):
    """[start, end) of each #if 0 branch."""
    regions = []
    stack = []
    for m in re.finditer(rb"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b([^\n]*)", masked, re.M):
        kw, rest = m.group(1).decode(), m.group(2).decode("latin-1").strip()
        if kw in ("if", "ifdef", "ifndef"):
            stack.append([m.end(), kw == "if" and re.fullmatch(r"\(?\s*(0|false)\s*\)?", rest) is not None])
        elif stack:
            start, dead = stack[-1]
            if dead:
                regions.append((start, m.start()))
            if kw == "endif":
                stack.pop()
            else:
                stack[-1] = [m.end(), kw == "elif" and re.fullmatch(r"\(?\s*(0|false)\s*\)?", rest) is not None]
    return regions


def compared(node):
    """The two operands of an == or != comparison, built in or overloaded,
    or ()."""
    kind = node.get("kind")
    inner = [c for c in node.get("inner", []) if isinstance(c, dict) and "kind" in c]
    if kind == "BinaryOperator" and node.get("opcode") in ("==", "!=") and len(inner) == 2:
        return inner
    if kind == "CXXOperatorCallExpr" and len(inner) == 3:
        name = skip_implicit(inner[0]).get("referencedDecl", {}).get("name")
        if name in ("operator==", "operator!="):
            return inner[1:]
    return ()


def is_end(node):
    """Whether a compared operand is where nothing is: null, a container's
    end() and the like, or npos."""
    if node.get("kind") == "ImplicitCastExpr" and node.get("castKind") == "NullToPointer":
        return True
    node = skip_implicit(node)
    kind = node.get("kind")
    if kind in ("CXXNullPtrLiteralExpr", "GNUNullExpr"):
        return True
    if kind == "DeclRefExpr" and node.get("referencedDecl", {}).get("name") == "npos":
        return True
    if kind == "CXXMemberCallExpr":
        callee = next((c for c in node.get("inner", []) if "kind" in c), {})
        return callee.get("kind") == "MemberExpr" and callee.get("name") in END_MEMBERS
    return False


def variable_of(node):
    """The declaration a node names, where it is a variable or a field."""
    kind = node.get("kind")
    if kind == "DeclRefExpr":
        ref = node.get("referencedDecl", {})
        if ref.get("kind") in ("VarDecl", "ParmVarDecl", "BindingDecl"):
            return ref.get("id")
    elif kind == "MemberExpr" and node.get("type", {}).get("qualType") != "<bound member function type>":
        return node.get("referencedMemberDecl")
    return None


def variables(node, constants=True):
    """The variables and fields a node reads, by declaration, without those
    of default arguments; without, too, unless constants, a constant of
    arithmetic type, which is no state."""
    found = set()
    stack = [node]
    while stack:
        sub = stack.pop()
        if sub.get("kind") == "CXXDefaultArgExpr":
            continue
        decl = variable_of(sub)
        if decl:
            t = sub.get("referencedDecl", {}).get("type", {}) if sub.get("kind") == "DeclRefExpr" \
                else sub.get("type", {})
            text = t.get("desugaredQualType") or t.get("qualType") or ""
            if constants or not re.fullmatch(r"const\s+" + ARITHMETIC.pattern, text.strip()):
                found.add(decl)
        stack.extend(c for c in sub.get("inner", []) if isinstance(c, dict) and "kind" in c)
    return found


# What a subscript through a class reads as a pointer would: a handle to
# something else, not a value of its own.
HANDLES = re.compile(r"iterator|__wrap_iter|optional|unique_ptr|shared_ptr|weak_ptr|LLPointer|_ptr\b|Ptr\b")


def side_effect(node):
    """Whether evaluating node changes something of itself: ++ or --, an
    assignment or compound assignment, built in or overloaded. A call may
    too, but is not looked in: calls are too common in a condition, and
    those whose failure leaves garbage are REQUIRE's business."""
    stack = [node]
    while stack:
        sub = stack.pop()
        kind = sub.get("kind")
        if kind in ("LambdaExpr", "CXXDefaultArgExpr"):
            continue
        inner = [c for c in sub.get("inner", []) if isinstance(c, dict) and "kind" in c]
        if kind == "UnaryOperator" and sub.get("opcode") in ("++", "--"):
            return True
        if kind == "CompoundAssignOperator" or (kind == "BinaryOperator" and sub.get("opcode") == "="):
            return True
        if kind == "CXXOperatorCallExpr" and inner:
            name = skip_implicit(inner[0]).get("referencedDecl", {}).get("name", "")
            if re.fullmatch(r"operator(?:[-+*/%^&|]|<<|>>)?=|operator(?:\+\+|--)", name):
                return True
        stack.extend(inner)
    return False


def leaned_on(node):
    """The variables and fields a condition leans on for its own safety:
    those it dereferences (unary *, ->, a class's operator* or
    operator->), uses as a subscript or a pointer's offset, or subscripts
    through where they are a pointer, an iterator, an optional or a smart
    pointer. A value's member read with a dot, a constant subscript of a
    value's array (v.mV[VX]) and a call are no such reliance. Constants of
    arithmetic type are left out, as no state."""
    found = set()
    stack = [node]
    while stack:
        sub = stack.pop()
        kind = sub.get("kind")
        if kind == "CXXDefaultArgExpr":
            continue
        inner = [c for c in sub.get("inner", []) if isinstance(c, dict) and "kind" in c]
        if kind == "UnaryOperator" and sub.get("opcode") == "*" and inner:
            found |= variables(inner[0], constants=False)
        elif kind == "MemberExpr" and sub.get("isArrow") and inner:
            found |= variables(inner[0], constants=False)
        elif kind == "CXXOperatorCallExpr" and inner:
            name = skip_implicit(inner[0]).get("referencedDecl", {}).get("name")
            if name in ("operator*", "operator->") and len(inner) == 2:
                found |= variables(inner[1], constants=False)
            elif name == "operator[]" and len(inner) == 3:
                found |= variables(inner[2], constants=False)
                if HANDLES.search(value_type(skip_implicit(inner[1]))):
                    found |= variables(inner[1], constants=False)
        elif kind == "ArraySubscriptExpr" and len(inner) == 2:
            found |= variables(inner[1], constants=False)
            # An array decays to the pointer subscripted; a pointer is one.
            if "[" not in value_type(skip_implicit_casts(inner[0])):
                found |= variables(inner[0], constants=False)
        elif kind in ("BinaryOperator", "CompoundAssignOperator") and sub.get("opcode") in ("+", "-", "+=", "-=") \
                and len(inner) == 2:
            for offset, other in ((inner[0], inner[1]), (inner[1], inner[0])):
                if value_type(other).endswith("*") and not value_type(offset).endswith("*"):
                    found |= variables(offset, constants=False)
        stack.extend(inner)
    return found


def declared_bare(decl):
    """Whether a variable is declared without an initializer: none at all,
    or, for a class, its default constructor called with nothing written.
    Whether that constructor sets the members is not in the AST read here
    (the class is a header's; its JSON says nothing of it), and an empty
    one, as LLVector4a's, leaves them indeterminate: a class counts, set
    or not."""
    if "init" not in decl:
        return True
    if decl.get("init") != "call":
        return False
    inner = [c for c in decl.get("inner", []) if isinstance(c, dict) and "kind" in c]
    if len(inner) != 1 or inner[0].get("kind") != "CXXConstructExpr" or inner[0].get("list"):
        return False
    return all(a.get("kind") == "CXXDefaultArgExpr" for a in inner[0].get("inner", []) if "kind" in a)


def written_root(node):
    """The variable an expression a call writes through is part of: x of
    x, x.m, x[i] where x is an array, &x; None where it is no variable's
    own (what a pointer points at, a container's element)."""
    while True:
        kind = node.get("kind")
        inner = [c for c in node.get("inner", []) if isinstance(c, dict) and "kind" in c]
        if kind == "DeclRefExpr":
            ref = node.get("referencedDecl", {})
            return ref.get("id") if ref.get("kind") == "VarDecl" else None
        if not inner:
            return None
        if kind in ("ImplicitCastExpr", "ParenExpr", "MaterializeTemporaryExpr", "CXXBindTemporaryExpr") or \
                (kind == "MemberExpr" and not node.get("isArrow")) or \
                (kind == "UnaryOperator" and node.get("opcode") == "&") or \
                (kind == "ArraySubscriptExpr" and "[" in value_type(skip_implicit_casts(inner[0]))):
            node = skip_implicit_casts(inner[0]) if kind == "ArraySubscriptExpr" else inner[0]
            continue
        return None


def const_object(base, arrow):
    """Whether the object a member function is called on is const there, as
    clang typed it: a const member function called on an object that is not
    const has it converted to const, so a call on an object that stays
    non-const is to a non-const member function."""
    text = base.get("type", {}).get("qualType", "")
    if arrow and "*" in text:
        text = text[:text.rfind("*")]
    while re.search(r"<[^<>]*>", text):
        text = re.sub(r"<[^<>]*>", "", text)
    return re.search(r"\bconst\b", text) is not None


def fills(arg):
    """Whether a call may fill an argument: an lvalue passed as it is, bound
    to a non-const reference, or the address of one, to a pointer to
    non-const. One bound to a const reference, or to a const pointer, is
    converted to const first."""
    kind = arg.get("kind")
    text = arg.get("type", {}).get("qualType", "")
    if kind == "UnaryOperator" and arg.get("opcode") == "&":
        return not const_object(arg, True)
    if kind in ("ImplicitCastExpr", "CXXConstructExpr", "MaterializeTemporaryExpr", "CXXBindTemporaryExpr") or \
            arg.get("valueCategory") != "lvalue":
        return False
    while re.search(r"<[^<>]*>", text):
        text = re.sub(r"<[^<>]*>", "", text)
    return re.search(r"\bconst\b", text) is None


def names_only(record):
    """Whether a class derived from test_group holds nothing but a
    constructor, taking nothing and doing nothing, that passes the group its
    name: nothing TUT ran around the group's tests goes with it."""
    own = [c for c in record.get("inner", []) if "kind" in c and not c.get("isImplicit")
           and c.get("kind") != "AccessSpecDecl"]
    if len(own) != 1 or own[0].get("kind") != "CXXConstructorDecl":
        return False
    parts = [c for c in own[0].get("inner", []) if "kind" in c]
    inits = [c for c in parts if c.get("kind") == "CXXCtorInitializer"]
    rest = [c for c in parts if c.get("kind") != "CXXCtorInitializer"]
    if len(inits) != 1 or "baseInit" not in inits[0]:
        return False
    return len(rest) == 1 and rest[0].get("kind") == "CompoundStmt" and \
        not [c for c in rest[0].get("inner", []) if "kind" in c]


def exclusive(a, b):
    """Whether code at two places, given by Source.conditionals_at, is never
    compiled together: only where they lie in different branches of one
    #if. Different #ifs may both hold, whatever their conditions say."""
    for (chain_a, branch_a), (chain_b, branch_b) in zip(a, b):
        if chain_a != chain_b:
            return False
        if branch_a != branch_b:
            return True
    return False


def test_body(test):
    return next((c for c in test.node.get("inner", []) if c.get("kind") == "CompoundStmt"), None) \
        if test.node else None


def skip_implicit_casts(node):
    while node.get("kind") in ("ImplicitCastExpr", "MaterializeTemporaryExpr", "CXXBindTemporaryExpr") \
            and node.get("inner"):
        node = node["inner"][0]
    return node


def find_kind(node, kind):
    for sub in iter_nodes(node):
        if sub.get("kind") == kind:
            return sub
    return None


def iter_nodes(node):
    stack = [node]
    while stack:
        cur = stack.pop()
        if isinstance(cur, dict):
            if "kind" in cur:
                yield cur
            stack.extend(reversed([c for c in cur.get("inner", []) if isinstance(c, dict)]))


def split_template_args(text):
    out = []
    depth = 0
    cur = []
    for ch in text:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    out.append("".join(cur).strip())
    return out


def needs_macro_guard(masked_text):
    """Whether text, inside a macro that does not take __VA_ARGS__, would be
    split: a comma outside parentheses, or a macro that could expand to one."""
    depth = 0
    for c in masked_text:
        if c == 0x28:
            depth += 1
        elif c == 0x29:
            depth -= 1
        elif c == 0x2C and depth == 0:
            return True
    return bool(re.search(rb"\{|\b[A-Z][A-Z0-9_]{2,}\s*\(", masked_text))


def text_statements(name, texts, macro, kind=None):
    """The statements a call becomes, from its arguments' text alone: for
    code no AST shows (only the calls whose types do not matter), and for a
    call in a macro, whose kind the macro's uses decided and whose
    arguments may be anything, so are parenthesised where they meet an
    operator."""
    simple = re.compile(r'[\w.:]+(\(\s*\))?|"(?:[^"\\]|\\.)*"')
    kind = kind or name

    def operand(t):
        return t if simple.fullmatch(t) else f"({t})"

    def condition(t):
        if macro:
            return operand(t)
        if re.search(r"&&|\|\||\?|[^=!<>]=[^=]|,", t) and not wrapped(t):
            return f"({t})"
        return t

    def message(t):
        if t in ("NULL", "nullptr", "0", '""'):
            return []
        return [f"UNSCOPED_INFO({t if simple.fullmatch(t) else f'({t})'})"]
    n = len(texts)
    if name in ("ensure", "ensure_not"):
        macro_name = "REQUIRE_FALSE" if name == "ensure_not" else "REQUIRE"
        if n not in (1, 2):
            return None
        return (message(texts[0]) if n == 2 else []) + [f"{macro_name}({condition(texts[-1])})"]
    if name in ("fail", "skip", "throw"):
        if n != 1:
            return None
        return [f"{'SKIP' if name == 'skip' else 'FAIL'}({operand(texts[0])})"]
    if name in ("ensure_equals", "ensure_not_equals"):
        if n not in (2, 3):
            return None
        msg = message(texts[0]) if n == 3 else []
        a, e = operand(texts[-2]), operand(texts[-1])
        if kind == "equals-llsd":
            return msg + [f"REQUIRE_THAT({a}, ll_test::LLSDEquals({e}))"]
        if kind == "equals-uri":
            return msg + [f"REQUIRE({a}.asString() == {e}.asString())"]
        return msg + [f"REQUIRE({a} {'==' if name == 'ensure_equals' else '!='} {e})"]
    if name in ("ensure_contains", "ensure_does_not_contain", "ensure_starts_with", "ensure_ends_with"):
        if n != 3:
            return None
        matcher = {"ensure_contains": "Catch::Matchers::ContainsSubstring",
                   "ensure_does_not_contain": "!Catch::Matchers::ContainsSubstring",
                   "ensure_starts_with": "Catch::Matchers::StartsWith",
                   "ensure_ends_with": "Catch::Matchers::EndsWith"}[name]
        return message(texts[0]) + [f"REQUIRE_THAT({operand(texts[1])}, {matcher}({texts[2]}))"]
    if name == "ensure_distance" and kind.startswith("distance-"):
        if n not in (3, 4):
            return None
        msg = message(texts[0]) if n == 4 else []
        a, e, d = texts[-3:]
        return msg + [f"REQUIRE_THAT({operand(a)}, ll_test::WithinDistance<{kind[9:]}>({e}, {d}))"]
    if name == "ensure_approximately_equals_range":
        if n != 4:
            return None
        a, e, d = texts[-3:]
        return message(texts[0]) + [f"REQUIRE_THAT(F32({a}), Catch::Matchers::WithinAbs(F32({e}), F32({d})))"]
    if name == "ensure_approximately_equals" and kind in ("bits-F32", "bits-F64"):
        if n not in (3, 4):
            return None
        msg = message(texts[0]) if n == 4 else []
        a, e, bits = texts[-3:]
        return msg + [f"REQUIRE_THAT({operand(a)}, ll_test::WithinBits({kind[5:]}({e}), {bits}))"]
    return None


def wrapped(text):
    """Whether text is one parenthesised whole."""
    if not (text.startswith("(") and text.endswith(")")):
        return False
    depth = 0
    for i, ch in enumerate(text):
        depth += ch == "("
        depth -= ch == ")"
        if depth == 0 and i < len(text) - 1:
            return False
    return True


def group_tag(name):
    tag = re.sub(r"[^A-Za-z0-9_:+-]+", "_", name).strip("_")
    if not tag or not tag[0].isalnum():
        tag = "tut_" + tag
    return tag


# --------------------------------------------------------------------------
# One file
# --------------------------------------------------------------------------
_FRIENDS = None


def tut_friends():
    """The tut:: types production code names as friends: they keep their
    namespace, so a file holding one keeps namespace tut."""
    global _FRIENDS
    if _FRIENDS is None:
        _FRIENDS = set()
        for top, dirs, files in os.walk(os.path.join(ROOT, "indra")):
            dirs[:] = [d for d in dirs if d not in ("tests", "test", "externals")]
            for f in files:
                if f.endswith((".h", ".hpp", ".inl")):
                    with open(os.path.join(top, f), "rb") as fh:
                        for m in re.finditer(rb"friend\s+(?:struct|class)\s+(?:::\s*)?tut\s*::\s*(\w+)", fh.read()):
                            _FRIENDS.add(m.group(1).decode())
    return _FRIENDS


def load_names(path):
    """--names: {"<file from the root>": {"<group>": {"<n>": "<name>"}}}, or
    for a test<N> defined in branches of one #if, a list of names, one a
    twin in the order the file declares them."""
    if not path:
        return {}
    with open(path) as f:
        data = json.load(f)
    out = {}
    for file, groups in data.items():
        for group, tests in groups.items():
            for n, name in tests.items():
                if not isinstance(name, str) and not (isinstance(name, list) and all(isinstance(x, str) for x in name)):
                    raise SystemExit(f"--names: {file} {group} {n}: a name, or a list of names, not {name!r}")
                out[(os.path.normpath(file), group, int(n))] = name
    return out


def convert_file(path, build_dir, config, dry_run, output_dir=None, names=None):
    db = CompileDatabase(build_dir, config)
    source = Source(path)
    if not TUT_INCLUDE.search(source.data) and b"test_group" not in source.data:
        return {"file": path, "skipped": "no TUT here", "tests": [], "checks": [], "notes": []}
    items, tut_ids, active_lines = run_clang(db, path)
    if os.environ.get("TUT2CATCH_DUMP"):
        with open(os.environ["TUT2CATCH_DUMP"], "w") as f:
            json.dump(items, f, indent=1)
    mangled = sorted({n.get("mangledName") for item in items for n in iter_nodes(item)
                      if n.get("kind") == "CXXMethodDecl" and n.get("name") == "test"
                      and "test_object" in n.get("mangledName", "")})
    conv = Converter(source, items, tut_ids, active_lines, demangle(mangled))
    rel = os.path.normpath(os.path.relpath(path, ROOT))
    conv.name_overrides = {(g, n): name for (f, g, n), name in (names or {}).items() if f == rel}
    conv.friend_names = tut_friends()
    sys.setrecursionlimit(max(sys.getrecursionlimit(), 20000))
    conv.analyse()
    conv.resolve_calls()
    conv.convert_uncompiled()
    conv.resolve_tests()
    conv.remove_uncompiled_groups()
    conv.name_tests()
    conv.plan_fixtures()
    conv.plan_structure()
    conv.plan_order()
    conv.plan_includes()
    conv.plan_checks()
    converted = conv.finish()
    if conv.can_unwrap(converted):
        conv.plan_unwrap()
        converted = conv.finish()
    tut_counts, catch_counts, expected_counts, tut_outside, catch_outside = conv.counts(converted)
    if output_dir:
        out = os.path.join(output_dir, os.path.relpath(path, ROOT))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(converted.encode("utf-8", "surrogateescape"))
    elif not dry_run:
        with open(path, "wb") as f:
            f.write(converted.encode("utf-8", "surrogateescape"))
    tests = []
    for test in sorted(conv.tests, key=lambda t: ((t.group.name if t.group else ""), t.n or 0)):
        tests.append({
            "file": os.path.relpath(path, ROOT),
            "group": test.group.name if test.group else None,
            "n": test.n,
            "line": source.line_of(test.start) if test.start is not None else None,
            "tut_name": test.tut_name,
            "catch2_name": test.catch2_name,
            "tut_assertions": tut_counts.get(id(test)),
            "catch2_expected": expected_counts.get(id(test)),
            "catch2_assertions": catch_counts.get(id(test)),
            "not_compiled_here": test.inactive,
            "past_group_limit": bool(test.group and test.n and test.n > test.group.limit),
        })
    checks = [{"file": os.path.relpath(path, ROOT), "line": source.line_of(o), "why": w}
              for o, w in sorted(conv.checks)]
    totals = {"file": os.path.relpath(path, ROOT), "tut_sha256": hashlib.sha256(source.data).hexdigest(),
              "catch2_sha256": hashlib.sha256(converted.encode("utf-8", "surrogateescape")).hexdigest(),
              "tut_outside_calls": tut_outside, "catch2_outside_expected": catch_outside}
    return {"file": os.path.relpath(path, ROOT), "tests": tests, "totals": totals, "checks": checks,
            "notes": conv.notes,
            "groups": sorted(g.name for g in conv.groups.values())}


def convert_one(args):
    path, build_dir, config, dry_run, output_dir, names = args
    result = {}

    def work():
        try:
            result["value"] = convert_file(path, build_dir, config, dry_run, output_dir, names)
        except ConvertError as e:
            result["value"] = {"file": os.path.relpath(path, ROOT), "error": str(e), "tests": [], "checks": [],
                               "notes": []}
        except Exception:
            # A bug of the converter's own, met in one file: it is that
            # file's error, traceback and all, and the others go on.
            result["value"] = {"file": os.path.relpath(path, ROOT), "error": traceback.format_exc().rstrip(),
                               "tests": [], "checks": [], "notes": []}
    # Deep ASTs recurse deeply.
    threading.stack_size(512 * 1024 * 1024)
    t = threading.Thread(target=work)
    t.start()
    t.join()
    return result["value"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("files", nargs="+", help="TUT test files to convert in place")
    parser.add_argument("--build-dir", default=os.path.join(ROOT, DEFAULT_BUILD),
                        help="the build tree whose compile_commands.json gives each file's flags")
    parser.add_argument("--config", default=DEFAULT_CONFIG, help="which configuration's flags to use")
    parser.add_argument("--mapping", help="write the TUT-to-Catch2 mapping, as JSON, here")
    parser.add_argument("--dry-run", action="store_true", help="convert nothing; report and map only")
    parser.add_argument("--output-dir", help="write each converted file under here, at its path from the "
                        "repository's root, instead of over the original")
    parser.add_argument("--jobs", type=int, default=1, help="files to convert at once")
    parser.add_argument("--quiet", action="store_true", help="print only the marks and the totals")
    parser.add_argument("--names", help='cases\' names, as JSON: {"<file from the root>": {"<group>": '
                        '{"<n>": "<name>"}}}, or a list of names for a test<N> in branches of one #if, '
                        'one a twin in the order the file declares them; each wins over the test\'s own')
    args = parser.parse_args()

    files = [os.path.abspath(f) for f in args.files]
    names = load_names(args.names)
    work = [(f, os.path.abspath(args.build_dir), args.config, args.dry_run, args.output_dir, names) for f in files]
    if args.jobs > 1:
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(convert_one, work))
    else:
        results = [convert_one(w) for w in work]

    mapping = []
    totals = []
    total_checks = 0
    errors = 0
    for r in results:
        if r.get("error"):
            errors += 1
            print(f"{r['file']}: error: {r['error']}")
            continue
        if r.get("skipped"):
            if not args.quiet:
                print(f"{r['file']}: {r['skipped']}")
            continue
        mapping.extend(r["tests"])
        totals.append(r["totals"])
        total_checks += len(r["checks"])
        if not args.quiet:
            converted = sum(1 for t in r["tests"] if t["catch2_name"])
            print(f"{r['file']}: {converted} of {len(r['tests'])} tests in {len(r['groups'])} group(s), "
                  f"{len(r['checks'])} TM-CHECK")
            for note in r["notes"]:
                print(f"  note: {note}")
        for c in r["checks"]:
            print(f"{c['file']}:{c['line']}: TM-CHECK: {c['why']}")
    if args.mapping:
        with open(args.mapping, "w") as f:
            json.dump({"tests": mapping, "files": totals}, f, indent=1)
    print(f"{len(results)} file(s), {len(mapping)} test(s), {total_checks} TM-CHECK(s), {errors} error(s)")
    return 2 if errors else (1 if total_checks else 0)


if __name__ == "__main__":
    sys.exit(main())
