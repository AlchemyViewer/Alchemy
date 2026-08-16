#!/usr/bin/env python3
"""\
@file bench.py
@brief Run, query, and compare Google Benchmark results.

$LicenseInfo:firstyear=2026&license=viewerlgpl$
Radia Viewer Source Code
Copyright (C) 2026, Hymenaei

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA

Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
$/LicenseInfo$
"""

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


_COLORS = {
    "bold": "\033[1m",
    "cyan": "\033[36m",
    "green": "\033[32m",
    "red": "\033[31m",
    "yellow": "\033[33m",
}
_RESET = "\033[0m"


def _use_color() -> bool:
    if os.environ.get("NO_COLOR") is not None:
        return False
    return sys.stdout.isatty()


def _paint(text: str, color: str, enabled: bool) -> str:
    if not enabled:
        return text
    return f"{_COLORS[color]}{text}{_RESET}"


def _repository_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _benchmark_executable_directories(build_dir: Path, config: str) -> list[Path]:
    staging_dir = build_dir / "sharedlibs" / config
    if sys.platform.startswith("linux"):
        return [staging_dir / "bin"]
    return [staging_dir]


def _benchmark_library_directories(build_dir: Path, config: str) -> list[Path]:
    staging_dir = build_dir / "sharedlibs" / config
    if os.name == "nt":
        return [staging_dir, staging_dir / "Release"]
    if sys.platform == "darwin":
        return [staging_dir / "Frameworks", staging_dir / "Release" / "Frameworks", Path("/usr/lib")]
    return [staging_dir / "lib", Path("/usr/lib")]


def _benchmark_path(benchmark_dir: Path, name: str) -> Path:
    path = benchmark_dir / name
    if os.name == "nt" and path.suffix.lower() != ".exe":
        path = path.with_suffix(".exe")
    return path


def _is_benchmark_executable(path: Path) -> bool:
    if not path.is_file() or not path.stem.endswith("_benchmarks"):
        return False
    return os.name != "nt" or path.suffix.lower() == ".exe"


def _find_benchmarks(
    build_dir: Path,
    config: str,
    names: list[str],
    run_all: bool,
) -> list[Path]:
    directories = _benchmark_executable_directories(build_dir, config)

    if run_all:
        benchmarks = {
            path
            for directory in directories
            if directory.is_dir()
            for path in directory.iterdir()
            if _is_benchmark_executable(path)
        }
        return sorted(benchmarks, key=lambda path: path.name.lower())

    paths = []
    for name in names:
        matches = [
            path
            for directory in directories
            if (path := _benchmark_path(directory, name)).is_file()
        ]
        if not matches:
            searched = ", ".join(str(directory) for directory in directories)
            raise FileNotFoundError(f"benchmark '{name}' was not found in {searched}")
        paths.append(matches[0])
    return paths


def _runtime_environment(build_dir: Path, config: str) -> dict[str, str]:
    environment = os.environ.copy()
    directories = _benchmark_library_directories(build_dir, config)
    existing_directories = [str(directory) for directory in directories if directory.is_dir()]

    if os.name == "nt":
        variable = "PATH"
    elif sys.platform == "darwin":
        variable = "DYLD_LIBRARY_PATH"
    else:
        variable = "LD_LIBRARY_PATH"

    if existing_directories:
        old_value = environment.get(variable)
        values = list(existing_directories)
        if old_value:
            values.append(old_value)
        environment[variable] = os.pathsep.join(values)

    return environment


def _has_option(arguments: list[str], option: str) -> bool:
    return any(argument == option or argument.startswith(f"{option}=") for argument in arguments)


def _benchmark_color_arguments(arguments: list[str], enabled: bool) -> list[str]:
    if _has_option(arguments, "--benchmark_color"):
        return list(arguments)
    value = "true" if enabled else "false"
    return [*arguments, f"--benchmark_color={value}"]


def _read_json(path: Path):
    with path.open(encoding="utf-8") as report_file:
        return json.load(report_file)


def _read_history(path: Path) -> list[dict]:
    history = _read_json(path)
    if not isinstance(history, list):
        raise ValueError(f"benchmark history must be a JSON array: {path}")
    return history


