#!/usr/bin/env python3
"""What the tests cost a build tree: executables, bytes, build time and run time.

Reads a CMake Ninja build tree and changes nothing in it, so it can run
while the tree is building: the manifest (build-<config>.ninja, or
build.ninja, with what they include), .ninja_log, the CTestTestfile.cmake
files, Testing/Temporary/CTestCostData.txt and the sizes of files on disk.
It never builds or runs anything. For one configuration it reports:

  executables  every executable a CTest registration runs, and every
               executable target whose name matches --pattern
               (PROJECT_*_TEST_*, INTEGRATION_TEST_* and BENCH_* by
               default), registered or not. Their targets are the test
               targets. A target that only test targets link, such as the
               TUT runner library, is test support
  bytes        the executables, and their object directories
               (CMakeFiles/<target>.dir/<config>)
  job-seconds  test compiles, test links, test support, and production
               compiles and links, from the latest .ninja_log entry of each
               edge. An entry more than --stall-factor times the median of
               its kind, and longer than --stall-floor seconds, is a stall
               (a machine that slept mid-build) and counts as that median
  production   objects of test targets whose source is not a test file
  in tests     (under no tests/ directory, and not *_test.cpp or
               *_bench.cpp), each source found from the manifest; most are
               a second compile of a file a library builds as well
  ctest        the serial sum of CTestCostData.txt's averages over the
               registered tests not labelled benchmark, their median and
               the slowest
  the log      the build sessions the latest entries come from, so a reader
               can tell a clean build from an incremental one

Job-seconds are the sum of the edges' durations under whatever parallelism
the build ran with: a measure of work, not of wall time or of CPU time.

A ninja run is told apart from the others by when it started: every entry
holds its offset from the run's start and a timestamp, which recent ninja
takes at the edge's start and older ninja at its end. That still works on a
log ninja has recompacted, which keeps one entry per output in no order.

    python3 scripts/testing/test_cost_report.py
    python3 scripts/testing/test_cost_report.py build-Linux-ninja-os --config Release
    python3 scripts/testing/test_cost_report.py --pattern '_tests$' --json > cost.json
"""
import argparse
import datetime
import json
import os
import re
import statistics
import sys
from collections import Counter, defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_BUILD = "build-Darwin-ninja-os"
DEFAULT_PATTERN = r"^(PROJECT_.+_TEST_.+|INTEGRATION_TEST_.+|BENCH_.+)$"
TEST_SOURCE = re.compile(r"(^|/)tests/|_(test|bench)\.(c|cc|cpp|cxx|m|mm)$")
STALL_FACTOR = 20.0
STALL_FLOOR = 300.0
SESSION_GAP = 2.0
MIB = 1024.0 * 1024.0

STEP_RULE = re.compile(
    r"^[A-Za-z0-9]+_(COMPILER|EXECUTABLE_LINKER|STATIC_LIBRARY_LINKER|SHARED_LIBRARY_LINKER|MODULE_LIBRARY_LINKER)__(.+)$"
)
NINJA_TOKEN = re.compile(r"\$(.)|( +)|(:)|([^$ :]+)", re.S)
CATEGORIES = ("test", "support", "production")
STEPS = ("compile", "link")


# --------------------------------------------------------------------------
# The ninja manifest
# --------------------------------------------------------------------------
class Edge:
    __slots__ = ("outputs", "rule", "inputs", "implicit", "object_dir", "step", "linker", "target")

    def __init__(self, outputs, rule, inputs, implicit):
        self.outputs = outputs
        self.rule = rule
        self.inputs = inputs
        self.implicit = implicit
        self.object_dir = None
        self.step = None
        self.linker = None
        self.target = None


def ninja_words(text):
    """The words of a build line, unescaped; an unescaped ':' is None."""
    words, word = [], None
    for m in NINJA_TOKEN.finditer(text):
        escaped, _, colon, plain = m.groups()
        if escaped is not None:
            word = (word or "") + escaped
        elif plain is not None:
            word = (word or "") + plain
        else:
            if word is not None:
                words.append(word)
                word = None
            if colon:
                words.append(None)
    if word is not None:
        words.append(word)
    return words


def parse_build(line):
    words = ninja_words(line[len("build "):])
    if None not in words:
        return None
    colon = words.index(None)
    outputs = words[:colon]
    if "|" in outputs:
        outputs = outputs[: outputs.index("|")]
    rest = words[colon + 1:]
    if not outputs or not rest:
        return None
    inputs, implicit = [], []
    into = inputs
    for word in rest[1:]:
        if word == "|":
            into = implicit
        elif word in ("||", "|@"):
            break
        else:
            into.append(word)
    return Edge(outputs, rest[0], inputs, implicit)


