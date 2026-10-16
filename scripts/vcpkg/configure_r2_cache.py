#!/usr/bin/env python3
"""Add R2 to a GitHub Actions job's vcpkg binary sources after a read preflight."""

import os
from pathlib import Path
import re
import subprocess
import sys


def cache_environment(env):
    mode = env.get("R2_CACHE_MODE", "read")
    if mode not in ("read", "readwrite"):
        raise ValueError("R2_CACHE_MODE must be read or readwrite")
    required = ("R2_ENDPOINT_URL", "R2_BUCKET", "R2_ACCESS_KEY_ID", "R2_SECRET_ACCESS_KEY")
    for name in required:
        if not env.get(name):
            raise ValueError(f"{name} must be set when the R2 cache is enabled")
        if any(c in env[name] for c in "\r\n\0"):
            raise ValueError(f"{name} must be a single line")

    endpoint = env["R2_ENDPOINT_URL"].rstrip("/")
    if not re.fullmatch(r"https://[a-f0-9]{32}(?:\.(?:eu|fedramp))?\.r2\.cloudflarestorage\.com", endpoint):
        raise ValueError("R2_ENDPOINT_URL must be an R2 account S3 HTTPS endpoint")
    bucket = env["R2_BUCKET"]
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{1,61}[a-z0-9]", bucket):
        raise ValueError("R2_BUCKET must be a 3-63 character R2 bucket name")
    sources = env.get("VCPKG_BINARY_SOURCES", "clear;default,readwrite")
    if any(c in sources for c in "\r\n\0"):
        raise ValueError("VCPKG_BINARY_SOURCES must be a single line")

    return {
        "AWS_ENDPOINT_URL_S3": endpoint,
        "AWS_DEFAULT_REGION": "auto",
        "AWS_REGION": "auto",
        "AWS_ACCESS_KEY_ID": env["R2_ACCESS_KEY_ID"],
        "AWS_SECRET_ACCESS_KEY": env["R2_SECRET_ACCESS_KEY"],
        "AWS_SESSION_TOKEN": "",
        "AWS_IGNORE_CONFIGURED_ENDPOINT_URLS": "false",
        "AWS_EC2_METADATA_DISABLED": "true",
        "AWS_PAGER": "",
        "VCPKG_BINARY_SOURCES": f"{sources};x-aws,s3://{bucket}/cache/,{mode}",
    }


def preflight(env):
    executable = "vcpkg.exe" if os.name == "nt" else "vcpkg"
    vcpkg = Path(env["VCPKG_ROOT"]) / executable
    result = subprocess.run(
        [str(vcpkg), "fetch", "aws", "--x-stderr-status"],
        env=env, check=True, text=True, stdout=subprocess.PIPE,
    )
    aws = result.stdout.strip()
    result = subprocess.run(
        [aws, "--version"], env=env, check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    version = re.search(r"aws-cli/(\d+)\.(\d+)\.(\d+)", result.stdout)
    if not version or tuple(map(int, version.groups())) < (2, 13, 0):
        raise ValueError("Install AWS CLI v2.13.0 or newer for environment-configured endpoints")
    print(f"vcpkg selected AWS CLI {version.group(0)} at {aws}")
    subprocess.run(
        [aws, "s3api", "list-objects-v2", "--bucket", env["R2_BUCKET"],
         "--prefix", "cache/", "--max-keys", "1", "--no-paginate",
         "--query", "KeyCount", "--output", "text",
         "--cli-connect-timeout", "15", "--cli-read-timeout", "30"],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )


def main():
    env = dict(os.environ)
    updates = cache_environment(env)
    output = Path(env["GITHUB_ENV"])
    env.update(updates)
    preflight(env)
    with output.open("a", encoding="utf-8", newline="\n") as stream:
        for name, value in updates.items():
            stream.write(f"{name}={value}\n")
    mode = env.get("R2_CACHE_MODE", "read")
    print(f"R2 read preflight passed; enabled cache/ in {mode} mode alongside existing sources")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"R2 cache setup failed: {error}")
