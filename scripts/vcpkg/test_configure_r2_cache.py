import contextlib
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import configure_r2_cache as cache


class R2CacheTests(unittest.TestCase):
    def setUp(self):
        self.env = {
            "R2_ENDPOINT_URL": "https://" + "a" * 32 + ".r2.cloudflarestorage.com",
            "R2_BUCKET": "alchemy-cache",
            "R2_ACCESS_KEY_ID": "test-access-key",
            "R2_SECRET_ACCESS_KEY": "test-secret-key",
            "R2_CACHE_MODE": "readwrite",
            "VCPKG_ROOT": "/vcpkg root",
            "VCPKG_BINARY_SOURCES": "clear;default,readwrite;nuget,https://example.com,readwrite",
        }

    def test_preserves_migration_sources_and_overrides_ambient_aws_settings(self):
        self.env.update(AWS_REGION="us-east-1", AWS_SESSION_TOKEN="stale",
                        AWS_IGNORE_CONFIGURED_ENDPOINT_URLS="true")
        result = cache.cache_environment(self.env)
        self.assertEqual(result["VCPKG_BINARY_SOURCES"], self.env["VCPKG_BINARY_SOURCES"]
                         + ";x-aws,s3://alchemy-cache/cache/,readwrite")
        self.assertEqual(result["AWS_REGION"], "auto")
        self.assertEqual(result["AWS_SESSION_TOKEN"], "")
        self.assertEqual(result["AWS_IGNORE_CONFIGURED_ENDPOINT_URLS"], "false")

    def test_invalid_configuration_cannot_inject_sources_or_job_environment(self):
        cases = {
            "R2_CACHE_MODE": ("", "write", "READ", "read;clear", "read\nINJECTED=yes"),
            "R2_ENDPOINT_URL": ("", "https://example.com", self.env["R2_ENDPOINT_URL"] + "/bad"),
            "R2_BUCKET": ("", "bucket;clear", "bucket/path", "bucket\nINJECTED=yes"),
            "R2_ACCESS_KEY_ID": ("", "key\rINJECTED=yes"),
            "R2_SECRET_ACCESS_KEY": ("", "secret\nINJECTED=yes", "secret\0"),
            "VCPKG_BINARY_SOURCES": ("clear\nINJECTED=yes",),
        }
        for name, values in cases.items():
            for value in values:
                with self.subTest(name=name, value=value):
                    with self.assertRaises(ValueError):
                        cache.cache_environment(dict(self.env, **{name: value}))

    def test_read_mode_and_default_cannot_enable_remote_uploads(self):
        for mode in ("read", None):
            with self.subTest(mode=mode):
                env = dict(self.env)
                if mode is None:
                    del env["R2_CACHE_MODE"]
                else:
                    env["R2_CACHE_MODE"] = mode
                result = cache.cache_environment(env)
                self.assertEqual(result["VCPKG_BINARY_SOURCES"].split(";")[-1],
                                 "x-aws,s3://alchemy-cache/cache/,read")

    def test_uses_vcpkg_selected_cli_with_environment_endpoint(self):
        calls = []

        def run(command, **kwargs):
            calls.append((command, kwargs))
            output = "/selected aws/aws\n" if len(calls) == 1 else "aws-cli/2.13.0 Python/3.11"
            return subprocess.CompletedProcess(command, 0, output)

        self.env.update(cache.cache_environment(self.env))
        with patch.object(cache.subprocess, "run", side_effect=run):
            cache.preflight(self.env)
        self.assertEqual(calls[0][0][1:], ["fetch", "aws", "--x-stderr-status"])
        self.assertEqual(calls[1][0], ["/selected aws/aws", "--version"])
        self.assertEqual(calls[2][0][:3], ["/selected aws/aws", "s3api", "list-objects-v2"])
        self.assertNotIn("--endpoint-url", calls[2][0])
        self.assertEqual(calls[2][1]["env"]["AWS_ENDPOINT_URL_S3"], self.env["R2_ENDPOINT_URL"])

    def test_rejects_old_cli_before_network_request(self):
        for version in ("aws-cli/1.44.0", "aws-cli/2.12.9", "unknown"):
            with self.subTest(version=version):
                results = [subprocess.CompletedProcess([], 0, "/aws\n"),
                           subprocess.CompletedProcess([], 0, version)]
                with patch.object(cache.subprocess, "run", side_effect=results) as run:
                    with self.assertRaises(ValueError):
                        cache.preflight(self.env)
                    self.assertEqual(run.call_count, 2)

    def test_preflight_failure_leaves_job_environment_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "github-env"
            output.write_text("EXISTING=value\n", encoding="utf-8")
            self.env["GITHUB_ENV"] = str(output)
            with patch.dict(os.environ, self.env, clear=True):
                with patch.object(cache, "preflight", side_effect=subprocess.CalledProcessError(1, ["aws"])):
                    with self.assertRaises(subprocess.CalledProcessError):
                        cache.main()
            self.assertEqual(output.read_text(encoding="utf-8"), "EXISTING=value\n")

    def test_success_exports_credentials_without_printing_them(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "github-env"
            self.env["GITHUB_ENV"] = str(output)
            messages = io.StringIO()
            with patch.dict(os.environ, self.env, clear=True):
                with patch.object(cache, "preflight"), contextlib.redirect_stdout(messages):
                    cache.main()
            values = dict(line.split("=", 1) for line in output.read_text(encoding="utf-8").splitlines())
            self.assertEqual(values, cache.cache_environment(self.env))
            self.assertNotIn(self.env["R2_ACCESS_KEY_ID"], messages.getvalue())
            self.assertNotIn(self.env["R2_SECRET_ACCESS_KEY"], messages.getvalue())

    def test_read_only_mode_exports_only_reader_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "github-env"
            self.env.update(GITHUB_ENV=str(output), R2_CACHE_MODE="read",
                            R2_ACCESS_KEY_ID="reader-id", R2_SECRET_ACCESS_KEY="reader-secret")
            messages = io.StringIO()
            with patch.dict(os.environ, self.env, clear=True):
                with patch.object(cache, "preflight"), contextlib.redirect_stdout(messages):
                    cache.main()
            values = dict(line.split("=", 1) for line in output.read_text(encoding="utf-8").splitlines())
            self.assertEqual(values["AWS_ACCESS_KEY_ID"], "reader-id")
            self.assertEqual(values["AWS_SECRET_ACCESS_KEY"], "reader-secret")
            self.assertTrue(values["VCPKG_BINARY_SOURCES"].endswith("/cache/,read"))
            self.assertIn("in read mode", messages.getvalue())
            self.assertNotIn("reader-secret", messages.getvalue())


if __name__ == "__main__":
    unittest.main()