def read_manifest(build_dir, config):
    """The edges of the manifest that builds config, following its includes."""
    top = os.path.join(build_dir, "build-%s.ninja" % config)
    if not os.path.isfile(top):
        top = os.path.join(build_dir, "build.ninja")
    edges, seen = [], set()

    def read(path):
        if path in seen or not os.path.isfile(path):
            return
        seen.add(path)
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
        edge, pending = None, ""
        for line in lines:
            if pending:
                line, pending = pending + line.lstrip(" "), ""
            if (len(line) - len(line.rstrip("$"))) % 2:
                pending = line[:-1]
                continue
            if line.startswith("build "):
                edge = parse_build(line)
                if edge is not None:
                    edges.append(edge)
            elif line.startswith("  "):
                if edge is not None and line.startswith("  OBJECT_DIR = "):
                    edge.object_dir = line[len("  OBJECT_DIR = "):].strip()
            else:
                edge = None
                m = re.match(r"(include|subninja)\s+(.+)$", line)
                if m:
                    read(os.path.join(build_dir, "".join(ninja_words(m.group(2).strip()))))

    read(top)
    return top, edges


def classify_steps(edges, config):
    """Mark each compile and link edge of config with its step and target.

    Returns those edges, and the edges that are neither phony nor any
    configuration's compile or link: custom commands, regeneration and the
    like.
    """
    steps, others = [], []
    suffix = "_" + config
    for edge in edges:
        if edge.rule == "phony":
            continue
        m = STEP_RULE.match(edge.rule)
        if not m:
            others.append(edge)
            continue
        rest = m.group(2)
        if not rest.endswith(suffix):
            continue
        target = rest[: -len(suffix)]
        if m.group(1) == "COMPILER":
            edge.step = "compile"
            if target.endswith("_unscanned"):
                target = target[: -len("_unscanned")]
        else:
            edge.step = "link"
            edge.linker = m.group(1)
        edge.target = target
        steps.append(edge)
    return steps, others


def support_targets(steps, tests):
    """Targets whose outputs only test targets, or other such targets, consume."""
    produced = {}
    for edge in steps:
        for out in edge.outputs:
            produced[out] = edge.target
    consumers = defaultdict(set)
    for edge in steps:
        for name in edge.inputs + edge.implicit:
            owner = produced.get(name)
            if owner is not None and owner != edge.target:
                consumers[owner].add(edge.target)
    support = set()
    changed = True
    while changed:
        changed = False
        for target, users in consumers.items():
            if target in tests or target in support:
                continue
            if users and all(u in tests or u in support for u in users):
                support.add(target)
                changed = True
    return support


# --------------------------------------------------------------------------
# .ninja_log
# --------------------------------------------------------------------------
def read_ninja_log(path):
    """The log's version and entries (start ms, end ms, mtime, output), in file order."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    m = re.match(r"# ninja log v(\d+)", lines[0])
    version = int(m.group(1)) if m else None
    entries = []
    # The piece after the last newline is empty, or a line ninja is still writing.
    for line in lines[1:-1]:
        parts = line.split("\t")
        if len(parts) < 5:
            continue
        try:
            entries.append((int(parts[0]), int(parts[1]), int(parts[2]), parts[3]))
        except ValueError:
            continue
    return version, entries


def wall_seconds(mtime):
    """A log mtime as seconds since 1970, or None.

    POSIX ninja records nanoseconds since 1970; Windows ninja 100 ns ticks
    since 2001; very old ninja seconds.
    """
    if mtime >= 10**17:
        seconds = mtime / 1e9
    elif mtime >= 10**14:
        seconds = mtime / 1e7 + 978307200
    elif mtime >= 10**8:
        seconds = float(mtime)
    else:
        return None
    return seconds if 946684800 <= seconds <= 4102444800 else None


def runs_by_start(entries, field):
    """Sessions from each entry's timestamp less its start (field 0) or end (field 1) offset."""
    keyed = []
    for i, entry in enumerate(entries):
        wall = wall_seconds(entry[2])
        if wall is not None:
            keyed.append((wall - entry[field] / 1000.0, i))
    keyed.sort()
    sessions, previous = [], None
    for start, i in keyed:
        if previous is None or start - previous > SESSION_GAP:
            sessions.append({"start": start, "entries": []})
        sessions[-1]["entries"].append(i)
        previous = start
    return sessions