def _read_google_benchmark_report(path: Path) -> dict:
    report = _read_json(path)
    if not isinstance(report, dict) or not isinstance(report.get("benchmarks"), list):
        raise ValueError(f"Google Benchmark produced an invalid report in {path}")
    if not report["benchmarks"]:
        raise ValueError(f"Google Benchmark produced no timing records in {path}")
    return report


def _run_benchmark(
    executable: Path,
    benchmark_args: list[str],
    environment: dict[str, str],
    build_dir: Path,
    output_path: Path,
    color_enabled: bool,
) -> dict:
    if _has_option(benchmark_args, "--benchmark_out"):
        raise ValueError("do not pass --benchmark_out; use --save instead")
    if _has_option(benchmark_args, "--benchmark_out_format"):
        raise ValueError("do not pass --benchmark_out_format; bench.py needs JSON output")

    command = [
        str(executable),
        *_benchmark_color_arguments(benchmark_args, color_enabled),
        f"--benchmark_out={output_path}",
        "--benchmark_out_format=json",
    ]
    result = subprocess.run(command, cwd=build_dir, env=environment, check=False)
    if result.returncode:
        raise RuntimeError(f"{executable.name} exited with status {result.returncode}")

    return _read_google_benchmark_report(output_path)


def _list_benchmark(
    executable: Path,
    benchmark_args: list[str],
    environment: dict[str, str],
    build_dir: Path,
) -> list[str]:
    if _has_option(benchmark_args, "--benchmark_out"):
        raise ValueError("do not pass --benchmark_out when listing benchmarks")
    if _has_option(benchmark_args, "--benchmark_out_format"):
        raise ValueError("do not pass --benchmark_out_format when listing benchmarks")

    result = subprocess.run(
        [str(executable), *benchmark_args, "--benchmark_list_tests=true"],
        cwd=build_dir,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode:
        raise RuntimeError(f"{executable.name} exited with status {result.returncode}")
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def _benchmark_records(report: dict) -> dict[str, dict]:
    records = {}
    for benchmark in report["benchmarks"]:
        if "cpu_time" not in benchmark or "real_time" not in benchmark:
            continue
        name = benchmark.get("run_name", benchmark.get("name"))
        if name:
            records[name] = benchmark
    return records


def _history_path(repository_root: Path, name: str) -> Path:
    return repository_root / "benchmarks" / "history" / f"{Path(name).stem}.json"


def _query_run(
    entry: dict,
    metric: str,
    case_pattern: re.Pattern[str] | None,
    previous_entry: dict | None,
) -> dict:
    if not isinstance(entry, dict):
        raise ValueError("benchmark history contains a non-object run entry")

    report = entry.get("report")
    if not isinstance(report, dict) or not isinstance(report.get("benchmarks"), list):
        raise ValueError("benchmark history contains an invalid Google Benchmark report")

    records = _benchmark_records(report)
    previous_records = {}
    if previous_entry is not None:
        if not isinstance(previous_entry, dict):
            raise ValueError("benchmark history contains a non-object run entry")
        previous_report = previous_entry.get("report")
        if not isinstance(previous_report, dict) or not isinstance(previous_report.get("benchmarks"), list):
            raise ValueError("benchmark history contains an invalid Google Benchmark report")
        previous_records = _benchmark_records(previous_report)

    if case_pattern is not None:
        records = {
            name: record
            for name, record in records.items()
            if case_pattern.search(name)
        }

    benchmarks = []
    for name in sorted(records):
        record = records[name]
        if metric not in record:
            continue
        benchmark = {
            "name": name,
            "value": record[metric],
            "time_unit": record.get("time_unit", "ns"),
        }
        if "iterations" in record:
            benchmark["iterations"] = record["iterations"]

        if previous_entry is None:
            comparison = {"status": "baseline"}
        elif (
            entry.get("config") != previous_entry.get("config")
            or entry.get("arguments", []) != previous_entry.get("arguments", [])
        ):
            comparison = {
                "status": "not_comparable",
                "reason": "run settings changed",
            }
        else:
            previous = previous_records.get(name)
            if previous is None:
                comparison = {"status": "new"}
            else:
                current_unit = record.get("time_unit", "ns")
                previous_unit = previous.get("time_unit", "ns")
                previous_value = previous.get(metric)
                if current_unit != previous_unit:
                    comparison = {
                        "status": "not_comparable",
                        "reason": "time unit changed",
                    }
                elif previous_value is None or previous_value == 0:
                    comparison = {
                        "status": "not_comparable",
                        "reason": "baseline is zero or missing",
                    }
                else:
                    change_percent = (record[metric] - previous_value) / previous_value * 100
                    if change_percent < 0:
                        status = "improvement"
                    elif change_percent > 0:
                        status = "regression"
                    else:
                        status = "unchanged"
                    comparison = {
                        "status": status,
                        "previous_value": previous_value,
                        "change_percent": change_percent,
                    }
        if previous_entry is not None:
            comparison["previous_timestamp"] = previous_entry.get("timestamp")
        benchmark["comparison"] = comparison
        benchmarks.append(benchmark)

    result = {"timestamp": entry.get("timestamp")}
    if "description" in entry:
        result["description"] = entry["description"]
    result.update(
        {
            "git_commit": entry.get("git_commit"),
            "git_dirty": entry.get("git_dirty"),
            "config": entry.get("config"),
            "arguments": entry.get("arguments", []),
            "benchmarks": benchmarks,
        }
    )
    return result


def _print_query_text(results: list[dict], metric: str) -> None:
    if not results:
        print("No saved benchmark history found.")
        return

    for index, result in enumerate(results):
        if index:
            print()

        name = result["benchmark"]
        runs = result["runs"]
        if not runs:
            print(f"No saved history for {name}.")
            continue

        print(f"{name} - last {len(runs)} run(s) - {metric}")
        for run_index, run in enumerate(runs, start=1):
            title = f"  Run {run_index}: {run.get('timestamp') or 'unknown time'}"
            if run.get("description"):
                title += f" - {run['description']}"
            print(title)

            metadata = []
            if run.get("git_commit"):
                metadata.append(f"commit {run['git_commit']}")
            if run.get("git_dirty") is not None:
                metadata.append("dirty" if run["git_dirty"] else "clean")
            if run.get("config"):
                metadata.append(str(run["config"]))
            if metadata:
                print(f"    {' | '.join(metadata)}")

            arguments = run.get("arguments") or []
            if arguments:
                print(f"    arguments: {' '.join(str(argument) for argument in arguments)}")

            if not run["benchmarks"]:
                print("    (no matching benchmark cases)")
                continue
            for benchmark in run["benchmarks"]:
                value = f"{benchmark['value']} {benchmark['time_unit']}"
                iterations = benchmark.get("iterations")
                if iterations is not None:
                    value += f" ({iterations} iterations)"
                comparison = benchmark["comparison"]
                status = comparison["status"]
                if status == "baseline":
                    comparison_text = "baseline"
                elif status in {"improvement", "regression", "unchanged"}:
                    comparison_text = f"{comparison['change_percent']:+.1f}%, {status}"
                elif status == "not_comparable":
                    comparison_text = f"not comparable ({comparison['reason']})"
                else:
                    comparison_text = status
                print(f"    {benchmark['name']}: {value} ({comparison_text})")


def _query_history(
    repository_root: Path,
    names: list[str],
    run_count: int,
    run_all: bool,
    metric: str,
    case_filter: str | None,
    output_format: str,
) -> int:
    history_directory = repository_root / "benchmarks" / "history"
    if run_all:
        paths = sorted(history_directory.glob("*_benchmarks.json"), key=lambda path: path.name.lower())
    else:
        paths = [_history_path(repository_root, name) for name in names]

    case_pattern = re.compile(case_filter) if case_filter else None
    results = []
    for path in paths:
        result = {
            "benchmark": path.stem,
            "runs": [],
        }
        if path.exists():
            history = _read_history(path)
            start = max(0, len(history) - run_count)
            comparison_start = max(0, start - 1)
            entries = history[comparison_start:]
            projected_runs = []
            for index, entry in enumerate(entries):
                previous_entry = entries[index - 1] if index else None
                projected_runs.append(
                    _query_run(entry, metric, case_pattern, previous_entry)
                )
            result["runs"] = projected_runs[-run_count:]
        results.append(result)

    if output_format == "json":
        payload = {
            "metric": metric,
            "benchmarks": results,
        }
        if case_filter:
            payload["case_filter"] = case_filter
        print(json.dumps(payload, indent=2))
    else:
        _print_query_text(results, metric)
    return 0


def _status_color(status: str) -> str:
    return {
        "Regression": "red",
        "Improvement": "green",
        "New": "yellow",
        "Removed": "yellow",
        "Skipped": "yellow",
    }.get(status, "yellow")


def _compare_reports(
    executable: Path,
    current: dict,
    baseline_path: Path,
    metric: str,
    threshold: float,
    color_enabled: bool,
) -> tuple[int, int, int]:
    baseline = _read_google_benchmark_report(baseline_path)
    current_records = _benchmark_records(current)
    baseline_records = _benchmark_records(baseline)
    comparison_count = 0
    regression_count = 0
    reported_count = 0

    for name in sorted(current_records):
        record = current_records[name]
        current_unit = record.get("time_unit", "ns")
        previous = baseline_records.get(name)
        if previous is None:
            reported_count += 1
            print(f"  {_paint('New', _status_color('New'), color_enabled)}  {name}")
            continue

        previous_unit = previous.get("time_unit", "ns")
        previous_value = previous[metric]
        current_value = record[metric]
        if previous_unit != current_unit:
            reported_count += 1
            print(
                f"  {_paint('Skipped', _status_color('Skipped'), color_enabled)}"
                f"  {name} (time unit changed)"
            )
            continue

        if previous_value == 0:
            reported_count += 1
            print(
                f"  {_paint('Skipped', _status_color('Skipped'), color_enabled)}"
                f"  {name} (baseline is zero)"
            )
            continue

        comparison_count += 1
        change = (current_value - previous_value) / previous_value * 100
        if change > threshold:
            status = "Regression"
            regression_count += 1
        elif change < -threshold:
            status = "Improvement"
        else:
            continue
        reported_count += 1
        print(
            f"  {_paint(status, _status_color(status), color_enabled)}"
            f"  {name}  {change:+.2f}%"
        )

    for name in sorted(set(baseline_records) - set(current_records)):
        reported_count += 1
        print(
            f"  {_paint('Removed', _status_color('Removed'), color_enabled)}"
            f"  {name}"
        )

    return comparison_count, regression_count, reported_count


def _git_metadata(repository_root: Path) -> tuple[str | None, bool]:
    commit_result = subprocess.run(
        ["git", "-C", str(repository_root), "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    )
    status_result = subprocess.run(
        ["git", "-C", str(repository_root), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=False,
    )
    commit = commit_result.stdout.strip() or None
    return commit, bool(status_result.stdout.strip())


def _write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as report_file:
        json.dump(value, report_file, indent=2)
        report_file.write("\n")


def _save_report(
    repository_root: Path,
    executable: Path,
    report: dict,
    timestamp: str,
    description: str | None,
    config: str,
    benchmark_args: list[str],
    git_commit: str | None,
    git_dirty: bool,
) -> None:
    benchmarks_directory = repository_root / "benchmarks"
    latest_path = benchmarks_directory / f"{executable.stem}.json"
    history_path = benchmarks_directory / "history" / f"{executable.stem}.json"
    history = []
    if history_path.exists():
        history = _read_history(history_path)

    entry = {"timestamp": timestamp}
    if description is not None:
        entry["description"] = description
    entry.update(
        {
            "git_commit": git_commit,
            "git_dirty": git_dirty,
            "config": config,
            "arguments": benchmark_args,
            "report": report,
        }
    )
    history.append(entry)
    _write_json(latest_path, report)
    _write_json(history_path, history)


def _parse_args() -> tuple[argparse.Namespace, list[str]]:
    argv = sys.argv[1:]
    if "--" in argv:
        separator = argv.index("--")
        runner_argv = argv[:separator]
        benchmark_argv = argv[separator + 1 :]
    else:
        runner_argv = argv
        benchmark_argv = []

    parser = argparse.ArgumentParser(
        description="Run, query, and compare Radia Google Benchmark results."
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="CMake build directory containing sharedlibs; not needed for --query.",
    )
    parser.add_argument(
        "--config",
        default="RelWithDebInfo",
        help="CMake configuration to run (default: %(default)s).",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Run every *_benchmarks executable in the build directory.",
    )
    parser.add_argument(
        "--list",
        dest="list_benchmarks",
        action="store_true",
        help="List discovered benchmark cases instead of running them.",
    )
    parser.add_argument(
        "--query",
        action="store_true",
        help="Query saved benchmark history without running a benchmark.",
    )
    parser.add_argument(
        "--last",
        type=int,
        default=1,
        metavar="N",
        help="Number of recent saved runs to query (default: %(default)s).",
    )
    parser.add_argument(
        "--case-filter",
        metavar="REGEX",
        help="Regular expression selecting benchmark cases in query output.",
    )
    parser.add_argument(
        "--query-format",
        choices=("text", "json"),
        default="text",
        help="Output format for --query (default: %(default)s).",
    )
    parser.add_argument(
        "--save",
        action="store_true",
        help="Save the latest native JSON and append to benchmarks/history/.",
    )
    parser.add_argument(
        "--description",
        metavar="TEXT",
        help="Describe this run in benchmark history; requires --save.",
    )
    parser.add_argument(
        "--compare",
        action="store_true",
        help="Compare against the existing latest JSON before saving.",
    )
    parser.add_argument(
        "--metric",
        choices=("cpu_time", "real_time"),
        default="cpu_time",
        help="Timing metric used for comparisons (default: %(default)s).",
    )
    parser.add_argument(
        "--regression-threshold",
        type=float,
        default=5.0,
        metavar="PERCENT",
        help="Percentage increase that counts as a regression (default: %(default)s).",
    )
    parser.add_argument(
        "--fail-on-regression",
        action="store_true",
        help="Exit nonzero when --compare finds a regression.",
    )
    parser.add_argument(
        "benchmarks",
        nargs="*",
        metavar="BENCHMARK",
        help="Benchmark target names, for example llfilesystem_benchmarks.",
    )
    args = parser.parse_args(runner_argv)

    if args.all and args.benchmarks:
        parser.error("--all cannot be combined with benchmark names")
    if args.query:
        if args.build_dir is not None:
            parser.error("--query does not use --build-dir")
        if benchmark_argv:
            parser.error("--query cannot be combined with Google Benchmark arguments")
        if not args.all and not args.benchmarks:
            parser.error("provide a benchmark name or use --all with --query")
        if (
            args.list_benchmarks
            or args.save
            or args.compare
            or args.fail_on_regression
            or args.description is not None
        ):
            parser.error(
                "--query cannot be combined with --list, --save, --compare, "
                "--description, or --fail-on-regression"
            )
    else:
        if args.build_dir is None:
            parser.error("--build-dir is required unless --query is used")
        if not args.list_benchmarks and not args.all and not args.benchmarks:
            parser.error("provide at least one benchmark name or use --all")
        if args.last != 1:
            parser.error("--last requires --query")
        if args.case_filter is not None:
            parser.error("--case-filter requires --query")
        if args.query_format != "text":
            parser.error("--query-format requires --query")
    if args.list_benchmarks and (args.save or args.compare or args.fail_on_regression):
        parser.error("--list cannot be combined with save or comparison options")
    if args.description is not None:
        args.description = args.description.strip()
        if not args.description:
            parser.error("--description cannot be empty")
        if not args.save:
            parser.error("--description requires --save")
    if args.regression_threshold < 0:
        parser.error("--regression-threshold cannot be negative")
    if args.fail_on_regression and not args.compare:
        parser.error("--fail-on-regression requires --compare")
    if args.last < 1:
        parser.error("--last must be at least 1")
    if args.case_filter is not None:
        try:
            re.compile(args.case_filter)
        except re.error as error:
            parser.error(f"invalid --case-filter: {error}")

    return args, benchmark_argv


def main() -> int:
    args, benchmark_args = _parse_args()
    repository_root = _repository_root()

    if args.query:
        try:
            return _query_history(
                repository_root,
                args.benchmarks,
                args.last,
                args.all,
                args.metric,
                args.case_filter,
                args.query_format,
            )
        except (OSError, TypeError, ValueError, json.JSONDecodeError, re.error) as error:
            print(f"error: could not query benchmark history: {error}", file=sys.stderr)
            return 1

    build_dir = args.build_dir.resolve()
    color_enabled = _use_color()
    run_all = args.all or (args.list_benchmarks and not args.benchmarks)

    try:
        benchmarks = _find_benchmarks(
            build_dir,
            args.config,
            args.benchmarks,
            run_all,
        )
    except FileNotFoundError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if not benchmarks:
        print(
            f"error: no *_benchmarks executable found under "
            f"{build_dir / 'sharedlibs' / args.config}",
            file=sys.stderr,
        )
        return 1

    environment = _runtime_environment(build_dir, args.config)
    if args.list_benchmarks:
        total_benchmarks = 0
        try:
            for index, benchmark in enumerate(benchmarks, start=1):
                if index > 1:
                    print()
                print(f"Benchmark #{index}: {benchmark.stem}")
                cases = _list_benchmark(
                    benchmark,
                    benchmark_args,
                    environment,
                    build_dir,
                )
                if cases:
                    for case in cases:
                        print(f"  {case}")
                    total_benchmarks += len(cases)
                else:
                    print("  (no benchmark cases)")
        except (OSError, RuntimeError, ValueError) as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        print()
        print(f"Total Benchmarks: {total_benchmarks}")
        return 0

    reports = []
    try:
        with tempfile.TemporaryDirectory(prefix=".bench-", dir=build_dir) as temporary_dir:
            temporary_path = Path(temporary_dir)
            for index, benchmark in enumerate(benchmarks, start=1):
                if index > 1:
                    print()
                print(f"Benchmark #{index}: {benchmark.stem}", flush=True)
                report = _run_benchmark(
                    benchmark,
                    benchmark_args,
                    environment,
                    build_dir,
                    temporary_path / f"{index}.json",
                    color_enabled,
                )
                reports.append((benchmark, report))
    except (OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    comparison_count = 0
    regression_count = 0
    comparison_status_count = 0
    missing_baselines = []
    if args.compare:
        for benchmark, report in reports:
            baseline_path = repository_root / "benchmarks" / f"{benchmark.stem}.json"
            if not baseline_path.exists():
                missing_baselines.append(benchmark.stem)
                continue
            try:
                print()
                (
                    current_comparisons,
                    current_regressions,
                    current_status_count,
                ) = _compare_reports(
                    benchmark,
                    report,
                    baseline_path,
                    args.metric,
                    args.regression_threshold,
                    color_enabled,
                )
                comparison_count += current_comparisons
                regression_count += current_regressions
                comparison_status_count += current_status_count
            except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
                print(f"error: could not compare {baseline_path}: {error}", file=sys.stderr)
                return 1

    if args.save:
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        git_commit, git_dirty = _git_metadata(repository_root)
        try:
            for benchmark, report in reports:
                _save_report(
                    repository_root,
                    benchmark,
                    report,
                    timestamp,
                    args.description,
                    args.config,
                    benchmark_args,
                    git_commit,
                    git_dirty,
                )
        except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
            print(f"error: could not save benchmark report: {error}", file=sys.stderr)
            return 1

    if args.compare:
        if comparison_count == 0:
            print("Nothing to compare.")
        else:
            subject = "There is" if regression_count == 1 else "There are"
            regression_word = "regression" if regression_count == 1 else "regressions"
            summary_prefix = f"{subject} {regression_count} {regression_word}"
            summary_suffix = f" out of {comparison_count}"
            if comparison_status_count:
                print()
            print(f"{summary_prefix}{summary_suffix}")
        for benchmark in missing_baselines:
            if comparison_count:
                print(f"Nothing to compare for {benchmark}.")

    if regression_count and args.fail_on_regression:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
