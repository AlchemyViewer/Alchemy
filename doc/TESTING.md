# Testing

The viewer's tests are [Catch2](https://github.com/catchorg/Catch2) v3, from vcpkg. They live beside the library they test, in `indra/<library>/tests/`, and are built only when the build is configured with `-DAL_BUILD_TESTS=ON`.

A library's test files build into one executable, a *test binary*, declared with `al_add_test_binary` (`indra/cmake/ALAddTestBinary.cmake`). Each file is still a CTest entry of its own, run in a process of its own. The harness every binary links is in `indra/test/`: the main, the state guard, the teardown, the matchers and what an `LL_ERRS` does.

## Running

```
ctest --test-dir build-<OS>-<preset> --output-on-failure -LE manual
```

`-LE manual` is the default run, the one `BUILD_AND_RUN_TESTS` makes. It leaves out the benchmarks and each binary's run of all its files in one process while that run is labelled `combined_pending`.

- **Some of them:** each entry is labelled with its library, and with `gl`, `python`, `isa`, `slow`, `combined`, `combined_pending` or `benchmark` where it is one. `ctest -L llui`, `ctest -LE gl`.
- **One file:** its entry is `<binary>/<file stem>`, as `ctest -R '^llui_tests/altextview_test$'`. Or run the binary yourself, from the binary's build folder, with the tag the main gives every case from its file's name: `llui_tests "[#altextview_test]~[.]"`. The `~[.]` leaves out the file's hidden cases, such as its benchmarks, which a tag alone would choose.
- **One case:** by its name, `llui_tests "altextview: a squiggle covers what its wave works out to pixel for pixel"`. A comma in a test spec means "or", so a case name never has one.
- **In another order:** `--order rand --rng-seed <n>`. Cases run in declared order by default.
- **Every case in a process of its own:** configure with `-DAL_TEST_PER_CASE=ON`. Each case is then its own entry, found when CTest runs. It is for chasing a case that passes alone and fails with the others.
- **The log:** `LOGTEST=DEBUG` logs everything at that level as it happens. `LOGFAIL=DEBUG` keeps it and prints a failing case's log only. `--al-log` also writes the log to `<binary>.<pid>.log`.

## Writing a test

Add the file to the `SOURCES` of its library's binary. Its stem names its entry and its tag, so a stem is unique within a binary, and a file is in one binary.

```cpp
#include "linden_common.h"

#include "lldate.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("lldate: a date round-trips through its ISO-8601 text", "[lldate]")
    {
        const LLDate date("2026-10-14T05:30:00Z");
        CHECK(LLDate(date.asString()) == date);
    }
}
```

- **Names:** `"<group>: <what it checks>"`, unique within the binary, ignoring case, since Catch2 matches names that way and CTest accepts duplicates silently. No commas: write "twenty thousand" for 20,000.
- **Tags:** `[<group>]` on every case, plus `[gl]`, `[python]`, `[slow]` or `[luau-solver]` where they apply.
- **`CHECK` by default:** a case goes on after a failed `CHECK`, so a run reports every failure. Use `REQUIRE` only where going on could only crash or measure nothing: a pointer, an optional, an index, a size that what follows reads through.
- **Saying why:** a scoped `INFO`, or `CAPTURE` for values. An `UNSCOPED_INFO` goes with the next assertion, whichever it is, so one made inside a helper the check calls takes it. Prefer a scoped `INFO` around a block.
- **Matchers** (`altestmatchers.h`):
  - `WithinBits(expected, frac_bits)` compares floats as `is_approx_equal_fraction` does;
  - `WithinDistance(expected, distance)`;
  - `LLSDEquals(expected)` compares all the way down and says where two LLSDs part;
  - `MemoryMatches(data, size)`.
- **Fixtures:** `TEST_CASE_METHOD(<fixture>, ...)`. A fixture is made afresh for every case. A sequence whose steps depend on one another is one case with `SECTION`s.

### Linkage

Everything a test file defines goes in an anonymous namespace. A test binary is many files in one executable, and two files' helpers of one name would be one function to the linker.

- **Types production code befriends by name are the exception.** Each lives in `namespace ll_test`, and is declared there by the header that befriends it:
  - `llfontregistry_data` and `llfontregistry_gl_data` (`llrender/llfontregistry.h`);
  - `UIImageProbe` (`llrender/lluiimage.h`);
  - `VertexBufferProbe` (`llrender/llfonttextcache.h`);
  - `RenderProbe` (`llrender/llrender.h`);
  - `TextViewProbe` (`llui/text/altextview.h` and `altextruler.h`);
  - `alincludesearch_data` (`alscript/preprocessor/alincludesearch.h`).
- **Internal is not enough for LL's name-keyed registries.** `LLSingleton`, `LLInstanceTracker` and `LLThreadLocalSingletonPointer` key on `typeid(T).name()`, which is `(anonymous namespace)::X` for every anonymous `X`. A type used with one of them needs a name unique across the tree.
- **Nothing at namespace scope may do anything.** No settings group, work queue, singleton, window, file or environment variable is made or touched there. Every file's initialisers run in every process of its binary, whichever file the process runs. Make such things in a fixture, or in a function's static in the binary's support library.

### Fakes

- **The real class wins over a fake:** declare a real `LLControlGroup` control rather than stubbing `getString`.
- **One home per binary:** where a fake is needed, it lives in the binary's fakes library (`<library>_test_fakes`, a `SUPPORT` object library), one definition of each. Test files hold none.
- **A fake that would replace code another test uses** keeps its test in a binary of its own (`UNIT`, below) until the fake can go.
- **`--allow-multiple-definition`, `/FORCE` and `-z muldefs` are never used.** They are how a fake silently wins. Apple's linker only warns about a duplicate strong symbol under `-dead_strip`, and keeps the first.