def find_sessions(entries):
    """Group the entries into ninja runs.

    Returns (session of each entry index, sessions). A session is a dict
    with its start (seconds since 1970, or None) and its entry indices.
    Which offset the timestamp goes with depends on ninja's version; the
    right one puts a run's entries together, so it is the one that finds
    fewer runs.
    """
    if any(wall_seconds(entry[2]) is not None for entry in entries):
        sessions = min((runs_by_start(entries, field) for field in (0, 1)), key=len)
    else:
        # No usable timestamps: runs in file order, which a recompacted log breaks up.
        sessions, previous = [], None
        for i, entry in enumerate(entries):
            if previous is None or entry[1] < previous:
                sessions.append({"start": None, "entries": []})
            sessions[-1]["entries"].append(i)
            previous = entry[1]
    session_of = {}
    for n, session in enumerate(sessions):
        for i in session["entries"]:
            session_of[i] = n
    return session_of, sessions


# --------------------------------------------------------------------------
# CTest
# --------------------------------------------------------------------------
CMAKE_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
CMAKE_BRACKET = re.compile(r"\[(=*)\[")
CMAKE_QUOTED = re.compile(r'"((?:[^"\\]|\\.)*)"', re.S)
CMAKE_ESCAPE = re.compile(r"\\(.)", re.S)
CMAKE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", ";": "\\;"}


def cmake_commands(text):
    """(name, arguments) for each command of a generated CMake script."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if c == "#":
            i = text.find("\n", i)
            i = n if i < 0 else i
            continue
        m = CMAKE_NAME.match(text, i)
        if not m:
            i += 1
            continue
        name, i = m.group(0).lower(), m.end()
        while i < n and text[i] in " \t":
            i += 1
        if i >= n or text[i] != "(":
            continue
        i += 1
        args, depth = [], 0
        while i < n:
            c = text[i]
            if c.isspace():
                i += 1
            elif c == ")":
                i += 1
                if depth == 0:
                    break
                depth -= 1
            elif c == "(":
                depth += 1
                i += 1
            elif c == '"':
                quoted = CMAKE_QUOTED.match(text, i)
                if not quoted:
                    break
                args.append(CMAKE_ESCAPE.sub(lambda e: CMAKE_ESCAPES.get(e.group(1), e.group(1)), quoted.group(1)))
                i = quoted.end()
            elif CMAKE_BRACKET.match(text, i):
                opener = CMAKE_BRACKET.match(text, i)
                close = "]" + opener.group(1) + "]"
                end = text.find(close, opener.end())
                end = n if end < 0 else end
                args.append(text[opener.end():end])
                i = end + len(close)
            else:
                start = i
                while i < n and not text[i].isspace() and text[i] not in '()"':
                    i += 1
                args.append(text[start:i])
        yield name, args


def condition_holds(args, config):
    if len(args) == 3 and args[0] == "CTEST_CONFIGURATION_TYPE" and args[1] == "MATCHES":
        return re.search(args[2], config) is not None
    if len(args) == 2 and args[0] == "EXISTS":
        return os.path.exists(args[1])
    return True


def read_ctest(build_dir, config):
    """The tests CTest would run for config, from the CTestTestfile.cmake tree.

    Each is a dict: name, command, labels, disabled.
    """
    tests, order, seen = {}, [], set()

    def read(path):
        path = os.path.realpath(path)
        if path in seen or not os.path.isfile(path):
            return
        seen.add(path)
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        here = os.path.dirname(path)
        # Each frame of an if: was the enclosing branch live, and has a branch of this if been taken.
        stack = []
        live = True
        for name, args in cmake_commands(text):
            if name == "if":
                holds = live and condition_holds(args, config)
                stack.append((live, holds))
                live = holds
            elif name == "elseif":
                parent, taken = stack[-1] if stack else (True, True)
                holds = parent and not taken and condition_holds(args, config)
                stack[-1] = (parent, taken or holds)
                live = holds
            elif name == "else":
                parent, taken = stack[-1] if stack else (True, True)
                live = parent and not taken
                stack[-1] = (parent, True)
            elif name == "endif":
                live = stack.pop()[0] if stack else True
            elif not live:
                continue
            elif name == "add_test" and len(args) >= 2:
                if args[1] == "NOT_AVAILABLE":
                    continue
                if args[0] not in tests:
                    order.append(args[0])
                tests[args[0]] = {"name": args[0], "command": args[1:], "labels": [], "disabled": False}
            elif name == "set_tests_properties" and "PROPERTIES" in args:
                at = args.index("PROPERTIES")
                props = dict(zip(args[at + 1::2], args[at + 2::2]))
                for test in args[:at]:
                    if test in tests:
                        if "LABELS" in props:
                            tests[test]["labels"] = [x for x in props["LABELS"].split(";") if x]
                        if "DISABLED" in props:
                            tests[test]["disabled"] = props["DISABLED"].upper() in ("1", "ON", "TRUE", "YES", "Y")
            elif name == "subdirs":
                for sub in args:
                    read(os.path.join(sub if os.path.isabs(sub) else os.path.join(here, sub), "CTestTestfile.cmake"))
            elif name == "include" and args:
                read(args[0] if os.path.isabs(args[0]) else os.path.join(here, args[0]))

    read(os.path.join(build_dir, "CTestTestfile.cmake"))
    return [tests[name] for name in order]


def read_cost_data(path):
    """CTestCostData.txt: {test: (runs, average seconds)}, and the tests that failed last."""
    costs, failed = {}, []
    if not os.path.isfile(path):
        return None, []
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    into_failed = False
    for line in lines:
        line = line.strip()
        if not line:
            continue
        if line == "---":
            if into_failed:
                break
            into_failed = True
            continue
        if into_failed:
            failed.append(line)
            continue
        parts = line.rsplit(None, 2)
        if len(parts) == 3:
            try:
                costs[parts[0]] = (int(parts[1]), float(parts[2]))
            except ValueError:
                pass
    return costs, failed


# --------------------------------------------------------------------------
# The report
# --------------------------------------------------------------------------
def real(path):
    """A path as the file system knows it, for comparing paths."""
    return os.path.normcase(os.path.realpath(path))


def cache_value(build_dir, key):
    path = os.path.join(build_dir, "CMakeCache.txt")
    if os.path.isfile(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith(key + ":"):
                    return line.split("=", 1)[1].rstrip("\n")
    return None


def tree_bytes(path):
    total = 0
    for root, _, files in os.walk(path):
        for name in files:
            try:
                total += os.lstat(os.path.join(root, name)).st_size
            except OSError:
                pass
    return total


def iso(seconds):
    if seconds is None:
        return None
    return datetime.datetime.fromtimestamp(seconds).strftime("%Y-%m-%d %H:%M:%S")


def object_dir(edge_list, target, config):
    """A target's object directory, from its link edge or from one of its objects."""
    for edge in edge_list:
        if edge.object_dir:
            return edge.object_dir
    marker = "CMakeFiles/%s.dir/" % target
    for edge in edge_list:
        at = edge.outputs[0].find(marker)
        if at >= 0:
            base = edge.outputs[0][: at + len(marker)]
            return base + config if edge.outputs[0][at + len(marker):].startswith(config + "/") else base.rstrip("/")
    return None


