# Settings schema guide

How to add, change, rename, delete or split a setting, and what every message
the build gives about one asks of you. Settings are declared once, in
`indra/cvars/<area>.toml`; `alcvargen` compiles them into handles
(`cvar::render::draw_distance()`), descriptors and `cvars.json`, and the lock,
`indra/cvars/cvars.lock`, records what each setting's stored values have meant,
so that every user's values are brought forward whatever the schema does next.

The one rule under all of it: **a line of the lock, once committed, stays.** A
step that may have shipped is put right by a step after it, never edited or
taken out. Everything below follows from that.

## Architecture in brief

| Piece | File |
|---|---|
| The declarations, one file an area | `indra/cvars/*.toml` |
| The lock: each setting's steps, a line a step | `indra/cvars/cvars.lock` |
| The generator (`alcvargen --help`) | `indra/cvars/gen/alcvargen.{h,cpp}` |
| How the build runs it, and the lock's target | `indra/cmake/ALCVarGen.cmake` |
| The lock HEAD holds, read through git | `indra/cmake/ALCVarCommittedLock.cmake` |
| The JSON Schema editors check the TOML by | `indra/cvars/cvars.schema.json`, `.taplo.toml` |
| The one conversion every stored value takes | `indra/alcvar/alcvarconvert.{h,cpp}` |
| `settings.db` | `indra/alcvar/alcvarfile.{h,cpp}` |
| What an open would do to a file (`alcvar_preview`) | `indra/alcvar/alcvarpreview.{h,cpp}`, `tools/alcvar_preview.cpp` |
| The tests' schema, and its own lock | `indra/alcvar/tests/schema/` |

The generator runs at configure time, so a fresh checkout's IDE sees the
headers, and again in the build whenever a TOML file, the lock or the
generator changes. A header holds keys and types alone: an edited default,
range or help recompiles one `.cpp`, and nothing that reads the setting.

## A declaration

```toml
[render.shadows.resolution_scale]
type     = "F32"
default  = 1.0
range    = [0.25, 4.0]
ui_range = [0.25, 2.0]
ui_step  = 0.05
unit     = "ratio"
alias    = "RenderShadowResolutionScale"
help     = "Shadow map size as a multiple of the window's"
```

Every field, with what it says, is in the JSON Schema, which an editor shows
as you type (below). `type`, `default` and `help` are required; a UUID also
needs `refers`. `ui_step` is a slider's increment: `step` is the lock's word
alone, and a field of that name is refused.

Names are `area.group.key`, lower_snake, and say what true means for a
boolean (`enabled`, not `disable_x`). The generator refuses C++ keywords,
macros some platform defines (`near`, `far`, `min`), units as suffixes
(`fade_ms`: that is `unit`), origins (`al_`, `fs_`: that is `origin`) and
the generator's own names (`read`, `group`, `all`).

## The six changes

### Add a setting

1. Declare it under its group's header, in its area's file.
2. Build. A local build records the new setting's first step in the lock
   itself, since a first step changes nothing stored, and says so:
   `render.toml:12:1: note: render.glow: declared (step 1); recorded in the lock`.
3. Commit the entry and the lock's new line together.

CI (`AL_CVARS_STRICT`) records nothing: a commit that adds a setting without
its lock line fails there with `behind the schema`.

### Change a default, range, label, help or `ui_*`

Edit it. None of these is a step: the lock is untouched, and stored values
are clamped into a new range as they are read, not rewritten. A value the user
set keeps winning over a new default.

### Change a type, a unit or the choices

1. Edit the entry. The build fails, saying what will happen to stored values
   and what to run:

   ```
   render.toml:80:1: note: render.foo.count: U32 -> F32 (step 3); stored values convert exactly
   cvars.lock:1:1: error: behind the schema; run cmake --build <tree> --target cvars_lock, and commit it with the change
   ```

2. Where nothing converts the old values (a `vec3` to an `F32`, pixels to a
   ratio, a choice remapped), the error says so and what to write: add a
   `migrate` for the step the change will be.

   ```toml
   migrate = [{ step = 3, scale = 0.001 }]                         # pixels of a 1000-wide window
   migrate = [{ step = 3, map = { 1 = 2, 2 = 4 }, else = "keep" }]  # levels doubled
   migrate = [{ step = 3, map = { low = 0, high = 1 }, else = "default" }]  # strings became numbers
   migrate = [{ step = 3, fn = "fog_from_old" }]                   # a registered hook
   migrate = [{ step = 3, reset = true }]                          # old values mean nothing now
   ```

   A `map`'s keys are values of the step before, as it held them (numbers of a
   number, strings of a string), and must be among that step's choices where
   it had any; its values, and its `else`, are values of the new step. `else`
   is `"keep"`, `"default"` or a value. A hook is registered by name with
   `ALCVars::registerMigration`, and `ALCVars::checkHooks` asserts that every
   hook the schema names is.