### What a case leaves behind

The **state guard** compares a set of process facts after every case with what they were before the first. A difference fails that case, by name: "left gFloaterView null", "changed the cwd", "set env AL_X".

- **Where the checks live:**
  - **The harness's own, in every binary:** the working directory, the environment, the floating-point control state, the C locale, the APR pool, the LLTrace master recorder, the `LLApp` and LLError's fatal function.
  - **Each library's:** registered in an object library, `<library>_test_checks`, which a binary takes in `SUPPORT`. llcommon's watch the live thread pools and work queues and LLError's default level. llui's watch focus, capture, the floater view and the colour table, and also clear what a frame leaves (popovers, the mortician).
- **Put back what you change, by RAII,** so a failed `REQUIRE` puts it back too.
- **Process-lifetime state a case is allowed to make**, such as a cache filled once or a worker thread made once, is accepted with `ll_test::StateGuard::accept()` as it is made.
- **Process-wide fixtures have one owner and an explicit end.** The headless UI (`ll_test::HeadlessUI::get()`) and the shared GL context (`ll_test::sharedHeadlessGL()`) are one per process. Each ends through `ll_test::Teardown::add`, run by the main while APR and the log are still up, and never in static destruction.

### Other harness pieces

- **An `LL_ERRS`** throws `ll_test::FatalError` in a test, so `REQUIRE_THROWS_AS(..., ll_test::FatalError)` checks one.
- **A child process of the test's own making:** register it with `AL_TEST_HELPER(name)`, and launch the binary with `ll_test::Helper::argument(name)`. The main then runs only that helper.
- **The running case's name:** `ll_test::currentTestName()`.

## Binaries

```
al_add_test_binary(<name> PROJECT <project> SOURCES <test file>...
                   [UNIT <file>...] [LIBRARIES <target>...] [SUPPORT <object library>...]
                   [INCLUDES <dir>...] [DEFINES <define>...] [ENVIRONMENT <VAR=value>...]
                   [GL] [PYTHON] [ISA_TIER <tier>] [RESOURCE_LOCK <name>...] [LAUNCHER <command>...]
                   [RERUN <run> RERUN_SOURCES <file>... RERUN_ENVIRONMENT <VAR=value>...]
                   [BENCH_SOURCES <file>...] [COMBINED])
```

A binary is a link set. Its files share their libraries, their fakes and their defines. The comment above the function in `ALAddTestBinary.cmake` is its reference. In short:

- **`UNIT`** compiles the library's own files into the binary in place of linking the library, for a test that stubs what they call. Such a test is a binary of its own.
- **`SUPPORT`** names object libraries linked whole: fakes and checks. A case or listener in a static archive would be dropped by the linker.
- **`GL`** labels the entries `gl` and registers them disabled with `-DAL_ENABLE_GL_TESTS=OFF`. **`PYTHON`** gives them the interpreter, and registers them disabled without one that has `llsd`.
- **`ISA_TIER`** builds the binary at an x86-64 tier, and it skips on a host below it.
- **`RERUN`** runs some files a second time with more environment. alscript's SLua tests run again under Luau's new solver.
- **`<binary>/stray_cases`** fails when a case belongs to no file's entry, such as one written in a header.
- **`<binary>/combined`** runs every file in one process. It is labelled `combined_pending`, outside the default run, until the binary's files are known to share a process; `COMBINED` then adds it to the default run.

## Benchmarks

A benchmark file goes in its library's binary under `BENCH_SOURCES`. It gets one entry, `bench/<binary>/<stem>`, labelled `benchmark` and `manual`, which `BUILD_AND_RUN_BENCHMARKS` runs (`ctest -L benchmark -V`). Each entry runs alone and reports to the console and to `Testing/benchmarks/<binary>_<stem>.xml`.

```cpp
#include "altestbenchmark.h"

namespace
{
    TEST_CASE("alregex_bench: group members with a date read from each row", "[alregex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const ALRegex     expression("([0-9]{1,2})/([0-9]{1,2})/([0-9]{4})");
        const std::string when = "09/23/2026";
        BENCHMARK("a date's pattern compiled once, re2")
        {
            ALRegexMatch result;
            return expression.match(when, &result) ? result.length(3) : 0;
        };
    }
}
```

- **Tag every case `[!benchmark]`.** It hides the case from every test run, and `ll_test::skipUnlessOptimised()` skips it outside an optimised build.
- **Return what the body made**, or pass it to `Catch::Benchmark::keep_memory`, so the optimiser cannot drop the work.
- **Setup goes before the `BENCHMARK`.** Catch2 calls the body many times: a warm-up, an estimate, then 100 samples. Each call must measure the same thing, so a body that changes what it measures puts it back, or alternates through a counter it captures: a character typed, then taken away.
- **Keep the measured code's surroundings as the real code has them.** A kernel run over arrays in a static reloads their bounds for every element, since any store might have moved them. Give each call local views.
- **Name each benchmark** for what one call covers: `"setAllDot3, 4,096 elements, cached"`. The comma rule is for case names; benchmark names are not test specs.

## Upstream tests

Linden Lab's own tests are TUT. One that arrives in a merge is converted by hand, to the rules above. `scripts/testing/tut2catch.py` did the bulk conversion, but it reads each file through clang with its build flags and TUT's headers, so it runs only in a checkout from before TUT was removed.