class Tree:
    """One configuration of a build tree: its edges, its tests and its log."""

    def __init__(self, build_dir, config, pattern, stall_factor, stall_floor):
        self.build_dir = build_dir
        self.config = config
        self.stall_factor = stall_factor
        self.stall_floor = stall_floor
        self.real_build = real(build_dir)
        self.source_root = cache_value(build_dir, "CMAKE_HOME_DIRECTORY") or os.path.join(ROOT, "indra")

        manifest, edges = read_manifest(build_dir, config)
        self.steps, self.others = classify_steps(edges, config)
        if not self.steps:
            raise ValueError("%s has no %s compile or link edges" % (manifest, config))
        self.manifest = os.path.relpath(manifest, build_dir)
        self.by_target = defaultdict(list)
        self.executables = {}
        for edge in self.steps:
            self.by_target[edge.target].append(edge)
            if edge.linker == "EXECUTABLE_LINKER":
                self.executables[real(os.path.join(build_dir, edge.outputs[0]))] = edge.target

        # The test targets: what CTest runs, and what the name pattern picks.
        self.registrations = read_ctest(build_dir, config)
        self.registered = set()
        self.unresolved = 0
        for test in self.registrations:
            found = [self.executables[real(a)] for a in test["command"] if os.path.isabs(a) and real(a) in self.executables]
            self.registered.update(found)
            self.unresolved += not found
        self.named = {t for t in self.executables.values() if pattern.search(t)}
        self.tests = self.registered | self.named
        self.support = support_targets(self.steps, self.tests)

        # The latest log entry of each edge, and the medians stalls are measured against.
        self.log_path = os.path.join(build_dir, ".ninja_log")
        self.log_version, self.entries = read_ninja_log(self.log_path)
        self.latest = {}
        for i, entry in enumerate(self.entries):
            self.latest[entry[3]] = i
        self.session_of, self.sessions = find_sessions(self.entries)
        self.logged = {}
        self.durations = defaultdict(list)
        for edge in self.steps:
            i = self.entry_of(edge)
            if i is not None:
                self.logged[id(edge)] = i
                self.durations[self.kind(edge)].append(self.duration(i))
        self.medians = {kind: statistics.median(values) for kind, values in self.durations.items()}
        self.stalled = []
        self.costs = {}

    def category(self, target):
        return "test" if target in self.tests else "support" if target in self.support else "production"

    def kind(self, edge):
        return self.category(edge.target), edge.step

    def entry_of(self, edge):
        for out in edge.outputs:
            if out in self.latest:
                return self.latest[out]
        return None

    def duration(self, i):
        return (self.entries[i][1] - self.entries[i][0]) / 1000.0

    def cost(self, edge):
        """The edge's job-seconds, a stall counted as its kind's median; None if never logged."""
        if id(edge) not in self.costs:
            i = self.logged.get(id(edge))
            value = None if i is None else self.duration(i)
            if value is not None:
                median = self.medians[self.kind(edge)]
                if value > self.stall_floor and value > self.stall_factor * median:
                    self.stalled.append({"output": self.entries[i][3], "kind": "%s %s" % self.kind(edge),
                                         "seconds": value, "counted": median,
                                         "when": iso(wall_seconds(self.entries[i][2]))})
                    value = median
            self.costs[id(edge)] = value
        return self.costs[id(edge)]

    def source_of(self, edge):
        src = edge.inputs[0] if edge.inputs else ""
        return os.path.normpath(src if os.path.isabs(src) else os.path.join(self.build_dir, src))

    def relative(self, path):
        """A source as the report shows it: from the source root when it is under it."""
        rel = os.path.relpath(path, self.source_root)
        return path if rel.startswith("..") else rel.replace(os.sep, "/")

    def is_generated(self, path):
        path = real(path)
        return path == self.real_build or path.startswith(self.real_build + os.sep)

    def executables_report(self):
        prefixes = Counter()
        for target in self.tests:
            prefixes["PROJECT" if target.startswith("PROJECT_") else "INTEGRATION" if target.startswith("INTEGRATION_TEST_")
                     else "BENCH" if target.startswith("BENCH_") else "other"] += 1
        return {
            "count": len(self.tests),
            "registered": len(self.registered),
            "named_only": len(self.named - self.registered),
            "registered_unnamed": sorted(self.registered - self.named),
            "by_prefix": dict(prefixes),
            "registrations": len(self.registrations),
            "registrations_without_executable": self.unresolved,
            "support_targets": sorted(self.support),
        }

    def bytes_report(self):
        sizes, missing, objects = [], 0, 0
        for target in sorted(self.tests):
            link = [e for e in self.by_target[target] if e.step == "link"]
            path = os.path.join(self.build_dir, link[0].outputs[0])
            if os.path.isfile(path):
                sizes.append(os.path.getsize(path))
            else:
                missing += 1
            folder = object_dir(self.by_target[target], target, self.config)
            if folder and os.path.isdir(os.path.join(self.build_dir, folder)):
                objects += tree_bytes(os.path.join(self.build_dir, folder))
        return {
            "executables": sum(sizes),
            "executables_present": len(sizes),
            "executables_missing": missing,
            "executable_median": statistics.median(sizes) if sizes else 0,
            "executable_max": max(sizes) if sizes else 0,
            "object_dirs": objects,
        }

    def job_seconds_report(self):
        rows, totals = [], defaultdict(float)
        for category in CATEGORIES:
            for step in STEPS:
                edges = [e for e in self.steps if self.kind(e) == (category, step)]
                counted = [c for c in (self.cost(e) for e in edges) if c is not None]
                rows.append({
                    "category": category, "step": step, "edges": len(edges), "logged": len(counted),
                    "job_seconds": sum(counted), "raw_job_seconds": sum(self.durations.get((category, step), [])),
                    "median": self.medians.get((category, step), 0.0),
                })
                totals[category] += sum(counted)
        whole = sum(totals.values())
        other = [self.entry_of(e) for e in self.others]
        return {
            "rows": rows,
            "total": whole,
            "test_share": totals["test"] / whole if whole else 0.0,
            "test_and_support_share": (totals["test"] + totals["support"]) / whole if whole else 0.0,
            "stall_factor": self.stall_factor,
            "stall_floor": self.stall_floor,
            "stalled": sorted(self.stalled, key=lambda s: -s["seconds"]),
            "other_steps": {"edges": len(self.others),
                            "job_seconds": sum(self.duration(i) for i in other if i is not None)},
        }

    def production_report(self, top):
        """Objects of test targets compiled from sources that are not test files.

        Test support's objects count apart, and only where a production
        target compiles the same file.
        """
        compiled_by = defaultdict(set)
        for edge in self.steps:
            if edge.step == "compile":
                compiled_by[self.source_of(edge)].add(self.category(edge.target))
        in_tests = defaultdict(lambda: {"objects": 0, "logged": 0, "job_seconds": 0.0})
        in_support = defaultdict(lambda: {"objects": 0, "logged": 0, "job_seconds": 0.0})
        for edge in self.steps:
            category = self.category(edge.target)
            if edge.step != "compile" or category == "production":
                continue
            src = self.source_of(edge)
            if self.is_generated(src) or TEST_SOURCE.search(self.relative(src)):
                continue
            if category == "support" and "production" not in compiled_by[src]:
                continue
            row = (in_tests if category == "test" else in_support)[src]
            row["objects"] += 1
            if self.cost(edge) is not None:
                row["logged"] += 1
                row["job_seconds"] += self.cost(edge)
        ranked = sorted(in_tests.items(), key=lambda kv: (-kv[1]["objects"], -kv[1]["job_seconds"], kv[0]))
        duplicates = Counter(r["objects"] for r in in_tests.values())
        return {
            "objects": sum(r["objects"] for r in in_tests.values()),
            "logged": sum(r["logged"] for r in in_tests.values()),
            "files": len(in_tests),
            "job_seconds": sum(r["job_seconds"] for r in in_tests.values()),
            "files_also_in_production": sum(1 for src in in_tests if "production" in compiled_by[src]),
            "duplicates": {str(k): v for k, v in sorted(duplicates.items(), reverse=True)},
            "top": [{"file": self.relative(src), "objects": r["objects"], "job_seconds": r["job_seconds"],
                     "also_in_production": "production" in compiled_by[src]} for src, r in ranked[:top]],
            "support_recompiles": {
                "objects": sum(r["objects"] for r in in_support.values()),
                "files": len(in_support),
                "job_seconds": sum(r["job_seconds"] for r in in_support.values()),
            },
        }

    def log_report(self):
        """The runs the latest entries come from: a clean build is one run holding nearly all of them."""
        by_run = defaultdict(Counter)
        for edge in self.steps:
            i = self.logged.get(id(edge))
            if i is not None:
                by_run[self.session_of.get(i)]["%s %s" % self.kind(edge)] += 1

        def describe(run):
            members = [self.entries[i] for i in self.sessions[run]["entries"]]
            return {
                "started": iso(self.sessions[run]["start"]),
                "wall_seconds": (max(e[1] for e in members) - min(e[0] for e in members)) / 1000.0,
                "outputs": len(members),
                "config_edges": sum(by_run[run].values()),
                "config_edges_by_kind": dict(by_run[run]),
            }

        runs = [run for run in range(len(self.sessions)) if by_run[run]]
        last = max(runs, key=lambda run: (self.sessions[run]["start"] or 0, run)) if runs else None
        largest = max(runs, key=lambda run: sum(by_run[run].values())) if runs else None
        walls = [w for w in (wall_seconds(self.entries[i][2]) for i in self.logged.values()) if w is not None]
        return {
            "path": self.log_path,
            "version": self.log_version,
            "entries": len(self.entries),
            "outputs": len(self.latest),
            "sessions": len(self.sessions),
            "sessions_timed": bool(self.sessions) and self.sessions[0]["start"] is not None,
            "latest": describe(len(self.sessions) - 1) if self.sessions else None,
            "latest_with_config": describe(last) if last is not None else None,
            "largest": describe(largest) if largest is not None else None,
            "config_edges": len(self.steps),
            "config_edges_logged": len(self.logged),
            "config_sessions": len(runs),
            "config_oldest": iso(min(walls)) if walls else None,
            "config_newest": iso(max(walls)) if walls else None,
        }

    def ctest_report(self, top):
        costs, failed = read_cost_data(os.path.join(self.build_dir, "Testing", "Temporary", "CTestCostData.txt"))
        report = {"cost_data": costs is not None, "registrations": len(self.registrations)}
        if costs is None:
            return report
        if self.registrations:
            names = {t["name"] for t in self.registrations}
            bench = [t for t in self.registrations if "benchmark" in t["labels"]]
            disabled = [t for t in self.registrations if t["disabled"] and "benchmark" not in t["labels"]]
            counted = [t["name"] for t in self.registrations if "benchmark" not in t["labels"] and not t["disabled"]]
            report.update({"benchmarks": len(bench), "disabled": len(disabled), "labels_known": True,
                           "stale": sorted(name for name in costs if name not in names)})
        else:
            counted = sorted(costs)
            report.update({"benchmarks": 0, "disabled": 0, "labels_known": False, "stale": []})
        timed = sorted(((costs[name][1], name) for name in counted if name in costs), reverse=True)
        averages = [seconds for seconds, _ in timed]
        report.update({
            "counted": len(timed),
            "without_cost_data": sorted(name for name in counted if name not in costs),
            "serial_seconds": sum(averages),
            "median": statistics.median(averages) if averages else 0.0,
            "slowest": [{"test": name, "seconds": seconds} for seconds, name in timed[:top]],
            "failed_last": failed,
        })
        return report


