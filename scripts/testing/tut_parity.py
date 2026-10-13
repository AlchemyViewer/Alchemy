#!/usr/bin/env python3
"""Proves a TUT file and its Catch2 conversion test the same things.

Two steps, either side of scripts/testing/tut2catch.py:

  record   before converting: runs TUT test executables, verbose, through
           the commands, working directories and environments their CTest
           entries give, and keeps every line a test's end prints:
             [group, N: name] ok | fail | exception: ... | skipped known failure | ...
           A test TUT never reported (a dummy test<N> slot) is not a test.
           With them it keeps the sha256 of the TUT source, the one
           indra/**/tests/<stem>.cpp git tracks, and it refuses an
           executable built before that source last changed: the entry's
           own target, wherever it is in the command (llcorehttp's runs
           under a Python peer), as check finds a Catch2 entry's binary. A
           test built at other
           x86-64 tiers too (an entry ending _baseline, _v2, _v3, _v4) is
           kept a tier apart, under <stem>@<tier>; so is a second run of a
           test's executable under another environment, an entry named for
           the executable's test, an underscore and the run (alscript's
           <test>_newsolver), under <stem>@<run>.
  check    after converting and building: runs each converted file's CTest
           entry, <binary>/<file stem>, as XML, and matches the cases to the
           TUT results through tut2catch.py's mapping. tut2catch.py must be
           beside this script: the converted file is read as it reads one.
           A file built at other tiers too has an entry
           <binary>_<tier>/<file stem> at each, held to TUT's run at that
           tier; the tree's own binary is held to TUT's untiered run (or,
           where there is none, its run at the tree's tier, AL_ISA_TIER). A
           tier the host does not run skips on both sides, which is listed.
           A file run a second time has an entry <binary>_<run>/<file stem>
           (al_add_test_binary's RERUN), held to TUT's run of that name.

Parity holds for a file when:
  - its inputs are the ones the run is of: the converted file is no newer
    than the executable its entry runs, and where the recorded results
    and the mapping both have the TUT source's sha256, the two agree (where
    either has none, that is listed); and the file is the one the converter
    wrote, by the mapping's catch2_sha256, unless --recounted says it was
    edited by hand and the edits were reviewed -- an edit that weakens an
    assertion keeps every count, so nothing else here would see it;
  - every TUT test it reported has exactly one Catch2 case, and every case
    of the file is one of its TUT tests;
  - each pair agrees: ok is a pass, a TUT skip is a Catch2 skip, and every
    TUT failure (fail, exception, abnormal end) is a Catch2 failure; and the
    name TUT printed is the one the mapping folded into the case's, but
    where a test skipped or failed before its set_test_name ran, and so
    printed none, which is listed;
  - the cases run in the order TUT ran their tests, groups by name and
    each group's tests by number, since a test may lean on what the ones
    before it left;
  - no two cases share a name (Catch2 takes two that differ in their tags),
    none is tagged [!mayfail] or [!shouldfail], under which Catch2 passes a
    case whatever it does, and the run's exit code fits its cases: 0 when
    some passed and none failed, 125 when all skipped, neither when any
    failed -- or, where none failed, 1 when the harness says its state
    guard found a change, but not when it says a teardown step threw;
  - each case's assertions, counted in the converted file (read as
    tut2catch.py reads it: comments, literals and #if 0 code left out),
    number its TUT test's ensure, fail and skip calls, as the mapping
    counted them in the TUT file, where a && chain the converter split
    counts one a term (catch2_expected). After a file has been edited by
    hand past what the converter wrote, --recounted lists the counts
    instead of holding them to the mapping; names and results must still
    agree, and a case whose TUT test asserted anything must still assert;
  - the assertions outside every case (helpers, fixtures) number what the
    mapping's record of the file says those outside every TUT test became
    (catch2_outside_expected); under --recounted, listed, but none where
    TUT had some is still a problem. A mapping in its first form, a bare
    list of tests, has no file records: that is listed;
  - every assertion counted in a passing case's body ran: the run keeps its
    passing assertions (-s), and each CHECK, REQUIRE and _THAT site must
    report from at least one line it spans. A FAIL or SKIP that ran would
    have failed or skipped the case, and a case that failed or skipped may
    have stopped early, so neither is held to it; nor is one the build
    leaves out, which the file's compile command in the build's
    compile_commands.json, run as clang++ -E, says. Where there is no such
    command, one under an #if that never ran is listed. So is one --unrun
    names, reviewed as never running in a passing case on this host: in a
    loop that should not turn, or a branch of an if constexpr the build
    discards -- or, named for one run of the file, in a branch only the
    other runs take;

A test the mapping says TUT never ran (past its group's limit) or this host
does not compile is listed, not counted against parity, as is an assertion
outside every case (a helper's) that no case ran. So is what the Catch2
harness's state guard says a case left changed: TUT never looked, so it is
a finding about the test, not a difference the conversion made; the
table's exit column shows the run's exit code. It prints a table, one row a
file (and one a tier it is built at besides), and exits 1 on any mismatch,
listing each.

    python3 -I scripts/testing/tut_parity.py record --out /tmp/tut.json \\
        INTEGRATION_TEST_RUNNER_v3math PROJECT_llmath_TEST_v3color
    python3 -I scripts/testing/tut2catch.py --mapping /tmp/map.json indra/llmath/tests/v3*_test.cpp
    (build the Catch2 binary)
    python3 -I scripts/testing/tut_parity.py check --tut /tmp/tut.json --mapping /tmp/map.json
"""
import argparse
import bisect
import hashlib
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
# The x86-64 tiers a test is built for beside the tree's own, lowest first.
TIERS = ("baseline", "v2", "v3", "v4")
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