3. Run `cmake --build <tree> --target cvars_lock` and commit the entry, its
   `migrate` and the lock's new line together.

A choice added is no step. A choice removed or given to another (`was`) is.
A unit named where there was none, or dropped, keeps the numbers as they are.
A `migrate` stays in its entry for as long as its step is in the lock.

**Changed twice before a commit.** `cvars_lock` reads the lock HEAD holds
(through git) and records the steps a setting gained since as one: a type
changed, recorded, and changed again is one step, and a new setting changed
after its first step was recorded is still a first step. A build in between
may ask for a `migrate` the fold will not need; run `cvars_lock` and see. With
no git, or a merge, rebase, cherry-pick or revert under way, nothing is
folded, and each recording is a step of its own. Two `migrate`s for steps the
fold makes one fail with `one change is one step`: keep one that answers the
whole change.

### Rename a setting

1. Change its header to the new name and add the old one as an alias:
   `alias = "render.shadows.detail"`. Keep the entry's `migrate` list.
2. Build: `render.shadows.detail -> render.shadows.casters (step 3); stored
   values follow the rename`. Run `cvars_lock`. The lock gains the new name's
   step, continuing the old chain, and a line closing the old name with
   `renamed_to`.

The alias stays: it is what reads a row stored under the old name, and
dropping it later fails (`keep the alias of the name it was renamed from`).
Its codec (`inverted`, `encoding = "string"`, `joined`, `unit`) is part of the
rename's step, so changing it later is refused as an edit of a recorded step.

### Delete a setting

1. Delete the entry. The build fails: `in the lock but declared nowhere`,
   suggesting the alias to add if a new setting of the same shape looks like
   its new name.
2. Run `cvars_lock`. The lock gains a `retired` line; users' rows are kept and
   ignored, for older viewers.

The name is never another setting's after that: declaring it again needs
`reuse = true`, which begins a history of its own that the old rows never
meet (they take the default). The same holds for a name renamed away.

### Split or merge settings

A setting that takes its first value from others says so with `from`:

```toml
[render.gpu.benchmark_score]
type = "F32"
from = [{ name = "render.gpu_benchmark_result", fn = "gpu_benchmark_score" }]
```

Where the setting has no row of its own, its hook is given each source's row
brought to that source's last step (through the source's own `migrate`s where
it is declared, by the conversions alone where it was retired) and gives the
value, written at the setting's first step. At the XML import the hook is
given the XML's values by name. A source must be in the lock: declared,
renamed or retired. Several settings may name one source. Delete the source
as above once nothing reads it but the split.

## The lock

```
{"name": "world.wind_scale", "step": 1, "decl": "8186477c6bc3b940", "parent": "", "type": "F32", "unit": "", "choices": [], "migrate": ""}
{"name": "world.wind_scale", "step": 2, "decl": "9d7c48642ce2d267", "parent": "8186477c6bc3b940", "type": "F32", "unit": "", "choices": [], "migrate": "11ee2a067decdf02"}
```

Each line is a step: its type, unit and choices, the hash of its `migrate`, and
its parent, the step it follows. `decl`, the step's fingerprint, is a hash of
all of that, and is what a stored row carries; help, defaults, ranges and the
step's number are not in it. The target writes the lock; a line edited by hand
fails (`decl is not its own`). Lines that end a chain carry `retired` or
`renamed_to` instead of a type.

**Branches.** The lock merges by union (`.gitattributes`): branches that
changed different settings merge cleanly. Two that changed one setting leave
two steps after one step, a fork, which the build refuses, naming both
fingerprints. The merged entry keeps one branch's `migrate` list and ends it
with a join, answering each of the other branch's steps by fingerprint:

```toml
migrate = [
  { step = 2, map = { 0 = 1, 1 = 0 }, else = "keep" },
  { step = 3, join = { "b55bd59aa25cf029" = { scale = 0.1 } } },
]
```

Both branches' lines stay, since either may have shipped. A step a release
took by cherry-pick is the same step: its fingerprint is the same on both.

**Forks of the repository.** A fork's own settings are steps of its own lock;
merging upstream later is a branch merge as above. Never take a line out to
make a merge go through.

**CI** checks a pull request's lock against its target branch's
(`alcvargen --check --base`), so a revert that takes a step out fails there
even though the entry was reverted with it.

## Finding a setting

```
alcvargen --where RenderFarClip indra/cvars/*.toml
alcvargen --where render.shadows.detail <tree>/alcvar/generated/cvars.json
```

says where it is declared, its header and its handle; an old name is followed
to its setting. The second form needs no declarations, only a built tree.

## Seeing what an open will do

```
alcvar_preview [--account] <settings.db>
```

opens a copy of the file (never the file itself) and says, a row a line, the
step each row is at, what it becomes and through which steps, or what is left
alone and why: a step this build does not know, a name no setting has, a hook
this build does not register. It writes nothing. It is built with the tests
(`AL_BUILD_TESTS`) against the tests' schema until the viewer's declares
settings; a backup (`.albackup`) has no reader yet.

