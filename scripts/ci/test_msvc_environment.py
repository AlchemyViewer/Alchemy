import unittest

import msvc_environment as msvc


VS = r"C:\Program Files\Microsoft Visual Studio\18\Enterprise"
TOOLS = VS + r"\VC\Tools\MSVC\14.50.35717\bin\HostX64\x64"
SDK = r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
CMAKE = VS + r"\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
NINJA = VS + r"\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
PYTHON = r"C:\hostedtoolcache\windows\Python\3.14.7\x64\Scripts"


class MsvcEnvironmentTests(unittest.TestCase):
    def setUp(self):
        self.before = {
            "Path": PYTHON + r";C:\Windows\system32",
            "VCPKG_ROOT": r"D:\a\Alchemy\Alchemy\vcpkg",
            "RUNNER_ARCH": "X64",
        }
        self.after = {
            "PATH": ";".join([TOOLS, CMAKE, NINJA, VS + r"\VC\vcpkg", SDK, PYTHON, r"C:\Windows\System32\\"]),
            "VCPKG_ROOT": VS + r"\VC\vcpkg",
            "RUNNER_ARCH": "X64",
            "INCLUDE": VS + r"\VC\Tools\MSVC\14.50.35717\include",
            "VSCMD_ARG_TGT_ARCH": "x64",
            "PROMPT": "$P$G",
        }

    def test_architecture_follows_the_runner(self):
        self.assertEqual(msvc.architecture("X64"), "x64")
        self.assertEqual(msvc.architecture("ARM64"), "arm64")
        for runner_arch in ("", "X86", "x64"):
            with self.assertRaises(ValueError):
                msvc.architecture(runner_arch)

    def test_parse_set_keys_names_as_windows_compares_them(self):
        output = "Path=C:\\a;C:\\b\r\nINCLUDE=C:\\x=y\r\nnot a variable\r\n"
        self.assertEqual(msvc.parse_set(output), {"PATH": "C:\\a;C:\\b", "INCLUDE": "C:\\x=y"})

    def test_hands_on_only_what_vcvars_changed(self):
        variables, _ = msvc.job_environment(self.before, self.after)
        self.assertEqual(variables, {
            "INCLUDE": self.after["INCLUDE"],
            "VSCMD_ARG_TGT_ARCH": "x64",
        })

    def test_keeps_the_submodule_vcpkg_and_the_pinned_cmake_and_ninja(self):
        variables, entries = msvc.job_environment(self.before, self.after)
        self.assertNotIn("VCPKG_ROOT", variables)
        self.assertNotIn("PATH", variables)
        self.assertEqual(entries, [TOOLS, SDK])

    def test_path_entries_compare_without_case_or_trailing_separator(self):
        self.after["PATH"] = ";".join([TOOLS, TOOLS.upper() + "\\", PYTHON.lower()])
        _, entries = msvc.job_environment(self.before, self.after)
        self.assertEqual(entries, [TOOLS])

    def test_a_variable_cannot_add_lines_to_the_job_environment(self):
        self.after["INCLUDE"] = "C:\\include\nINJECTED=yes"
        with self.assertRaises(ValueError):
            msvc.job_environment(self.before, self.after)


if __name__ == "__main__":
    unittest.main()
