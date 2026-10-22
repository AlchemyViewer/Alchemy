from pathlib import Path
import contextlib
import io
import tempfile
import unittest

import check_raw_gl as check


class RawCallTests(unittest.TestCase):
    def names(self, text):
        return [name for _, name in check.raw_calls(text)]

    def test_calls_are_found_with_their_lines(self):
        text = "void f()\n{\n    glClear(GL_COLOR_BUFFER_BIT);\n    glViewport (0, 0, 1, 1);\n}\n"
        self.assertEqual(check.raw_calls(text), [(3, "glClear"), (4, "glViewport")])

    def test_what_is_not_a_raw_call_is_not_counted(self):
        text = ("gGL.clear(GL_COLOR_BUFFER_BIT);\n"
                "if (!glEGLImageTargetTexture2DOES) {}\n"
                "PFNGLCLEARPROC glClear = nullptr;\n"
                "LLGLEnable blend(GL_BLEND);\n"
                "global(1);\n")
        self.assertEqual(self.names(text), [])

    def test_comments_and_strings_are_not_counted(self):
        text = ('// glClear(0);\n'
                '/* glViewport(0, 0, 1, 1);\n   glFlush(); */\n'
                'LL_INFOS() << "glReadPixels(" << \'(\' << LL_ENDL;\n'
                'auto s = R"(glFinish())";\n'
                'glFlush();\n')
        self.assertEqual(check.raw_calls(text), [(6, "glFlush")])

    def test_if_zero_blocks_are_not_counted_and_their_else_is(self):
        text = ("#if 0 // DEPRECATED\n"
                "glClear(0);\n"
                "#if LL_WINDOWS\n"
                "glFlush();\n"
                "#else\n"
                "glFinish();\n"
                "#endif\n"
                "#else\n"
                "glViewport(0, 0, 1, 1);\n"
                "#endif\n"
                "#if LL_DARWIN\n"
                "glPointSize(1.f);\n"
                "#endif\n")
        self.assertEqual(check.raw_calls(text), [(9, "glViewport"), (12, "glPointSize")])

    def test_elif_zero_branches_are_not_counted(self):
        text = ("#if 0\n"
                "glClear(0);\n"
                "#elif 0\n"
                "glFlush();\n"
                "#elif LL_WINDOWS\n"
                "glFinish();\n"
                "#endif\n"
                "#if LL_DARWIN\n"
                "glViewport(0, 0, 1, 1);\n"
                "#elif 0\n"
                "glPointSize(1.f);\n"
                "#else\n"
                "glCullFace(GL_BACK);\n"
                "#endif\n")
        self.assertEqual(check.raw_calls(text), [(6, "glFinish"), (9, "glViewport"), (13, "glCullFace")])

    def test_a_condition_that_only_starts_with_zero_is_counted(self):
        text = ("#if 0 || LL_WINDOWS\n"
                "glClear(0);\n"
                "#elif 0 && LL_DARWIN\n"
                "glFlush();\n"
                "#endif\n"
                "#if 0 // DEPRECATED\n"
                "glFinish();\n"
                "#endif\n")
        self.assertEqual(check.raw_calls(text), [(2, "glClear"), (4, "glFlush")])

    def test_a_call_whose_parenthesis_is_on_a_later_line_is_counted_on_its_name_line(self):
        text = "void f()\n{\n    glClear\n        (GL_COLOR_BUFFER_BIT);\n    glFlush();\n}\n"
        self.assertEqual(check.raw_calls(text), [(3, "glClear"), (5, "glFlush")])

    def test_an_unclosed_quote_keeps_the_lines_after_it(self):
        text = "int n = 1'000;\nglFlush();\n"
        self.assertEqual(check.raw_calls(text), [(2, "glFlush")])


class BaselineTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        self.newview = self.root / "indra" / "newview"
        (self.newview / "tests").mkdir(parents=True)
        self.baseline = self.root / "baseline.txt"
        self.write("llkept.cpp", "void f() { glFlush(); glFinish(); }\n")
        self.write("llclean.cpp", "void f() { gGL.flush(); }\n")
        self.write("tests/llsomething_test.cpp", "void f() { glClear(0); }\n")

    def tearDown(self):
        self.dir.cleanup()

    def write(self, name, text):
        (self.newview / name).write_text(text, encoding="utf-8")

    def run_check(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = check.main(["--root", str(self.root), "--baseline", str(self.baseline), *map(str, args)])
        return status, out.getvalue()

    def test_update_records_every_file_with_calls_and_no_tests(self):
        self.assertEqual(self.run_check("--update")[0], 0)
        self.assertEqual(check.read_baseline(self.baseline), {"indra/newview/llkept.cpp": 2})
        self.assertEqual(self.run_check()[0], 0)

    def test_a_new_call_fails_and_names_its_site(self):
        self.run_check("--update")
        self.write("llclean.cpp", "void f() { gGL.flush(); }\nvoid g() { glClear(0); }\n")
        status, out = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn("indra/newview/llclean.cpp:2: glClear", out)

    def test_a_call_more_than_the_baseline_fails(self):
        self.run_check("--update")
        self.write("llkept.cpp", "void f() { glFlush(); glFinish(); glFlush(); }\n")
        self.assertEqual(self.run_check()[0], 1)

    def test_fewer_calls_than_the_baseline_ask_for_an_update(self):
        self.run_check("--update")
        self.write("llkept.cpp", "void f() { glFlush(); }\n")
        status, out = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn("--update", out)

    def test_a_file_the_baseline_names_that_is_gone_asks_for_an_update(self):
        self.run_check("--update")
        (self.newview / "llkept.cpp").unlink()
        self.assertEqual(self.run_check()[0], 1)

    def test_files_given_are_checked_alone_and_others_ignored(self):
        self.run_check("--update")
        self.write("llkept.cpp", "void f() { glFlush(); glFinish(); glFlush(); }\n")
        outside = self.root / "indra" / "llrender"
        outside.mkdir(parents=True)
        (outside / "llrender.cpp").write_text("void f() { glClear(0); }\n", encoding="utf-8")
        self.assertEqual(self.run_check(self.newview / "llclean.cpp", outside / "llrender.cpp",
                                        self.newview / "tests" / "llsomething_test.cpp")[0], 0)
        self.assertEqual(self.run_check(self.newview / "llkept.cpp")[0], 1)


if __name__ == "__main__":
    unittest.main()