## Editors

`.taplo.toml` points Taplo, and Even Better TOML in VS Code, at
`indra/cvars/cvars.schema.json`: fields complete with their help, enums
(types, units, scopes, flags, areas) complete, and a misspelled field is
marked as you type. The schema is the generator's own tables, written by
`alcvargen --json-schema indra/cvars/cvars.schema.json`; `alcvargen_tests`
fails when the committed one is behind the generator.

## Messages

Every message is a compiler's: `file:line:col: error: <setting>: <what>`, at
the entry's absolute path, or `file(line,col): error CV0001: ...` under MSVC,
which Visual Studio lists in its error list. A configure prints them as they
are before CMake's own line. Notes say what a change does to stored values;
a misspelled field, unit, type or value says what it nearly is (`did you mean
'default'?`).

| Message | What to do |
|---|---|
| `behind the schema; run ... --target cvars_lock` | Run it, and commit the lock with the change. The notes above it say what each step does. |
| `<A> -> <B> (step n) has no automatic conversion` | Add `migrate = [{ step = n, fn = "..." }]`, or `reset = true`. A map or a scale takes no such value. |
| `<units> do not scale into one another` | Add a `migrate` of step n: `scale`, `map` or `reset`. |
| `the map's key x is not among step n's choices` / `is not a number, as step n holds` | A key is a value the step before held, as it held it. |
| `the map gives x, and step n holds a string` / `which is not among the choices` | A map's values, and its `else`, are values of the new step. |
| `a map says what becomes of a value it does not list` | Add `else = "keep"`, `"default"` or a value. |
| `one change is one step: a migrate here begins step n at most` | A `migrate` names the step the change will be: the lock's last plus one. |
| `a migrate begins a step after the first` | A first step has nothing before it to migrate. |
| `step n is not the step the lock records` | A recorded `migrate`, or a rename's alias codec, was edited or deleted. Put it back; a step after it puts right what was wrong. |
| `step n is step m again with no migrate of its own` | A `migrate` stays in its entry while its step is in the lock. |
| `two steps follow step n: <decl> and <decl>` | A fork: end the entry's `migrate` list with a join answering the other branch's steps. |
| `the join's <decl> is no step of another branch` | A join names the other branch's steps by fingerprint, from the lock. |
| `in the lock but declared nowhere` | Deleted: run `cvars_lock`, which retires it. Renamed: add the old name as an alias. |
| `retired` / `renamed to x; declaring the name again needs reuse = true` | Choose another name, or add `reuse = true`. |
| `reuse = true, but the name was never renamed away or retired` | Remove `reuse`. |
| `its history begins after a step of no old name it declares` | A renamed setting lost its alias: put it back. |
| `a rename continues one old name's history: a and b both have one` | Two aliases each have a chain; a rename continues one. |
| `the old name x was retired` | A retired name's rows are no setting's: drop the alias. |
| `x is not in the lock: a setting takes its first value from one declared, renamed or retired` | A `from` source must be a setting the lock has had. |
| `step n follows a step the lock does not hold` / `is missing from the lock` | A line was taken out. Put it back from git: a line, once committed, stays. |
| `decl is not its own` / `not a line of the lock` | The lock was edited by hand. Restore it and let `cvars_lock` write it. |
| `a setting has no field 'x'` | A field of the schema's, or `did you mean` it. `step` is `ui_step`. |
| `the default ... is outside the range` / `is not among the choices` | Make them agree. |
| `x is not lower_snake` / `is a C++ keyword` / `is a macro` / `'ms' is a unit` / `'al' says where a setting came from` / `booleans name what true means` | Rename it, before it ships. |
| `a UUID says what it refers to` | Add `refers = "sound"`, `"texture"`, ... or `"none"`. |
| `a sensitive setting is backed up only when asked` / `a dangerous setting is in no preset` | Add `backup = "ask"` / `preset = "none"`. |
| `declared before its group` / `no group declares this` / `one file an area` | Declare the group's header first, in its area's one file. |
| `names a file: say which kind with asset = "..."` | Add `asset`, which checks the value is a bare file name. |
| `cannot be read` / `cannot be written` | A path is wrong, or the lock is read-only (checked out by a tool that locks files). |

## The tests' schema

`indra/alcvar/tests/schema/` has a lock of its own and its own target,
`alcvar_test_schema_lock`. Its histories are made on purpose, several steps
in one commit, which `cvars_lock`'s fold would make one: record them with the
generator directly, a stage at a time, with no `--committed`:

```
<tree>/alcvargen-host/bin/alcvargen --write-lock --lock indra/alcvar/tests/schema/cvars.lock \
    indra/alcvar/tests/schema/render.toml indra/alcvar/tests/schema/ui.toml \
    indra/alcvar/tests/schema/chat.toml indra/alcvar/tests/schema/perf.toml
```

The generator's tests (`ctest -L alcvargen`) cover every message above; a new
rule gets a case there that fails without it.
