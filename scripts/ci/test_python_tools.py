from pathlib import Path, PureWindowsPath
import unittest

import python_tools as tools


REQUIREMENTS = 'cmake==4.4.3; sys_platform != "linux"\nninja==1.13.2\n'
PREFIX = "/Users/runner/hostedtoolcache/Python/3.14.7/arm64"


class PythonToolsTests(unittest.TestCase):
    def test_scripts_dir_follows_the_platform(self):
        root = PureWindowsPath(r"D:\a\_temp\python-tools")
        self.assertEqual(tools.scripts_dir(root, windows=True), root / "Scripts")
        self.assertEqual(tools.interpreter(root, windows=True), root / "Scripts" / "python.exe")
        root = Path("/home/runner/work/_temp/python-tools")
        self.assertEqual(tools.interpreter(root, windows=False), root / "bin" / "python")

    def test_normalized_drops_blank_lines_and_line_endings(self):
        self.assertEqual(tools.normalized("  a==1\r\n\r\nb==2  "), "a==1\nb==2\n")

    def test_key_names_the_runner_and_the_interpreter(self):
        key = tools.cache_key("macOS", "ARM64", "3.14.7", PREFIX, REQUIREMENTS)
        self.assertTrue(key.startswith("python-tools-macOS-ARM64-3.14.7-"))

    def test_key_ignores_what_pip_ignores(self):
        self.assertEqual(
            tools.cache_key("Linux", "X64", "3.14.7", PREFIX, REQUIREMENTS),
            tools.cache_key("Linux", "X64", "3.14.7", PREFIX, "\r\n" + REQUIREMENTS.replace("\n", "\r\n\r\n")),
        )

    def test_key_changes_with_anything_the_venv_depends_on(self):
        base = tools.cache_key("Linux", "X64", "3.14.7", PREFIX, REQUIREMENTS)
        changed = [
            tools.cache_key("Linux", "ARM64", "3.14.7", PREFIX, REQUIREMENTS),
            tools.cache_key("Linux", "X64", "3.14.8", PREFIX, REQUIREMENTS),
            tools.cache_key("Linux", "X64", "3.14.7", "/opt/hostedtoolcache/Python/3.14.7/x64", REQUIREMENTS),
            tools.cache_key("Linux", "X64", "3.14.7", PREFIX, REQUIREMENTS.replace("1.13.2", "1.13.3")),
        ]
        for key in changed:
            self.assertNotEqual(key, base)

    def test_paths_sit_in_the_runner_temp(self):
        root, requirements = tools.paths({"RUNNER_TEMP": "/home/runner/work/_temp/"})
        self.assertEqual(root, Path("/home/runner/work/_temp/python-tools"))
        self.assertEqual(requirements, Path("/home/runner/work/_temp/python-tools-requirements.txt"))


if __name__ == "__main__":
    unittest.main()