def isa_tier(name):
    """The x86-64 tier a TUT entry's or a Catch2 binary's name ends in
    (_baseline, _v2, _v3, _v4), it being built for that tier; None for one
    built at the tree's own."""
    m = re.search(r"_(" + "|".join(TIERS) + r")$", name)
    return m.group(1) if m else None


def tut_identity(name, command=()):
    """The test file a TUT CTest entry runs, by stem, and which of its runs
    the entry is: its ISA tier, or the name of a second run of the test's
    executable under another environment -- the entry is named for the
    executable's test, an underscore and the run, which the command, where
    given, tells from a test whose own name has an underscore. None for the
    test's own run."""
    m = re.search(r"TEST_RUNNER_(\w+)$", name) or re.search(r"_TEST_(\w+)$", name)
    if not m:
        return name, None
    test = m.group(1)
    tier = isa_tier(test)
    if tier:
        return test[:-len(tier) - 1] + "_test", tier
    for word in command:
        runs = re.search(r"_TEST_(\w+)$", os.path.splitext(os.path.basename(word))[0])
        if runs and test.startswith(runs.group(1) + "_"):
            return runs.group(1) + "_test", test[len(runs.group(1)) + 1:]
    return test + "_test", None


def executable_in(command, target):
    """The file in an entry's command named for its target, wherever a
    launcher put it; else the command's first word."""
    for word in command:
        if os.path.splitext(os.path.basename(word))[0] == target and os.path.isfile(word):
            return word
    return command[0] if command else None


def tut_executable(name, command):
    """The test executable in a TUT entry's command: INTEGRATION_TEST_<test>
    for INTEGRATION_TEST_RUNNER_<test>, and the entry's own name otherwise."""
    return executable_in(command, name.replace("INTEGRATION_TEST_RUNNER_", "INTEGRATION_TEST_", 1))


