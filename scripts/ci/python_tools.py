#!/usr/bin/env python3
"""Put a venv of pinned Python tools on a GitHub Actions job's path.

The venv is built once for each runner, interpreter and list of
requirements, and restored whole from the Actions cache after that, so a
job with a hit runs no pip at all. The python-tools action calls this three
times:

  key       the cache key and the venv's path, as step outputs; the
            requirements, from REQUIREMENTS, go to a file beside the venv
  build     on a miss, the venv. Without pip, which a restored venv never
            runs: the setup's own pip installs into it. Without the CMake
            documentation either, which no build reads. Each file left out
            is one fewer to unpack on every restore, which on Windows is
            what a restore costs
  activate  the venv's scripts ahead on the path, and the venv as the
            Python root. setup-python names its own interpreter in
            Python_ROOT_DIR and Python3_ROOT_DIR, and CMake's FindPython
            takes a root it is given ahead of an active venv: the tests
            that spawn a Python peer would get an interpreter without llsd

The key names the exact interpreter because a venv is not portable: it
points at the interpreter it was made from, by path.
"""

import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import venv


def scripts_dir(root, windows=os.name == "nt"):
    return root / ("Scripts" if windows else "bin")


def interpreter(root, windows=os.name == "nt"):
    return scripts_dir(root, windows) / ("python.exe" if windows else "python")


def normalized(requirements):
    """The requirements as pip reads them, whatever line endings YAML gave."""
    lines = [line.strip() for line in requirements.splitlines()]
    return "\n".join(line for line in lines if line) + "\n"


def cache_key(runner_os, runner_arch, version, base_prefix, requirements):
    digest = hashlib.sha256()
    for part in (base_prefix, normalized(requirements)):
        digest.update(part.encode("utf-8"))
        digest.update(b"\0")
    return f"python-tools-{runner_os}-{runner_arch}-{version}-{digest.hexdigest()[:16]}"


def paths(env):
    temp = Path(os.path.normpath(env["RUNNER_TEMP"]))
    return temp / "python-tools", temp / "python-tools-requirements.txt"


def documentation(python):
    """CMake's Help and doc trees, when CMake is one of the tools."""
    probe = subprocess.run(
        [str(python), "-c", "import cmake; print(cmake.CMAKE_DOC_DIR); print(cmake.CMAKE_SHARE_DIR)"],
        text=True, capture_output=True,
    )
    if probe.returncode != 0:
        return []
    doc, share = probe.stdout.splitlines()
    return [Path(doc), *sorted(Path(share).glob("cmake-*/Help"))]


def file_count(root):
    return sum(len(files) for _, _, files in os.walk(root))


def key(env):
    root, requirements = paths(env)
    text = normalized(env["REQUIREMENTS"])
    requirements.write_text(text, encoding="utf-8")
    name = cache_key(env["RUNNER_OS"], env["RUNNER_ARCH"], platform.python_version(), sys.base_prefix, text)
    with open(env["GITHUB_OUTPUT"], "a", encoding="utf-8", newline="\n") as stream:
        stream.write(f"key={name}\n")
        stream.write(f"venv={root}\n")
    print(f"{name}: {root}")


def build(env):
    root, requirements = paths(env)
    # A copied interpreter outside Windows would look for its shared
    # library beside the copy.
    venv.EnvBuilder(with_pip=False, symlinks=os.name != "nt").create(root)
    python = interpreter(root)
    subprocess.run(
        [sys.executable, "-m", "pip", "--python", str(python), "install",
         "--disable-pip-version-check", "--no-input", "--no-cache-dir",
         "--requirement", str(requirements)],
        check=True,
    )
    installed = file_count(root)
    for tree in documentation(python):
        shutil.rmtree(tree, ignore_errors=True)
    print(f"{root}: {file_count(root)} files, {installed} before the CMake documentation went")


def activate(env):
    root, _ = paths(env)
    # A venv whose interpreter is gone fails here rather than in the build.
    subprocess.run([str(interpreter(root)), "-c", ""], check=True)
    # The runner puts each line of GITHUB_PATH ahead of the ones before it.
    with open(env["GITHUB_PATH"], "a", encoding="utf-8", newline="\n") as stream:
        stream.write(f"{scripts_dir(root)}\n")
    with open(env["GITHUB_ENV"], "a", encoding="utf-8", newline="\n") as stream:
        for name in ("VIRTUAL_ENV", "Python_ROOT_DIR", "Python3_ROOT_DIR"):
            stream.write(f"{name}={root}\n")


COMMANDS = {"key": key, "build": build, "activate": activate}


def main(argv):
    if len(argv) != 2 or argv[1] not in COMMANDS:
        sys.exit(f"usage: {argv[0]} {'|'.join(COMMANDS)}")
    COMMANDS[argv[1]](dict(os.environ))


if __name__ == "__main__":
    main(sys.argv)
