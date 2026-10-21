#!/usr/bin/env python3
"""\
Counts the raw OpenGL calls (gl* functions) each source file under indra/newview makes and checks
them against raw_gl_baseline.txt: a file may make no more than the baseline gives it, and none at all
where it gives nothing. llrender owns GL; newview reaches it through gGL, LLRenderTarget,
ALGPUReadback and the rest.

Comments, string literals and #if 0 blocks are not counted. A file that makes fewer calls than the
baseline gives it fails too, so the baseline is lowered as calls move out: run with --update.

    check_raw_gl.py              check every file under indra/newview
    check_raw_gl.py FILE...      check these files, those outside indra/newview ignored
    check_raw_gl.py --update     write the baseline from the tree as it is

$LicenseInfo:firstyear=2026&license=viewerlgpl$
Alchemy Viewer Source Code
Copyright (C) 2026, Rye <rye@alchemyviewer.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation;
version 2.1 of the License only.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
$/LicenseInfo$
"""

import argparse
import re
import sys
from pathlib import Path, PurePosixPath

REPO = Path(__file__).resolve().parents[2]
BASELINE = Path(__file__).resolve().with_name("raw_gl_baseline.txt")
SCANNED = PurePosixPath("indra/newview")
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl", ".m", ".mm"}
CALL = re.compile(r"\bgl[A-Z]\w*(?=\s*\()")
DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def strip_comments_and_strings(text):
    """The text with comments blanked and string and character literals emptied, lines kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("\n" * text.count("\n", i, end))
            i = end
        elif text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            open_paren = text.find("(", i + 2)
            if open_paren < 0:
                out.append(text[i])
                i += 1
                continue
            closing = ")" + text[i + 2:open_paren] + '"'
            end = text.find(closing, open_paren)
            end = n if end < 0 else end + len(closing)
            out.append('""' + "\n" * text.count("\n", i, end))
            i = end
        elif text[i] in "\"'":
            quote = text[i]
            j = i + 1
            while j < n and text[j] != quote and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(quote + quote)
            # An unclosed one, a digit separator's quote among them, ends at its line.
            i = j if j < n and text[j] == "\n" else j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def strip_if_zero(text):
    """The text with the branches of #if 0 and #elif 0 blanked. Any other condition is taken as compiled."""
    lines = text.split("\n")
    stack = []  # per open conditional: whether the branch in force is a 0
    for index, line in enumerate(lines):
        match = DIRECTIVE.match(line)
        if match:
            kind, rest = match.group(1), match.group(2).strip()
            zero = rest == "0"
            if kind in ("if", "ifdef", "ifndef"):
                stack.append(kind == "if" and zero)
            elif kind == "elif" and stack:
                stack[-1] = zero
            elif kind == "else" and stack:
                stack[-1] = False
            elif kind == "endif" and stack:
                stack.pop()
            lines[index] = ""
        elif any(stack):
            lines[index] = ""
    return "\n".join(lines)


def raw_calls(text):
    """(line, name) for each raw gl* call the source makes, its parenthesis on the same line or a later one."""
    code = strip_if_zero(strip_comments_and_strings(text))
    return [(code.count("\n", 0, match.start()) + 1, match.group(0)) for match in CALL.finditer(code)]


def read_baseline(path):
    baseline = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            name, count = line.rsplit(None, 1)
            baseline[name] = int(count)
    return baseline


def write_baseline(path, counts):
    lines = ["# Raw gl* calls each file under indra/newview may make; checked by check_raw_gl.py"]
    lines += [f"{name} {count}" for name, count in sorted(counts.items()) if count]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def scanned_files(root):
    return sorted(p for p in (root / SCANNED).rglob("*") if p.suffix in SUFFIXES and "tests" not in
                  p.relative_to(root / SCANNED).parts)


def key(root, path):
    return path.resolve().relative_to(root).as_posix()


def check(root, baseline, files):
    """The messages for every file whose count is not its baseline's."""
    problems = []
    for path in files:
        name = key(root, path)
        calls = raw_calls(path.read_text(encoding="utf-8", errors="replace"))
        allowed = baseline.get(name, 0)
        if len(calls) > allowed:
            sites = "\n".join(f"    {name}:{line}: {call}" for line, call in calls)
            problems.append(f"{name}: {len(calls)} raw GL calls where {allowed} are allowed. Reach GL through "
                            f"llrender (gGL, LLRenderTarget, ALGPUReadback, ...) instead:\n{sites}")
        elif len(calls) < allowed:
            problems.append(f"{name}: {len(calls)} raw GL calls where the baseline allows {allowed}: run "
                            f"scripts/code_tools/check_raw_gl.py --update")
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("files", nargs="*", type=Path, help="files to check, rather than all of indra/newview")
    parser.add_argument("--update", action="store_true", help="write the baseline from the tree as it is")
    parser.add_argument("--root", type=Path, default=REPO, help="the repository's root")
    parser.add_argument("--baseline", type=Path, default=BASELINE, help="the baseline file")
    args = parser.parse_args(argv)
    root = args.root.resolve()

    if args.update:
        counts = {key(root, p): len(raw_calls(p.read_text(encoding="utf-8", errors="replace")))
                  for p in scanned_files(root)}
        write_baseline(args.baseline, counts)
        print(f"{args.baseline}: {sum(counts.values())} raw GL calls in {sum(1 for c in counts.values() if c)} files")
        return 0

    baseline = read_baseline(args.baseline)
    if args.files:
        newview = (root / SCANNED).resolve()
        files = [f.resolve() for f in args.files if f.suffix in SUFFIXES and f.exists() and
                 newview in f.resolve().parents and "tests" not in f.resolve().relative_to(newview).parts]
    else:
        files = scanned_files(root)
        present = {key(root, p) for p in files}
        missing = [name for name in baseline if name not in present]
        if missing:
            print("The baseline names files that are gone: run scripts/code_tools/check_raw_gl.py --update\n    " +
                  "\n    ".join(missing))
            return 1

    problems = check(root, baseline, files)
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
