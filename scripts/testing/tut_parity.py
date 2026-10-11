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
           TUT results through tut2catch.py's mapping. tut2catch.py must be
           beside this script: the converted file is read as it reads one.

Parity holds for a file when:
  - every TUT test it reported has exactly one Catch2 case, and every case
    of the file is one of its TUT tests;
  - each pair agrees: ok is a pass, a TUT skip is a Catch2 skip, and every
    TUT failure (fail, exception, abnormal end) is a Catch2 failure; and the
    name TUT printed is the one the mapping folded into the case's;
  - the cases run in the order TUT ran their tests, groups by name and
    each group's tests by number, since a test may lean on what the ones
    before it left;
  - no two cases share a name (Catch2 takes two that differ in their tags),
    none is tagged [!mayfail] or [!shouldfail], under which Catch2 passes a
    case whatever it does, and the run's exit code fits its cases: 0 when
    some passed and none failed, 125 when all skipped, neither when any
    failed;
  - each case's assertions, counted in the converted file (read as
    tut2catch.py reads it: comments, literals and #if 0 code left out),
    number its TUT test's ensure, fail and skip calls, as the mapping
    counted them in the TUT file, where a && chain the converter split
    counts one a term (catch2_expected). After a file has been edited by
    hand past what the converter wrote, --recounted lists the counts
    instead of holding them to the mapping; names and results must still
    agree;
  - every assertion counted in a passing case's body ran: the run keeps its
    passing assertions (-s), and each CHECK, REQUIRE and _THAT site must
    report from at least one line it spans. A FAIL or SKIP that ran would
    have failed or skipped the case, and a case that failed or skipped may
    have stopped early, so neither is held to it.

