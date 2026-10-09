#!/usr/bin/env python3
"""Proves a TUT file and its Catch2 conversion test the same things.

Two steps, either side of scripts/testing/tut2catch.py:

  record   before converting: runs TUT test executables, verbose, through
           the commands, working directories and environments their CTest
           entries give, and keeps every line a test's end prints:
             [group, N: name] ok | fail | exception: ... | skipped known failure | ...
           A test TUT never reported (a dummy test<N> slot) is not a test.
  check    after converting and building: runs each converted file's CTest
           entry, <binary>/<file stem>, as XML, and matches the cases to the
           TUT results through tut2catch.py's mapping.

Parity holds for a file when:
  - every TUT test it reported has exactly one Catch2 case, and every case
    of the file is one of its TUT tests;
  - each pair agrees: ok is a pass, a TUT skip is a Catch2 skip, and every
    TUT failure (fail, exception, abnormal end) is a Catch2 failure; and the
    name TUT printed is the one the mapping folded into the case's;
  - each case's assertions, counted in the converted file, number its
    TUT test's ensure, fail and skip calls, as the mapping counted them in
    the TUT file.

A test the mapping says TUT never ran (past its group's limit) or this host
does not compile is listed, not counted against parity. So is what the
Catch2 harness's state guard says a case left changed: TUT never looked, so
it is a finding about the test, not a difference the conversion made; the
table's exit column shows the run's exit code. It prints a table, one row a
file, and exits 1 on any mismatch, listing each.

    python3 -I scripts/testing/tut_parity.py record --out /tmp/tut.json \\
        INTEGRATION_TEST_RUNNER_v3math PROJECT_llmath_TEST_v3color
    python3 -I scripts/testing/tut2catch.py --mapping /tmp/map.json indra/llmath/tests/v3*_test.cpp
    (build the Catch2 binary)
    python3 -I scripts/testing/tut_parity.py check --tut /tmp/tut.json --mapping /tmp/map.json
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_BUILD = "build-Darwin-ninja-os"
DEFAULT_CONFIG = "RelWithDebInfo"
RESULT_LINE = re.compile(
    r"^\[(?P<group>.+?), (?P<n>\d+)(?:: (?P<name>.*?))?\] "
    r"(?P<result>ok|fail|exception|test destructor throw|abnormal termination|skipped known failure|unknown)\b")
CATCH_ASSERTION = re.compile(
    r"(?<![\w.>:])(?:REQUIRE|REQUIRE_FALSE|REQUIRE_THAT|CHECK|CHECK_FALSE|CHECK_THAT|FAIL|FAIL_CHECK|SKIP)\s*\(")


# --------------------------------------------------------------------------
# CTest's view of a test
# --------------------------------------------------------------------------
def ctest_tests(build_dir, config):
    out = subprocess.run(["ctest", "--test-dir", build_dir, "-C", config, "--show-only=json-v1"],
                         capture_output=True, text=True, check=True)
    tests = {}
    for t in json.loads(out.stdout).get("tests", []):
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        tests[t["name"]] = {"command": t.get("command", []), "props": props}
    return tests


def environment(props):
    env = dict(os.environ)
    for item in props.get("ENVIRONMENT", []):
        key, _, value = item.partition("=")
        env[key] = value
    for item in props.get("ENVIRONMENT_MODIFICATION", []):
        key, _, rest = item.partition("=")
        op, _, value = rest.partition(":")
        old = env.get(key, "")
        if op == "reset":
            env.pop(key, None) if key not in os.environ else env.__setitem__(key, os.environ[key])
        elif op == "set":
            env[key] = value
        elif op == "unset":
            env.pop(key, None)
        elif op == "string_append":
            env[key] = old + value
        elif op == "string_prepend":
            env[key] = value + old
        elif op == "path_list_append":
            env[key] = old + os.pathsep + value if old else value
        elif op == "path_list_prepend":
            env[key] = value + os.pathsep + old if old else value
        elif op == "cmake_list_append":
            env[key] = old + ";" + value if old else value
        elif op == "cmake_list_prepend":
            env[key] = value + ";" + old if old else value
    return env


def run(test, extra, timeout):
    props = test["props"]
    try:
        proc = subprocess.run(test["command"] + extra, cwd=props.get("WORKING_DIRECTORY") or None,
                              env=environment(props), capture_output=True, timeout=timeout)
        return proc.returncode, proc.stdout.decode("utf-8", "replace") + proc.stderr.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        return "timeout", (e.stdout or b"").decode("utf-8", "replace")


def stem_of_tut(name):
    """The test file a TUT CTest entry runs, by stem."""
    m = re.search(r"TEST_RUNNER_(\w+)$", name) or re.search(r"_TEST_(\w+)$", name)
    if not m:
        return name
    return re.sub(r"_(?:baseline|v2|v3|v4)$", "", m.group(1)) + "_test"


# --------------------------------------------------------------------------
# record
# --------------------------------------------------------------------------
def record(args):
    tests = ctest_tests(args.build_dir, args.config)
    out = {}
    if args.out and os.path.exists(args.out):
        with open(args.out) as f:
            out = json.load(f)
    for name in args.tests:
        test = tests.get(name)
        if test is None:
            print(f"{name}: no such CTest entry", file=sys.stderr)
            return 2
        code, text = run(test, ["-v"], args.timeout)
        results = []
        for line in text.splitlines():
            m = RESULT_LINE.match(line)
            if not m:
                continue
            result = m.group("result")
            results.append({"group": m.group("group"), "n": int(m.group("n")), "name": m.group("name") or "",
                            "result": {"ok": "ok", "skipped known failure": "skip"}.get(result, "fail"),
                            "said": line[m.start("result"):].strip()})
        out[stem_of_tut(name)] = {"ctest": name, "exit": code, "results": results}
        counts = {}
        for r in results:
            counts[r["result"]] = counts.get(r["result"], 0) + 1
        print(f"{name}: exit {code}, " + ", ".join(f"{v} {k}" for k, v in sorted(counts.items())))
    if args.out:
        with open(args.out, "w") as f:
            json.dump(out, f, indent=1)
    return 0


# --------------------------------------------------------------------------
# check
# --------------------------------------------------------------------------
def catch2_results(test, timeout):
    """{case name: pass | skip | fail} from running a Catch2 entry as XML."""
    with tempfile.TemporaryDirectory() as tmp:
        report = os.path.join(tmp, "report.xml")
        code, text = run(test, ["-r", f"xml::out={report}"], timeout)
        cases = {}
        try:
            root = ElementTree.parse(report).getroot()
        except (ElementTree.ParseError, FileNotFoundError):
            return code, None, text
        for case in root.iter("TestCase"):
            result = case.find("OverallResult")
            if result is None:
                cases[case.get("name")] = "fail"
                continue
            if result.get("success") != "true":
                cases[case.get("name")] = "fail"
            elif int(result.get("skips", "0")) > 0:
                cases[case.get("name")] = "skip"
            else:
                cases[case.get("name")] = "ok"
        return code, cases, text


def recount(path, cases):
    """The Catch2 assertions in each named case's body, from the file itself."""
    with open(path, "rb") as f:
        text = f.read().decode("utf-8", "surrogateescape")
    masked = mask(text)
    counts = {}
    for m in re.finditer(r'TEST_CASE_METHOD\s*\(', masked):
        open_paren = m.end() - 1
        close = matching(masked, open_paren)
        header = text[open_paren + 1:close]
        literal = re.search(r',\s*((?:"(?:[^"\\]|\\.)*"\s*)+),', header)
        if not literal:
            continue
        name = "".join(unescape(p) for p in re.findall(r'"((?:[^"\\]|\\.)*)"', literal.group(1)))
        brace = masked.find("{", close)
        end = matching(masked, brace)
        counts[name] = len(CATCH_ASSERTION.findall(masked, brace, end))
    return {name: counts.get(name) for name in cases}


