#!/usr/bin/env python3
"""Give a GitHub Actions job the MSVC build environment, for Ninja.

vcvarsall.bat puts cl, link, rc, mt and the Windows SDK on the path, with
the INCLUDE and LIB they read, for one target architecture. This runs it
for the runner's own architecture under the Visual Studio the Visual
Studio generator would pick, and hands the steps after it what it changed:
the variables through GITHUB_ENV and the new PATH entries through
GITHUB_PATH. Two of its changes stay out: Visual Studio's own CMake and
Ninja, which would shadow the pinned ones every vcpkg package ABI hashes,
and VCPKG_ROOT, which names Visual Studio's vcpkg rather than the
submodule.
"""

import os
from pathlib import Path
import subprocess

# Visual Studio 2026, which the Visual Studio 18 2026 generator uses.
VERSION_RANGE = "[18.0,19.0)"
ARCHITECTURES = {"X64": "x64", "ARM64": "arm64"}
# Set by the developer prompt but not wanted in the job.
EXCLUDED_VARIABLES = {"PATH", "VCPKG_ROOT", "PROMPT"}
# Path entries of the tools the job pins or brings itself.
EXCLUDED_PATH_PARTS = ("\\commonextensions\\microsoft\\cmake\\", "\\vc\\vcpkg")


def architecture(runner_arch):
    try:
        return ARCHITECTURES[runner_arch]
    except KeyError:
        raise ValueError(f"No MSVC architecture for a {runner_arch or 'nameless'} runner") from None


def parse_set(output):
    """The variables `set` printed, keyed by upper-case name as Windows
    compares them."""
    variables = {}
    for line in output.splitlines():
        name, sep, value = line.partition("=")
        if sep and name:
            variables[name.upper()] = value
    return variables


def _path_key(entry):
    return entry.rstrip("\\/").lower()


def job_environment(before, after):
    """What vcvarsall changed: the variables to set, and the PATH entries
    to add, in the order it put them."""
    before = {name.upper(): value for name, value in before.items()}
    after = {name.upper(): value for name, value in after.items()}
    variables = {}
    for name, value in after.items():
        if name in EXCLUDED_VARIABLES or before.get(name) == value:
            continue
        if any(c in name + value for c in "\r\n\0"):
            raise ValueError(f"{name} does not fit on one line of GITHUB_ENV")
        variables[name] = value

    known = {_path_key(entry) for entry in before.get("PATH", "").split(";") if entry}
    entries = []
    for entry in after.get("PATH", "").split(";"):
        key = _path_key(entry)
        if not entry or key in known or any(part in key + "\\" for part in EXCLUDED_PATH_PARTS):
            continue
        known.add(key)
        entries.append(entry)
    return variables, entries


def visual_studio(env):
    vswhere = (Path(env.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
               / "Microsoft Visual Studio" / "Installer" / "vswhere.exe")
    result = subprocess.run(
        [str(vswhere), "-version", VERSION_RANGE, "-products", "*", "-latest",
         "-property", "installationPath"],
        check=True, text=True, stdout=subprocess.PIPE,
    )
    installation = result.stdout.strip()
    if not installation:
        raise ValueError(f"No Visual Studio {VERSION_RANGE} on this runner")
    return Path(installation)


def vcvars(installation, arch):
    vcvarsall = installation / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    # cmd /s keeps the quotes inside the outer pair as written.
    result = subprocess.run(
        f'cmd /d /s /c ""{vcvarsall}" {arch} >nul && set"',
        check=True, text=True, stdout=subprocess.PIPE,
    )
    return parse_set(result.stdout)


def main():
    env = dict(os.environ)
    arch = architecture(env.get("RUNNER_ARCH", ""))
    installation = visual_studio(env)
    variables, entries = job_environment(env, vcvars(installation, arch))
    with open(env["GITHUB_ENV"], "a", encoding="utf-8", newline="\n") as stream:
        for name, value in variables.items():
            stream.write(f"{name}={value}\n")
    # The runner puts each line of GITHUB_PATH ahead of the ones before it.
    with open(env["GITHUB_PATH"], "a", encoding="utf-8", newline="\n") as stream:
        for entry in reversed(entries):
            stream.write(f"{entry}\n")
    print(f"MSVC {arch} from {installation}: {len(variables)} variables, {len(entries)} path entries")
    for name in ("VCTOOLSVERSION", "WINDOWSSDKVERSION"):
        if name in variables:
            print(f"  {name}={variables[name]}")


if __name__ == "__main__":
    main()