A test the mapping says TUT never ran (past its group's limit) or this host
does not compile is listed, not counted against parity, as is an assertion
outside every case (a helper's) that no case ran. So is what the
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
import bisect
import importlib.util
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
# What every test entry exits with when it skips: the harness turns Catch2's
# "all skipped" into it, and CTest reads it as skipped.
SKIP_CODE = 125
# Tags under which Catch2 passes a case whatever it does.
FORGIVING_TAGS = ("[!mayfail]", "[!shouldfail]")
RESULT_LINE = re.compile(
    r"^\[(?P<group>.+?), (?P<n>\d+)(?:: (?P<name>.*?))?\] "
    r"(?P<result>ok|fail|exception|test destructor throw|abnormal termination|skipped known failure|unknown)\b")
# Assertions that report only when they run, and then fail or skip the case:
# in a case that passed, one of these not running is the pass.
REPORTING_ONLY = {"FAIL", "FAIL_CHECK", "SKIP"}
# What a run's XML says an assertion ran with, and where.
RAN_ELEMENTS = {"Expression", "Failure", "Skip"}


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
    """The cases a Catch2 entry ran, as XML with its passing assertions too
    (-s), in the order it ran them: each {name, tags, line, result: ok | skip
    | fail, ran: {(file, line) of each assertion that ran}}. A list, not a
    dict by name: Catch2 takes two cases of one name that differ in their
    tags. Read as it streams: a case that loops over its checks writes
    megabytes."""
    with tempfile.TemporaryDirectory() as tmp:
        report = os.path.join(tmp, "report.xml")
        code, text = run(test, ["-s", "-r", f"xml::out={report}"], timeout)
        cases = []
        case = None
        try:
            for event, element in ElementTree.iterparse(report, events=("start", "end")):
                if event == "start":
                    if element.tag == "TestCase":
                        case = {"name": element.get("name"), "tags": element.get("tags", ""),
                                "line": int(element.get("line", "0")), "result": "fail", "ran": set()}
                    continue
                if case is None:
                    continue
                if element.tag in RAN_ELEMENTS:
                    case["ran"].add((element.get("filename", ""), int(element.get("line", "0"))))
                    element.clear()
                elif element.tag == "OverallResult":
                    if element.get("success") != "true":
                        case["result"] = "fail"
                    elif int(element.get("skips", "0")) > 0:
                        case["result"] = "skip"
                    else:
                        case["result"] = "ok"
                elif element.tag == "TestCase":
                    cases.append(case)
                    case = None
                    element.clear()
        except (ElementTree.ParseError, FileNotFoundError):
            return code, None, text
        return code, cases, text


def exit_fits(code, cases, output):
    """Whether a run's exit code says what its cases do: 0 when some passed
    and none failed (1 when the state guard found a change, which it says),
    the skip code when every case skipped, and neither when any failed or
    none ran -- a filter that matches nothing exits 2."""
    results = {c["result"] for c in cases}
    if "fail" in results or not results:
        return code not in (0, SKIP_CODE, "timeout")
    if results == {"skip"}:
        return code == SKIP_CODE
    return code == 0 or (code == 1 and "The state guard found" in output)


_CONVERTER = None


def converter():
    """tut2catch.py, beside this script: the file is read with the masking
    and the #if 0 regions the mapping's counts were made with. By path, as
    python -I puts no script's directory on sys.path."""
    global _CONVERTER
    if _CONVERTER is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tut2catch.py")
        spec = importlib.util.spec_from_file_location("tut2catch", path)
        _CONVERTER = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_CONVERTER)
    return _CONVERTER


def read_cases(path):
    """The converted file's test cases and assertion sites, read from the file
    as tut2catch.py reads it: comments and literals masked, #if 0 branches
    left out. A site is (first line, last line, macro), the lines its call
    spans; returns ([{name, lines, sites}], the sites outside every case)."""
    tc = converter()
    with open(path, "rb") as f:
        data = f.read()
    masked = tc.mask_source(data)
    dead = tc.dead_regions(masked)
    newlines = [m.start() for m in re.finditer(rb"\n", masked)]

    def live(at):
        return not any(start <= at < end for start, end in dead)

    def line(at):
        return bisect.bisect_left(newlines, at) + 1

    sites = []
    for m in tc.CATCH_ASSERTION.finditer(masked):
        if live(m.start()):
            close = matching(masked, m.end() - 1)
            macro = m.group(0).rstrip(b"( \t\r\n").decode()
            sites.append((m.start(), (line(m.start()), line(close), macro)))
    cases = []
    inside = set()
    for m in re.finditer(rb"\bTEST_CASE(_METHOD)?\s*\(", masked):
        if not live(m.start()):
            continue
        open_paren = m.end() - 1
        close = matching(masked, open_paren)
        header = data[open_paren + 1:close].decode("utf-8", "surrogateescape")
        # The name is the first argument of a TEST_CASE, the second of a
        # TEST_CASE_METHOD.
        literal = re.search(r',\s*((?:"(?:[^"\\]|\\.)*"\s*)+),' if m.group(1) else
                            r'^\s*((?:"(?:[^"\\]|\\.)*"\s*)+),', header)
        if not literal:
            continue
        name = "".join(unescape(p) for p in re.findall(r'"((?:[^"\\]|\\.)*)"', literal.group(1)))
        brace = masked.find(b"{", close)
        end = matching(masked, brace)
        body = [(at, site) for at, site in sites if brace < at < end]
        inside.update(at for at, _ in body)
        cases.append({"name": name, "lines": (line(m.start()), line(brace)), "sites": [site for _, site in body]})
    return cases, [site for at, site in sites if at not in inside]


def matching(masked, at):
    """Where the bracket at at closes, in masked text."""
    depth = 0
    for i in range(at, len(masked)):
        c = masked[i]
        if c in b"({[":
            depth += 1
        elif c in b")}]":
            depth -= 1
            if depth == 0:
                return i
    return len(masked)


def ran_here(case, path):
    """The lines of path a case's run reported an assertion from."""
    suffix = "/" + path.replace("\\", "/")
    return {line for file, line in case["ran"] if ("/" + file.replace("\\", "/")).endswith(suffix)}


def body_of(case, file_cases):
    """The file's case a run's case is: by name, and by the line Catch2 says
    it starts on where two have one name."""
    named = [c for c in file_cases if c["name"] == case["name"]]
    if len(named) > 1:
        named = [c for c in named if c["lines"][0] <= case["line"] <= c["lines"][1]]
    return named[0] if len(named) == 1 else None


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
        # The first case of a name is the one matched; a second is an error
        # of its own.
        by_name = {}
        for case in cases:
            if case["name"] in by_name:
                problems.append(f"{path}: Catch2 has more than one case named {case['name']!r}")
            else:
                by_name[case["name"]] = case
            for tag in FORGIVING_TAGS:
                if tag in case["tags"]:
                    problems.append(f"{path}: {case['name']!r} is tagged {tag}, under which Catch2 passes it "
                                    f"whatever it does")
        if not exit_fits(code, cases, output):
            counts = {}
            for case in cases:
                counts[case["result"]] = counts.get(case["result"], 0) + 1
            problems.append(f"{path}: {entry_names[0]} exited {code}, which does not fit its cases ("
                            + (", ".join(f"{v} {k}" for k, v in sorted(counts.items())) or "none") + ")")
        tut_results = {(r["group"], r["n"]): r for r in recorded["results"]}
        file_cases, outside = read_cases(os.path.join(ROOT, path))
        counted = {}
        for c in file_cases:
            counted.setdefault(c["name"], len(c["sites"]))
        # An assertion counted is not one that ran: a case passes as well
        # when a loop around its CHECK never turns. Each site in a passing
        # case's body must have reported at least once.
        ran_anywhere = set()
        for case in cases:
            ran = ran_here(case, path)
            ran_anywhere |= ran
            if case["result"] != "ok":
                continue
            body = body_of(case, file_cases)
            for first, last, macro in body["sites"] if body else []:
                if macro not in REPORTING_ONLY and not any(first <= n <= last for n in ran):
                    problems.append(f"{path}: {case['name']!r} passed without running the {macro} on line {first}")
        never = [f"the {macro} on line {first}, outside every case, never ran"
                 for first, last, macro in outside
                 if macro not in REPORTING_ONLY and not any(first <= n <= last for n in ran_anywhere)]
        matched = agree = counts_ok = 0
        listed = []
        seen_cases = set()
        paired = {}
        for e in entries:
            key = (e["group"], e["n"])
            name = e["catch2_name"]
            # Before taking TUT's result: a test<N> defined in both branches
            # of an #if is two entries, and the result is the active twin's.
            if e.get("not_compiled_here") or (e.get("past_group_limit") and key not in tut_results):
                listed.append(f"{e['group']} {e['n']}: " +
                              ("not compiled here" if e.get("not_compiled_here") else "past its group's limit"))
                if name in by_name:
                    seen_cases.add(name)
                continue
            t = tut_results.pop(key, None)
            if t is None:
                problems.append(f"{path}: {e['group']} test<{e['n']}> did not run under TUT")
                continue
            if not name or name not in by_name:
                problems.append(f"{path}: {e['group']} test<{e['n']}> has no Catch2 case"
                                + (f" ({name!r})" if name else ""))
                continue
            seen_cases.add(name)
            paired[name] = key
            matched += 1
            if e.get("tut_name") is not None and e["tut_name"] != t["name"]:
                problems.append(f"{path}: {e['group']} test<{e['n']}>: TUT named it {t['name']!r}, "
                                f"the mapping {e['tut_name']!r}")
            if by_name[name]["result"] == t["result"]:
                agree += 1
            else:
                problems.append(f"{path}: {name!r}: TUT {t['said']!r}, Catch2 {by_name[name]['result']}")
            static = counted.get(name)
            # One TUT call is one Catch2 assertion, but for a && chain split
            # into one assertion a term, which the mapping counted.
            expected = e.get("catch2_expected", e["tut_assertions"])
            if expected == e["catch2_assertions"] == static:
                counts_ok += 1
            elif args.recounted and static is not None:
                counts_ok += 1
                print(f"{path}: listed: {name!r}: {expected} assertions converted, {static} in the file now")
            else:
                problems.append(f"{path}: {name!r}: {e['tut_assertions']} TUT assertion calls, "
                                f"{expected} Catch2 assertions they became, {e['catch2_assertions']} mapped, "
                                f"{static} in the file")
        for key, t in sorted(tut_results.items()):
            problems.append(f"{path}: TUT ran [{key[0]}, {key[1]}], which the mapping does not have")
        for name in sorted(set(by_name) - seen_cases):
            problems.append(f"{path}: Catch2 case {name!r} is no TUT test of the mapping")
        # TUT ran a file's groups by name and each group's tests by number,
        # and a test may lean on what the ones before it left.
        keys = set(paired.values())
        tut_order = [(r["group"], r["n"]) for r in recorded["results"] if (r["group"], r["n"]) in keys]
        catch2_order = [(paired[c["name"]], c["name"]) for c in cases
                        if c["name"] in paired and by_name[c["name"]] is c]
        for at, (tut_key, (key, name)) in enumerate(zip(tut_order, catch2_order)):
            if tut_key != key:
                problems.append(f"{path}: Catch2 runs {name!r}, [{key[0]}, {key[1]}], as test {at + 1} of "
                                f"{len(tut_order)}, where TUT ran [{tut_key[0]}, {tut_key[1]}]: not TUT's order")
                break
        clean = not any(p.startswith(path + ":") for p in problems)
        rows.append((path, len(recorded["results"]), len(cases), matched, agree, counts_ok, len(listed), code,
                     "ok" if clean else "MISMATCH"))
        for line in listed + never + guard:
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
    chk.add_argument("--recounted", action="store_true",
                     help="the files were edited by hand since conversion: list each case's assertion count "
                          "rather than hold it to the mapping's (names and results still must match)")
    chk.add_argument("files", nargs="*", help="only these converted files (default: all the mapping has)")
    args = parser.parse_args()
    return record(args) if args.step == "record" else check(args)


if __name__ == "__main__":
    sys.exit(main())