def tut_source(stem):
    """The TUT source a stem names, as git tracks it under ROOT, and the
    sha256 of its bytes: (None, None) unless exactly one
    indra/**/tests/<stem>.cpp is tracked."""
    try:
        listing = subprocess.run(["git", "-C", ROOT, "ls-files", "-z", "--", f":(glob)indra/**/tests/{stem}.cpp"],
                                 capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None, None
    paths = [p.decode("utf-8", "surrogateescape") for p in listing.split(b"\0") if p]
    if len(paths) != 1:
        return None, None
    with open(os.path.join(ROOT, paths[0]), "rb") as f:
        return paths[0], hashlib.sha256(f.read()).hexdigest()


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
        stem, variant = tut_identity(name, test["command"])
        tier = variant if variant in TIERS else None
        # What the results are of, so that check can tell they are of the
        # source the mapping was made from.
        source, digest = tut_source(stem)
        if source is None:
            print(f"{name}: not exactly one tracked indra/**/tests/{stem}.cpp, so no source hash kept",
                  file=sys.stderr)
        # An executable older than its source is not of it: its results
        # would stand for tests the file no longer has.
        executable = tut_executable(name, test["command"])
        if source is not None and executable and os.path.isfile(executable) and \
                os.path.getmtime(os.path.join(ROOT, source)) > os.path.getmtime(executable):
            print(f"{name}: {source} changed since {executable} was built; build it again to record it",
                  file=sys.stderr)
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
        # Each tier's run is kept apart: one the host cannot run exits 125
        # with no results, which must not stand for the others. So is each
        # second run, whose results are of its own environment.
        out[f"{stem}@{variant}" if variant else stem] = {"ctest": name, "stem": stem, "variant": variant,
                                                         "tier": tier, "exit": code, "tut_file": source,
                                                         "tut_sha256": digest, "results": results}
        counts = {}
        for r in results:
            counts[r["result"]] = counts.get(r["result"], 0) + 1
        print(f"{name}: exit {code}, " + (", ".join(f"{v} {k}" for k, v in sorted(counts.items())) or "no results"))
    if args.out:
        with open(args.out, "w") as f:
            json.dump(out, f, indent=1)
    return 0


# --------------------------------------------------------------------------
# check
# --------------------------------------------------------------------------
class LegalXml:
    """A Catch2 XML report as a file to parse. Catch2 writes a value it
    prints as XML, \\xNN for a control character or a byte that is not
    UTF-8, but lets through vertical tab, form feed and a UTF-8 surrogate,
    which XML forbids too: a check on bytes that hold one, as commonmisc's
    binary LLSD does, would leave its report unreadable. Those are read as
    Catch2 writes the rest."""

    FORBIDDEN = re.compile(rb"[\x0b\x0c]|\xed[\xa0-\xbf][\x80-\xbf]")

    def __init__(self, raw):
        self.raw = raw

    def read(self, size=-1):
        data = self.raw.read(size)
        # A surrogate's three bytes may straddle two reads; 0xED leads one
        # and is never a later byte of any.
        while b"\xed" in data[-2:]:
            more = self.raw.read(2)
            if not more:
                break
            data += more
        return self.FORBIDDEN.sub(lambda m: "".join(f"\\x{b:02X}" for b in m.group()).encode(), data)


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
            with open(report, "rb") as raw:
                for event, element in ElementTree.iterparse(LegalXml(raw), events=("start", "end")):
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
    and none failed, the skip code when every case skipped, and neither when
    any failed or none ran -- a filter that matches nothing exits 2. Where
    none failed, the harness exits 1 for the state guard's finding a change,
    which is listed, and for a teardown step's throwing, which is not: a
    check at static destruction ended a TUT process."""
    results = {c["result"] for c in cases}
    if "fail" in results or not results:
        return code not in (0, SKIP_CODE, "timeout")
    if code == 1 and "The state guard found" in output and "teardown step(s) threw" not in output:
        return True
    return code == (SKIP_CODE if results == {"skip"} else 0)


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
    left out. A site is (first line, last line, macro, the #if blocks that
    hold it), the lines its call spans; a case is {name, lines, sites, the
    #if blocks that hold it}. Returns (the cases, the sites outside every
    case)."""
    tc = converter()
    with open(path, "rb") as f:
        data = f.read()
    masked = tc.mask_source(data)
    dead = tc.dead_regions(masked)
    conditional = conditional_regions(masked)
    newlines = [m.start() for m in re.finditer(rb"\n", masked)]

    def live(at):
        return not any(start <= at < end for start, end in dead)

    def held(at):
        return frozenset(i for i, (start, end) in enumerate(conditional) if start <= at < end)

    def line(at):
        return bisect.bisect_left(newlines, at) + 1

    sites = []
    for m in tc.CATCH_ASSERTION.finditer(masked):
        if live(m.start()):
            close = matching(masked, m.end() - 1)
            macro = m.group(0).rstrip(b"( \t\r\n").decode()
            sites.append((m.start(), (line(m.start()), line(close), macro, held(m.start()))))
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
        cases.append({"name": name, "lines": (line(m.start()), line(brace)), "sites": [site for _, site in body],
                      "held": held(m.start())})
    return cases, [site for at, site in sites if at not in inside]


def conditional_regions(masked):
    """[start, end) of each #if block, but an include guard's: code a build
    may leave out."""
    regions = []
    stack = []
    for m in re.finditer(rb"^[ \t]*#[ \t]*(if|ifdef|ifndef|endif)\b([^\n]*)", masked, re.M):
        if m.group(1) != b"endif":
            name = m.group(2).strip()
            guard = m.group(1) == b"ifndef" and re.match(rb"\s*#[ \t]*define[ \t]+" + re.escape(name) + rb"\b",
                                                          masked[m.end():]) is not None
            stack.append((m.end(), guard))
        elif stack:
            start, guard = stack.pop()
            if not guard:
                regions.append((start, m.start()))
    return regions


class CompiledLines:
    """The lines of a file this build compiles: its compile command, from
    compile_commands.json as tut2catch.py reads it, run as clang++ -E. None
    for a file with no command to be had, or one that fails."""

    def __init__(self, build_dir, config):
        self.build_dir = build_dir
        self.config = config
        self.database = None
        self.loaded = False

    def __call__(self, path):
        tc = converter()
        # As the build names it, through any link on the way.
        path = os.path.realpath(path)
        if not self.loaded:
            self.loaded = True
            try:
                self.database = tc.CompileDatabase(self.build_dir, self.config)
            except tc.ConvertError:
                pass
        if self.database is None:
            return None
        try:
            directory, args, compiled = self.database.command(path)
            proc = subprocess.run(args + ["-E", compiled], cwd=directory, capture_output=True)
        except (tc.ConvertError, OSError):
            return None
        if proc.returncode != 0:
            return None
        kept = set()
        line = 0
        in_main = False
        for raw in proc.stdout.split(b"\n"):
            m = re.match(rb'^# (\d+) "((?:[^"\\]|\\.)*)"', raw)
            if m:
                line = int(m.group(1))
                named = os.path.join(directory, m.group(2).decode("utf-8", "replace"))
                in_main = os.path.basename(named) == os.path.basename(path) and os.path.realpath(named) == path
                continue
            if in_main and raw.strip():
                kept.add(line)
            line += 1
        return kept


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


def read_mapping(path):
    """tut2catch.py's mapping: its tests, and its records of the files by path
    -- None for a mapping in the first form it took, a bare list of tests."""
    with open(path) as f:
        mapping = json.load(f)
    if isinstance(mapping, list):
        return mapping, None
    return mapping["tests"], {record["file"]: record for record in mapping.get("files", [])}


def catch2_variant(binary, command):
    """Which run of a file a Catch2 entry under <binary>/ is: the ISA tier
    the binary's name ends in, or, where the name is the binary's own
    executable's, an underscore and more, a second run of it under another
    environment (al_add_test_binary's RERUN), that run's name. None for the
    file's own run."""
    tier = isa_tier(binary)
    if tier:
        return tier
    for word in command:
        target = os.path.splitext(os.path.basename(word))[0]
        if binary.startswith(target + "_") and os.path.isfile(word):
            return binary[len(target) + 1:]
    return None


def tree_tier(build_dir):
    """The ISA tier the build tree is built for: AL_ISA_TIER in its cache."""
    try:
        with open(os.path.join(build_dir, "CMakeCache.txt"), errors="replace") as f:
            m = re.search(r"^AL_ISA_TIER:\w+=(\S+)$", f.read(), re.M)
    except OSError:
        return None
    return m.group(1) if m else None


def check_run(args, label, path, entry_name, test, recorded, entries, file_cases, outside, kept, record, problems):
    """One Catch2 entry's run of a converted file against TUT's run of its
    original: appends what does not hold to problems, prints what is listed,
    and returns the table's row, or None where there is no run to show. kept
    is the file's lines this build compiles, or None where that is not
    known."""
    first_problem = len(problems)
    # A converted file changed since its binary was built is not what the
    # binary runs: its cases, lines and counts would not be the run's.
    binary = entry_name.split("/", 1)[0]
    variant = catch2_variant(binary, test["command"])
    if variant and variant not in TIERS:
        binary = binary[:-len(variant) - 1]
    executable = executable_in(test["command"], binary)
    if executable and os.path.isfile(executable) and \
            os.path.getmtime(os.path.join(ROOT, path)) > os.path.getmtime(executable):
        problems.append(f"{label}: changed since {executable} was built; build it again to check it")
        return None
    code, cases, output = catch2_results(test, args.timeout)
    # Where the host cannot run a tier, either side exits with the skip code
    # before running anything: TUT with no results, Catch2 with no XML.
    tut_skipped = recorded["exit"] == SKIP_CODE and not recorded["results"]
    catch2_skipped = code == SKIP_CODE and cases is None
    if tut_skipped and catch2_skipped:
        print(f"{label}: listed: TUT and Catch2 both skipped it (exit {SKIP_CODE}): the host does not run its tier")
        return label, 0, 0, 0, 0, 0, 0, code, "skipped"
    if tut_skipped or catch2_skipped:
        problems.append(f"{label}: TUT {'skipped' if tut_skipped else 'ran'} it (exit {recorded['exit']}), "
                        f"{entry_name} {'skipped' if catch2_skipped else 'ran'} it (exit {code})")
        return None
    if cases is None:
        problems.append(f"{label}: {entry_name} gave no XML (exit {code}):\n{output[-2000:]}")
        return None
    guard = [line for line in output.splitlines() if line.startswith("State guard:")]
    # The first case of a name is the one matched; a second is an error of
    # its own.
    by_name = {}
    for case in cases:
        if case["name"] in by_name:
            problems.append(f"{label}: Catch2 has more than one case named {case['name']!r}")
        else:
            by_name[case["name"]] = case
        for tag in FORGIVING_TAGS:
            if tag in case["tags"]:
                problems.append(f"{label}: {case['name']!r} is tagged {tag}, under which Catch2 passes it "
                                f"whatever it does")
    if not exit_fits(code, cases, output):
        counts = {}
        for case in cases:
            counts[case["result"]] = counts.get(case["result"], 0) + 1
        said = [line.strip() for line in output.splitlines()
                if "teardown step(s) threw" in line or "The state guard found" in line]
        problems.append(f"{label}: {entry_name} exited {code}, which does not fit its cases ("
                        + (", ".join(f"{v} {k}" for k, v in sorted(counts.items())) or "none") + ")"
                        + "".join(f"; it said: {line}" for line in said))
    tut_results = {(r["group"], r["n"]): r for r in recorded["results"]}
    # Counted in the body that ran: a test<N> in both branches of an #if is
    # two cases of one name in the file, and the run names the line of its.
    counted = {}
    for name, case in by_name.items():
        body = body_of(case, file_cases)
        counted[name] = len(body["sites"]) if body else None
    # An assertion counted is not one that ran: a case passes as well when a
    # loop around its CHECK never turns. Each site in a passing case's body
    # this build compiles must have reported at least once. Where that is
    # not known, one under an #if the case is not under (a case that ran was
    # compiled) is listed.
    def compiled(first, last, conditional):
        return any(n in kept for n in range(first, last + 1)) if kept is not None else \
            (None if conditional else True)

    # Listed for the run, apart from the tests listed.
    notes = []
    ran_anywhere = set()
    for case in cases:
        ran = ran_here(case, path)
        ran_anywhere |= ran
        if case["result"] != "ok":
            continue
        body = body_of(case, file_cases)
        for first, last, macro, blocks in body["sites"] if body else []:
            if macro in REPORTING_ONLY or any(first <= n <= last for n in ran):
                continue
            here = compiled(first, last, bool(blocks - body["held"]))
            if here:
                # Reviewed for this run of the file, or for every run.
                why = args.expect_unrun.get(label, {}).get(str(first)) or \
                    args.expect_unrun.get(path, {}).get(str(first))
                if why:
                    notes.append(f"{case['name']!r}: the {macro} on line {first} never ran, as reviewed: {why}")
                else:
                    problems.append(f"{label}: {case['name']!r} passed without running the {macro} on line {first}")
            elif here is None:
                notes.append(f"{case['name']!r}: the {macro} on line {first} never ran, and is under an #if no "
                             f"compile command here says this build compiles")
    notes += [f"the {macro} on line {first}, outside every case, never ran"
              for first, last, macro, blocks in outside
              if macro not in REPORTING_ONLY and not any(first <= n <= last for n in ran_anywhere)
              and compiled(first, last, bool(blocks)) is not False]
    # The TUT results must be of the source the mapping was made from.
    hashes = {"the recorded results": recorded.get("tut_sha256"), "the mapping": (record or {}).get("tut_sha256")}
    if all(hashes.values()):
        if len(set(hashes.values())) > 1:
            problems.append(f"{label}: TUT's results were recorded from another {os.path.basename(path)} than the "
                            f"one converted (sha256 {hashes['the recorded results'][:12]}, the mapping's "
                            f"{hashes['the mapping'][:12]})")
    else:
        notes.append(f"no TUT source hash in {' or '.join(k for k, v in hashes.items() if not v)}, "
                     f"so the two are not known to be of one source")
    matched = agree = counts_ok = 0
    listed = []
    seen_cases = set()
    paired = {}
    for e in entries:
        key = (e["group"], e["n"])
        name = e["catch2_name"]
        # Before taking TUT's result: a test<N> defined in both branches of
        # an #if is two entries, and the result is the active twin's.
        if e.get("not_compiled_here") or (e.get("past_group_limit") and key not in tut_results):
            listed.append(f"{e['group']} {e['n']}: " +
                          ("not compiled here" if e.get("not_compiled_here") else "past its group's limit"))
            if name in by_name:
                seen_cases.add(name)
            continue
        t = tut_results.pop(key, None)
        if t is None:
            problems.append(f"{label}: {e['group']} test<{e['n']}> did not run under TUT")
            continue
        if not name or name not in by_name:
            problems.append(f"{label}: {e['group']} test<{e['n']}> has no Catch2 case"
                            + (f" ({name!r})" if name else ""))
            continue
        seen_cases.add(name)
        paired[name] = key
        matched += 1
        if e.get("tut_name") is not None and e["tut_name"] != t["name"]:
            # TUT prints a test's name once its set_test_name has run: one
            # that skipped or failed before that printed none.
            if not t["name"] and t["result"] != "ok":
                listed.append(f"{e['group']} {e['n']}: ended before its set_test_name, so TUT printed no name")
            else:
                problems.append(f"{label}: {e['group']} test<{e['n']}>: TUT named it {t['name']!r}, "
                                f"the mapping {e['tut_name']!r}")
        if by_name[name]["result"] == t["result"]:
            agree += 1
        else:
            problems.append(f"{label}: {name!r}: TUT {t['said']!r}, Catch2 {by_name[name]['result']}")
        static = counted.get(name)
        # One TUT call is one Catch2 assertion, but for a && chain split into
        # one assertion a term, which the mapping counted.
        expected = e.get("catch2_expected", e["tut_assertions"])
        if expected == e["catch2_assertions"] == static:
            counts_ok += 1
        elif args.recounted and static == 0 and e["tut_assertions"] > 0:
            problems.append(f"{label}: {name!r}: {e['tut_assertions']} TUT assertion calls, none in the file")
        elif args.recounted and static is not None:
            counts_ok += 1
            print(f"{label}: listed: {name!r}: {expected} assertions converted, {static} in the file now")
        else:
            problems.append(f"{label}: {name!r}: {e['tut_assertions']} TUT assertion calls, "
                            f"{expected} Catch2 assertions they became, {e['catch2_assertions']} mapped, "
                            f"{static} in the file")
    for key, t in sorted(tut_results.items()):
        problems.append(f"{label}: TUT ran [{key[0]}, {key[1]}], which the mapping does not have")
    for name in sorted(set(by_name) - seen_cases):
        problems.append(f"{label}: Catch2 case {name!r} is no TUT test of the mapping")
    # TUT ran a file's groups by name and each group's tests by number, and a
    # test may lean on what the ones before it left.
    keys = set(paired.values())
    tut_order = [(r["group"], r["n"]) for r in recorded["results"] if (r["group"], r["n"]) in keys]
    catch2_order = [(paired[c["name"]], c["name"]) for c in cases if c["name"] in paired and by_name[c["name"]] is c]
    for at, (tut_key, (key, name)) in enumerate(zip(tut_order, catch2_order)):
        if tut_key != key:
            problems.append(f"{label}: Catch2 runs {name!r}, [{key[0]}, {key[1]}], as test {at + 1} of "
                            f"{len(tut_order)}, where TUT ran [{tut_key[0]}, {tut_key[1]}]: not TUT's order")
            break
    for line in listed + notes + guard:
        print(f"{label}: listed: {line}")
    return (label, len(recorded["results"]), len(cases), matched, agree, counts_ok, len(listed), code,
            "ok" if len(problems) == first_problem else "MISMATCH")


def check(args):
    mapping, file_records = read_mapping(args.mapping)
    args.expect_unrun = {}
    if args.unrun:
        with open(args.unrun) as f:
            args.expect_unrun = json.load(f)
    with open(args.tut) as f:
        tut = json.load(f)
    # By stem and run, which each record names; one recorded before records
    # did has its tier read from its CTest name: results recorded before they
    # were kept by tier, the last tier's under the stem, still say which tier
    # they are of.
    records = {}
    for key, recorded in tut.items():
        if "variant" in recorded:
            records[(recorded["stem"], recorded["variant"])] = recorded
        else:
            records[tut_identity(recorded["ctest"]) if recorded.get("ctest") else (key, None)] = recorded
    own_tier = tree_tier(args.build_dir)
    tests = ctest_tests(args.build_dir, args.config)
    compiled_lines = CompiledLines(args.build_dir, args.config)
    by_file = {}
    for entry in mapping:
        by_file.setdefault(entry["file"], []).append(entry)
    files = sorted(by_file) if not args.files else [os.path.relpath(os.path.abspath(f), ROOT) for f in args.files]

    rows = []
    problems = []
    for path in files:
        entries = by_file.get(path, [])
        stem = os.path.splitext(os.path.basename(path))[0]
        file_problems = []
        file_cases, outside = read_cases(os.path.join(ROOT, path))
        kept = compiled_lines(os.path.join(ROOT, path))
        record = (file_records or {}).get(path)
        # A file edited past what the converter wrote is held to the
        # converter's counts only once --recounted says it was looked at: an
        # edit that kept every count, a weakened assertion, shows nowhere
        # else.
        if record is not None:
            with open(os.path.join(ROOT, path), "rb") as f:
                written = hashlib.sha256(f.read()).hexdigest() == record.get("catch2_sha256")
            if not record.get("catch2_sha256"):
                print(f"{path}: listed: no hash in the mapping of the file the converter wrote")
            elif not written and args.recounted:
                print(f"{path}: listed: edited since the converter wrote it")
            elif not written:
                file_problems.append(f"{path}: edited since the converter wrote it; review the edits, then "
                                     f"check it with --recounted")
        # What no test's count holds: the assertions outside every test body
        # (helpers, fixtures), which the mapping totals for the file.
        if file_records is None:
            print(f"{path}: listed: no file totals in the mapping")
        elif record is None:
            file_problems.append(f"{path}: the mapping has no record of the file")
        elif len(outside) != record["catch2_outside_expected"]:
            if args.recounted and (outside or not record["tut_outside_calls"]):
                print(f"{path}: listed: {record['catch2_outside_expected']} assertions outside every case "
                      f"converted, {len(outside)} in the file now")
            else:
                file_problems.append(f"{path}: {record['tut_outside_calls']} TUT assertion calls outside every "
                                     f"test, {record['catch2_outside_expected']} Catch2 assertions they became, "
                                     f"{len(outside)} outside every case in the file")
        # The file's Catch2 entries, <binary>/<stem>, by the tier the binary
        # is built for: one at the tree's own, checked against TUT's
        # untiered entry (or, where there is none, its entry at the tree's
        # tier), and at most one, <binary>_<tier>, at each other tier,
        # checked against TUT's entry at that tier. A second run of the
        # binary, <binary>_<run>, is checked against TUT's run of that name.
        runs = {}
        for name in tests:
            if name.endswith("/" + stem):
                runs.setdefault(catch2_variant(name[:-len(stem) - 1], tests[name]["command"]), []).append(name)
        if len(runs.get(None, [])) != 1:
            file_problems.append(f"{path}: {len(runs.get(None, []))} Catch2 CTest entries end in /{stem}")
        used = set()
        file_rows = []
        for tier in sorted(runs, key=lambda t: (t in TIERS, TIERS.index(t) if t in TIERS else 0, t or "")):
            label = f"{path} ({tier})" if tier else path
            if len(runs[tier]) != 1:
                if tier:
                    file_problems.append(f"{label}: {len(runs[tier])} Catch2 CTest entries end in _{tier}/{stem}")
                continue
            key = (stem, tier)
            if tier is None and key not in records and (stem, own_tier) in records:
                key = (stem, own_tier)
            if key not in records:
                file_problems.append(f"{label}: no TUT results recorded for {stem}" + (f" at {tier}" if tier in TIERS else
                                                                                       f" in its {tier} run" if tier else ""))
                continue
            used.add(key)
            row = check_run(args, label, path, runs[tier][0], tests[runs[tier][0]], records[key], entries,
                            file_cases, outside, kept, record, problems)
            if row:
                file_rows.append(row)
        # TUT's runs no Catch2 entry was checked against: its run at the
        # tree's own tier is the untiered one again, where that was checked.
        for key in sorted((k for k in records if k[0] == stem and k not in used), key=lambda k: k[1] or ""):
            if key[1] and key[1] == own_tier and (stem, None) in used:
                print(f"{path}: listed: TUT's run at {own_tier}, the tree's own tier: {runs[None][0]} runs that "
                      f"tier, and was checked against TUT's untiered run")
            else:
                file_problems.append(f"{path}: TUT ran {stem} " + (f"at {key[1]}" if key[1] in TIERS else
                                                                     f"in its {key[1]} run" if key[1] else "untiered")
                                     + f" ({records[key]['ctest']}), and no Catch2 entry does")
        problems.extend(file_problems)
        for row in file_rows:
            rows.append(row[:-1] + ("MISMATCH",) if file_problems else row)

    header = ("file", "TUT", "Catch2", "matched", "agree", "counts", "listed", "exit", "parity")
    widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(len(header))]
    for r in [header] + rows:
        print("  ".join(str(v).ljust(w) if i == 0 else str(v).rjust(w) for i, (v, w) in enumerate(zip(r, widths))))
    for p in problems:
        print(f"mismatch: {p}")
    return 1 if problems else 0


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
    chk.add_argument("--unrun", help="JSON of assertion sites reviewed as never running in a passing case on "
                     "this host, {file: {line: why}}: each is listed rather than a mismatch. A key may name "
                     "one run of a file, as the table does (\"<file> (newsolver)\"), for a site only that run "
                     "leaves out")
    chk.add_argument("files", nargs="*", help="only these converted files (default: all the mapping has)")
    args = parser.parse_args()
    return record(args) if args.step == "record" else check(args)


if __name__ == "__main__":
    sys.exit(main())