def mask(text):
    """Comments blanked and literals' insides replaced, offsets kept."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out[i:j] = " " * (j - i)
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out[i:j] = [c if c == "\n" else " " for c in text[i:j]]
            i = j
        elif text[i] in "\"'":
            if text[i] == "'" and i > 0 and text[i - 1].isalnum() and re.match(r"\d", text[i - 1]):
                i += 1
                continue
            q = text[i]
            j = i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out[i + 1:j] = "_" * (j - i - 1)
            i = j + 1
        else:
            i += 1
    return "".join(out)


def matching(text, at):
    depth = 0
    for i in range(at, len(text)):
        if text[i] in "({[":
            depth += 1
        elif text[i] in ")}]":
            depth -= 1
            if depth == 0:
                return i
    return len(text)


def unescape(body):
    """The value of the inside of a narrow string literal."""
    simple = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'", "a": "\a",
              "b": "\b", "f": "\f", "v": "\v", "?": "?"}
    out = []
    i = 0
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


def check(args):
    with open(args.mapping) as f:
        mapping = json.load(f)
    with open(args.tut) as f:
        tut = json.load(f)
    tests = ctest_tests(args.build_dir, args.config)
    by_file = {}
    for entry in mapping:
        by_file.setdefault(entry["file"], []).append(entry)
    files = sorted(by_file) if not args.files else [os.path.relpath(os.path.abspath(f), ROOT) for f in args.files]

    rows = []
    problems = []
    for path in files:
        entries = by_file.get(path, [])
        stem = os.path.splitext(os.path.basename(path))[0]
        recorded = tut.get(stem)
        if recorded is None:
            problems.append(f"{path}: no TUT results recorded for {stem}")
            continue
        entry_names = [n for n, t in tests.items() if n.endswith("/" + stem)]
        if len(entry_names) != 1:
            problems.append(f"{path}: {len(entry_names)} Catch2 CTest entries end in /{stem}")
            continue
        code, cases, output = catch2_results(tests[entry_names[0]], args.timeout)
        if cases is None:
            problems.append(f"{path}: {entry_names[0]} gave no XML (exit {code}):\n{output[-2000:]}")
            continue
        guard = [line for line in output.splitlines() if line.startswith("State guard:")]
        tut_results = {(r["group"], r["n"]): r for r in recorded["results"]}
        counted = recount(os.path.join(ROOT, path), [e["catch2_name"] for e in entries if e["catch2_name"]])
        matched = agree = counts_ok = 0
        listed = []
        seen_cases = set()
        for e in entries:
            key = (e["group"], e["n"])
            t = tut_results.pop(key, None)
            name = e["catch2_name"]
            if e.get("not_compiled_here") or (e.get("past_group_limit") and t is None):
                listed.append(f"{e['group']} {e['n']}: " +
                              ("not compiled here" if e.get("not_compiled_here") else "past its group's limit"))
                if name in cases:
                    seen_cases.add(name)
                continue
            if t is None:
                problems.append(f"{path}: {e['group']} test<{e['n']}> did not run under TUT")
                continue
            if not name or name not in cases:
                problems.append(f"{path}: {e['group']} test<{e['n']}> has no Catch2 case"
                                + (f" ({name!r})" if name else ""))
                continue
            seen_cases.add(name)
            matched += 1
            if e.get("tut_name") is not None and e["tut_name"] != t["name"]:
                problems.append(f"{path}: {e['group']} test<{e['n']}>: TUT named it {t['name']!r}, "
                                f"the mapping {e['tut_name']!r}")
            if cases[name] == t["result"]:
                agree += 1
            else:
                problems.append(f"{path}: {name!r}: TUT {t['said']!r}, Catch2 {cases[name]}")
            static = counted.get(name)
            if e["tut_assertions"] == e["catch2_assertions"] == static:
                counts_ok += 1
            else:
                problems.append(f"{path}: {name!r}: {e['tut_assertions']} TUT assertion calls, "
                                f"{e['catch2_assertions']} Catch2 assertions mapped, {static} in the file")
        for key, t in sorted(tut_results.items()):
            problems.append(f"{path}: TUT ran [{key[0]}, {key[1]}], which the mapping does not have")
        for name in sorted(set(cases) - seen_cases):
            problems.append(f"{path}: Catch2 case {name!r} is no TUT test of the mapping")
        clean = not any(p.startswith(path + ":") for p in problems)
        rows.append((path, len(recorded["results"]), len(cases), matched, agree, counts_ok, len(listed), code,
                     "ok" if clean else "MISMATCH"))
        for line in listed + guard:
            print(f"{path}: listed: {line}")

    header = ("file", "TUT", "Catch2", "matched", "agree", "counts", "listed", "exit", "parity")
    widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(len(header))]
    for r in [header] + rows:
        print("  ".join(str(v).ljust(w) if i == 0 else str(v).rjust(w) for i, (v, w) in enumerate(zip(r, widths))))
    for p in problems:
        print(f"mismatch: {p}")
    return 1 if problems or any(r[-1] != "ok" for r in rows) else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--build-dir", default=os.path.join(ROOT, DEFAULT_BUILD))
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, default=900, help="seconds a run may take")
    sub = parser.add_subparsers(dest="step", required=True)
    rec = sub.add_parser("record", help="run TUT executables and keep their results")
    rec.add_argument("tests", nargs="+", help="CTest names of TUT tests")
    rec.add_argument("--out", required=True, help="JSON to add the results to")
    chk = sub.add_parser("check", help="run the converted files' Catch2 entries and compare")
    chk.add_argument("--tut", required=True, help="what record kept")
    chk.add_argument("--mapping", required=True, help="tut2catch.py's mapping")
    chk.add_argument("files", nargs="*", help="only these converted files (default: all the mapping has)")
    args = parser.parse_args()
    return record(args) if args.step == "record" else check(args)


if __name__ == "__main__":
    sys.exit(main())