def build_report(build_dir, config, pattern, stall_factor, stall_floor, top):
    tree = Tree(build_dir, config, pattern, stall_factor, stall_floor)
    return {
        "build_dir": build_dir,
        "config": config,
        "generator": cache_value(build_dir, "CMAKE_GENERATOR"),
        "source_root": tree.source_root,
        "manifest": tree.manifest,
        "log": tree.log_report(),
        "executables": tree.executables_report(),
        "bytes": tree.bytes_report(),
        "job_seconds": tree.job_seconds_report(),
        "production_in_tests": tree.production_report(top),
        "ctest": tree.ctest_report(top),
    }


# --------------------------------------------------------------------------
# Plain text
# --------------------------------------------------------------------------
def plural(count, word, words=None):
    """'1 file', '2 files'."""
    return "%s %s" % (f"{count:,}", word if count == 1 else words or word + "s")


def mib(size):
    return f"{size / MIB:,.0f} MiB" if size >= 1000 * MIB else "%.1f MiB" % (size / MIB)


def print_report(r):
    out = []
    say = out.append
    exe, size, jobs, prod, log, ctest = (r["executables"], r["bytes"], r["job_seconds"], r["production_in_tests"],
                                         r["log"], r["ctest"])
    shown = os.path.relpath(r["build_dir"], ROOT)
    shown = r["build_dir"] if shown.startswith("..") else shown
    say("Test cost: %s, %s (%s)" % (shown, r["config"], r["generator"] or "unknown generator"))

    say("")
    say("What the log covers")
    say("  .ninja_log v%s: %s for %s, from %s%s" % (
        log["version"], plural(log["entries"], "entry", "entries"), plural(log["outputs"], "output"),
        plural(log["sessions"], "ninja run"),
        "" if log["sessions_timed"] else " (no timestamps: runs split in file order, unreliable after a recompaction)"))

    def session_line(label, s):
        say("  %s: started %s, %s s wall, %s logged" % (
            label, s["started"] or "at an unknown time", f"{s['wall_seconds']:,.1f}", plural(s["outputs"], "output")))
        kinds = s["config_edges_by_kind"]
        if kinds:
            say("    the latest entry of %s: %s" % (
                plural(s["config_edges"], r["config"] + " edge"),
                ", ".join("%s %s" % (f"{v:,}", k) for k, v in sorted(kinds.items()))))

    if log["latest"]:
        session_line("latest run", log["latest"])
    if log["latest_with_config"] and log["latest_with_config"] != log["latest"]:
        session_line("latest run with %s edges" % r["config"], log["latest_with_config"])
    if log["largest"] and log["largest"] != log["latest_with_config"]:
        session_line("largest %s run" % r["config"], log["largest"])
    say("  %s %s compile and link edges: %s logged, last built in %s%s" % (
        f"{log['config_edges']:,}", r["config"], f"{log['config_edges_logged']:,}", plural(log["config_sessions"], "run"),
        " from %s to %s" % (log["config_oldest"], log["config_newest"]) if log["config_oldest"] else ""))

    say("")
    say("Test executables")
    prefixes = ", ".join("%s %d" % (k, v) for k, v in sorted(exe["by_prefix"].items()))
    say("  %s (%s)" % (plural(exe["count"], "executable"), prefixes))
    say("  %s CTest runs, from %s; %s by name only" % (
        f"{exe['registered']:,}", plural(exe["registrations"], "registration"), f"{exe['named_only']:,}"))
    unnamed = exe["registered_unnamed"]
    if unnamed:
        say("  registered, outside the name pattern: %s%s" % (
            ", ".join(unnamed[:10]), " and %d more" % (len(unnamed) - 10) if len(unnamed) > 10 else ""))
    if exe["registrations_without_executable"]:
        say("  %s no executable of this tree (scripts and the like)" % (
            plural(exe["registrations_without_executable"], "registration runs", "registrations run")))
    if exe["support_targets"]:
        say("  test support (only tests link it): %s" % ", ".join(exe["support_targets"]))

    say("")
    say("Bytes")
    say("  executables  %s in %s (median %s, largest %s)%s" % (
        mib(size["executables"]), plural(size["executables_present"], "file"), mib(size["executable_median"]),
        mib(size["executable_max"]),
        "; %s not on disk" % f"{size['executables_missing']:,}" if size["executables_missing"] else ""))
    say("  object dirs  %s" % mib(size["object_dirs"]))

    say("")
    say("Job-seconds (latest entry per edge)")
    say("  %-20s %7s %7s %10s %8s" % ("", "edges", "logged", "job-s", "median"))
    for row in jobs["rows"]:
        say("  %-20s %7s %7s %10s %8.2f" % (
            "%s %s" % (row["category"], row["step"]), f"{row['edges']:,}", f"{row['logged']:,}",
            f"{row['job_seconds']:,.0f}", row["median"]))
    say("  %-20s %7s %7s %10s" % ("total", "", "", f"{jobs['total']:,.0f}"))
    say("  tests are %.1f%% of it, %.1f%% with test support" % (
        100 * jobs["test_share"], 100 * jobs["test_and_support_share"]))
    say("  not counted: %s (custom commands, regeneration), %s job-s" % (
        plural(jobs["other_steps"]["edges"], "other edge"), f"{jobs['other_steps']['job_seconds']:,.0f}"))
    if jobs["stalled"]:
        say("  %s (over %gx their kind's median and %g s), counted as the median:" % (
            plural(len(jobs["stalled"]), "stalled entry", "stalled entries"), jobs["stall_factor"], jobs["stall_floor"]))
        for s in jobs["stalled"]:
            say("    %8s s -> %5.1f s  %s  (%s, %s)" % (
                f"{s['seconds']:,.0f}", s["counted"], s["output"], s["kind"], s["when"] or "time unknown"))
    else:
        say("  no stalled entries")

    say("")
    say("Production sources compiled into tests")
    say("  %s from %s, %s job-s%s; production targets compile %s of the files as well" % (
        plural(prod["objects"], "object"), plural(prod["files"], "file"), f"{prod['job_seconds']:,.0f}",
        "" if prod["logged"] == prod["objects"] else " (%s logged)" % f"{prod['logged']:,}",
        f"{prod['files_also_in_production']:,}"))
    if prod["duplicates"]:
        say("  test targets compiling each: %s" % ", ".join(
            "%sx %s" % (k, plural(v, "file")) for k, v in prod["duplicates"].items()))
    for row in prod["top"]:
        say("    %2dx %7.1f s  %s%s" % (row["objects"], row["job_seconds"], row["file"],
                                       "" if row["also_in_production"] else "  (no production target)"))
    sup = prod["support_recompiles"]
    if sup["objects"]:
        say("  test support compiles %s more objects, from %s that production targets compile too, %s job-s" % (
            f"{sup['objects']:,}", plural(sup["files"], "file"), f"{sup['job_seconds']:,.0f}"))

    say("")
    say("CTest")
    if not ctest["cost_data"]:
        say("  no Testing/Temporary/CTestCostData.txt: ctest has not run in this tree")
    else:
        say("  serial sum %s s over %s (median %.3f s)%s" % (
            f"{ctest['serial_seconds']:,.1f}", plural(ctest["counted"], "test"), ctest["median"],
            "" if ctest["labels_known"] else "; no registrations, so benchmarks count too"))
        say("  left out: %s labelled benchmark, %s disabled, %s without cost data; %s ignored" % (
            f"{ctest['benchmarks']:,}", f"{ctest['disabled']:,}", f"{len(ctest['without_cost_data']):,}",
            plural(len(ctest["stale"]), "stale name")))
        for row in ctest["slowest"]:
            say("    %7.2f s  %s" % (row["seconds"], row["test"]))
        if ctest["failed_last"]:
            say("  failed last run: %s" % ", ".join(ctest["failed_last"]))
    print("\n".join(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("build_dir", nargs="?", default=os.path.join(ROOT, DEFAULT_BUILD),
                    help="a CMake Ninja build tree (default: %s at the repository root)" % DEFAULT_BUILD)
    ap.add_argument("--config", default="RelWithDebInfo", help="the configuration (default: RelWithDebInfo)")
    ap.add_argument("--pattern", default=DEFAULT_PATTERN,
                    help="a regex for test target names, added to what CTest runs (default: %(default)s)")
    ap.add_argument("--stall-factor", type=float, default=STALL_FACTOR,
                    help="an entry this many times its kind's median is a stall (default: %(default)g)")
    ap.add_argument("--stall-floor", type=float, default=STALL_FLOOR,
                    help="and longer than this many seconds (default: %(default)g)")
    ap.add_argument("--top", type=int, default=10, help="rows in each list (default: %(default)d)")
    ap.add_argument("--json", action="store_true", help="print JSON instead of text")
    args = ap.parse_args()

    if not os.path.isfile(os.path.join(args.build_dir, ".ninja_log")):
        print("%s has no .ninja_log: not a Ninja build tree, or never built" % args.build_dir, file=sys.stderr)
        return 1
    try:
        report = build_report(args.build_dir, args.config, re.compile(args.pattern), args.stall_factor,
                              args.stall_floor, args.top)
    except (OSError, ValueError, re.error) as error:
        print("test_cost_report: %s" % error, file=sys.stderr)
        return 1
    if args.json:
        json.dump(report, sys.stdout, indent=1, sort_keys=False)
        print()
    else:
        print_report(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
