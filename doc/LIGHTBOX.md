# Lightbox maintainer guide

How to add a new effect section to the Lightbox floater. The floater is built
so that exposing a new post-processing effect through the usual rows is
(almost always) a pure XUI change: no new C++, no layout mathematics beyond the
rules below, resets and live preview for free.

Richer controls — colour wheels, graphs, the eyedropper, the scopes — do have
C++ behind them, written once each and then reused from XUI like any other row.
Sections 4b to 4d cover those; if you only need sliders and checkboxes you can
skip them.

## Architecture in brief

The Lightbox is a *window onto settings the renderer already watches*. Every
row binds a widget to a `gSavedSettings` key via `control_name`; the renderer
reads those keys on the frame path (`LLCachedControl` or signal-refreshed
statics), so edits preview live with no glue code.

| Piece | File |
|---|---|
| Floater shell + Looks bar | `indra/newview/skins/default/xui/en/floater_lightbox_settings.xml` |
| Look tab (color science) | `.../panel_lightbox_look.xml` |
| Lens tab (optical/film effects) | `.../panel_lightbox_lens.xml` |
| Scene tab (quality/performance) | `.../panel_lightbox_scene.xml` |
| The C++ (callbacks, Vec3 binder, section reset, Looks bar) | `indra/newview/alfloaterlightbox.{h,cpp}` |
| Looks preset system + whitelist | `indra/newview/llpresetsmanager.{h,cpp}` |
| Bundled starter Looks | `indra/newview/app_settings/looks/` |
| Colour wheel widget + its maths | `indra/newview/alcolorwheel{ctrl,model}.{h,cpp}` |
| Curve/band graph widget + its maths | `indra/newview/alcurve{editorctrl,model}.{h,cpp}` |
| Scopes floater + measurement | `indra/newview/alfloaterscopes.{h,cpp}`, `alscopedata.{h,cpp}` |
| White-balance map and its inverse | `indra/newview/alwhitebalancesolver.{h,cpp}` |
| Scene colour picker tool | `indra/newview/altoolscenepicker.{h,cpp}` |

**The C++ does not grow per effect. It does grow per new *kind of control*.**
That is the real contract, and the distinction matters when you plan work:

- Exposing another slider, checkbox, dropdown, colour or vector row is still
  pure XUI. Nothing below the line changes.
- Introducing a control that has never existed here before — a wheel, a graph,
  an eyedropper — is a widget plus, usually, a floater-side callback. Those are
  written once and then reused by name like any other row.

The floater's C++ provides:

- `LightBox.ResetControlDefault` — per-row reset; `parameter` = setting name.
- `LightBox.ResetSection` — data-driven section reset; `parameter` = `sec_<id>`.
  It walks the panels named `sec_<id>` and `sec_<id>_adv`, collects every
  descendant's bound control (plus settings named by `vec3_*` spinners), and
  resets them. New rows enroll automatically.
- `LightBox.CommitVec3` — the component binder for Vector3/Color3 settings,
  driven entirely by the widget naming contract `vec3_<SettingName>_<0|1|2>`.
  `postBuild` discovers the widgets, seeds them, and keeps them synced with
  the control both ways. Partial exposure is supported: expose only the
  meaningful components and the binder preserves the rest on write.
  **Any `LLUICtrl` will do** — the name is the whole contract, so a bank of
  related values can be sliders rather than spinners. Prefer sliders for a
  bank: eight hue sliders in a column read as a shape, and you can see at a
  glance that the warm end has been pulled and the cool end left alone. Eight
  spinners read as a form to be filled in.
- `LightBox.CommitToneCurve` / `LightBox.RefreshToneCurve` — the tone curve
  graph's handle commit and its channel-selector refresh.
- `LightBox.CommitSplitToneGraph` — the split-tone band graph's handle, which
  writes `RenderSplitToneBalance`.
- `LightBox.PickWhiteBalance` — arms the eyedropper.
- The Looks bar and tonemapper-row greying (effect-specific, already done).

The last four are examples of the per-control cost: a graph or a tool needs
something to interpret its input, so it gets one callback and one `setup*`
call in `postBuild`. Both graphs follow the same shape — `setupX` connects to
the settings' signals and calls `refreshX`; `refreshX` rebuilds the plot and
handles from the settings; `onCommitX` writes the setting and calls `refreshX`
again behind a re-entry guard. Copy that shape rather than inventing another.

## Adding a section: step by step

### 1. Declare the settings

Add keys to `indra/newview/app_settings/settings_alchemy.xml` with a good
`Comment` — write the valid range and what values mean into it. The comment is
the source for the row's `min_val`/`max_val`/tooltip, and Debug Settings shows
it too. Match the code's defaults if the setting is read with an inline
fallback (an undeclared key read by `LLCachedControl` works but does not
persist — declare everything you expose).

### 2. Choose the tab and shape

- **Look** = color science (tone, grading). **Lens** = optical/film effects.
  **Scene** = render quality and performance.
- Essentials (2-4 knobs) in the main section; long tail in a *sibling*
  accordion tab named `atab_sec_<id>_adv` titled `"<Section> - Advanced"`.
- **Fold instead of splitting** when the advanced tail is small (~2-3 rows) or
  the essentials are trivial (one slider) — one section, no sibling.
- Enum-valued settings are dropdowns, never bare int sliders. Sliders whose
  change handler reallocates GPU resources per tick should also be dropdowns
  with a few sane steps (see shadow resolution scale: x2/x1/x0.5).
- Multi-component settings get semantic labels, not X/Y/Z (see the SSAO
  "Occluded value"/"Occluded saturation" rows).
- Debug-only toggles (dither, buffer formats, GL context flags) stay out of
  the floater; Debug Settings is their home.

### 3. Copy the section skeleton

```xml
<accordion_tab
 expanded="false"
 layout="topleft"
 height="{PANEL_HEIGHT + 29}"
 name="atab_sec_myfx"
 title="My Effect"
 fit_panel="true">
    <panel
     follows="all"
     layout="topleft"
     height="{PANEL_HEIGHT}"
     left="0"
     top="0"
     right="-1"
     name="sec_myfx">
        <!-- rows -->
        <!-- final row: the section reset -->
        <button
         follows="top|right" layout="topleft" height="18" width="100"
         right="-8" top_pad="10" label="Reset All" halign="left"
         scale_image="true" image_overlay="Refresh_Off"
         image_overlay_alignment="right" name="sec_myfx_reset"
         tool_tip="Reset this section (including Advanced) to defaults">
            <button.commit_callback function="LightBox.ResetSection" parameter="sec_myfx" />
        </button>
    </panel>
</accordion_tab>
```

The essentials section's Reset All resets the Advanced sibling too (the walker
includes `sec_<id>_adv`); the sibling's own button uses `parameter="sec_myfx_adv"`.

### 4. Rows

First row uses `top="8"`; later rows chain with `top_pad`. Every value row gets
an 18px reset glyph. Copy these verbatim and edit names/keys/ranges:

**Scalar (slider) row** — `top_pad="9"` between slider rows:

```xml
<slider
 follows="left|top|right" layout="topleft" left="8" top_pad="9" right="-32"
 height="16" label="Strength" label_width="140" can_edit_text="true"
 decimal_digits="2" increment="0.01" min_val="0" max_val="1"
 name="myfx_strength" tool_tip="..." control_name="RenderMyFxStrength" />
<button
 follows="top|right" layout="topleft" height="18" width="18" right="-8"
 top_pad="-17" scale_image="true" image_overlay="Refresh_Off"
 image_overlay_alignment="center" name="myfx_strength_rst" tool_tip="Reset to default">
    <button.commit_callback function="LightBox.ResetControlDefault" parameter="RenderMyFxStrength" />
</button>
```

**Checkbox row** (no reset glyph): `check_box` with `control_name`,
`top_pad="10"` after a button, `top_pad="8"` after another checkbox.

**Enum dropdown row**: label `text` (width 140) + `combo_box` at
`left_delta="140" top_pad="-16" right="-32" height="18"` with integer
`combo_box.item` values + reset button at `top_pad="-18"`. Float-valued combos
work only with values whose `%lg` stringification is exact ("2", "1", "0.5").

**Color row** (0-1 tints only): label `text` (`top_pad="12"`) + `color_swatch`
at `left_delta="140" top_pad="-18" width="60" height="24"` with
`can_apply_immediately="true"` and **`label_height="0"`** (without it the
default label strip leaves ~1px of color) + reset at `top_pad="-21"`. Color3
alpha handling lives in the widget — nothing else needed.

**Vector row**: label `text` (width 110, `top_pad="10"`) + spinners named
`vec3_<Setting>_<0|1|2>` at `left_delta="110" top_pad="-16" width="68"
height="18"` (then `left_delta="72" top_delta="0"`), each with per-axis
`min_val`/`max_val` and `<spinner.commit_callback function="LightBox.CommitVec3" />`,
+ one reset glyph (`top_delta="0"`, parameter = the setting). Setting names
contain no underscores, so the name parse is unambiguous. Omit components that
are unused — label the ones you keep by meaning.

A **bank** of the same component across many settings is the other shape this
supports, and it wants ordinary slider rows rather than the compact spinner
layout: one section per component, one slider per setting, named
`vec3_<Setting>_<n>` with a `<slider.commit_callback>`. Nothing but XUI is
involved either way.

**Group headers** (inside a long Advanced panel): a `view_border`
(`bevel_style="none" height="0"`, `top_pad="12"`) then a bold `text`
(`font="SansSerifBold"`, `top_pad="6"`); the first group needs no border.

### 4b. The richer widgets

Three widgets exist beyond the standard rows. Each is one XUI tag.

**`color_wheel`** — a hue ring with a draggable puck, a master slider and three
editable channel fields, all driving one Vector3 setting.

```xml
<color_wheel
 follows="left|top" layout="topleft" left="8" top="8" width="120" height="175"
 label="Lift" centre="0" min_value="-0.5" max_value="0.5"
 name="wheel_lift" tool_tip="..." enabled_control="RenderColorGrade"
 control_name="RenderColorGradeLift" />
```

- Binds through plain `control_name`, **not** the `vec3_*` contract: the widget
  handles a three-element LLSD array in `setValue`/`getValue`, which is all
  `LLUICtrl::setControlVariable` needs to wire both directions.
- `centre`/`min_value`/`max_value` describe the setting: lift is 0 over
  [-0.5, 0.5]; gamma and gain are 1 over [0.5, 1.5]; a split-tone tint is 0.5
  over [0, 1] **plus `lock_master="true"`**, because the renderer divides each
  tint by `dot(tint, LUMA)` so its magnitude cancels — a master there would be
  a control that does nothing.
- A bank of three at 120px wide with a 4px gap fits the accordion at the
  floater's minimum width. Lefts 8 / 132 / 256.

**`curve_editor`** — a graph with draggable handles, used for the tone curve and
the split-tone bands. It owns no curve: a consumer hands it a sampling function
and a handle list, which is why one widget serves both.

```xml
<curve_editor
 follows="left|top|right" layout="topleft" left="8" top_pad="10" right="-8"
 height="72" grid_divisions="4" draw_diagonal="false"
 name="split_tone_graph" tool_tip="..." enabled_control="RenderColorGrade">
    <curve_editor.commit_callback function="LightBox.CommitSplitToneGraph" />
</curve_editor>
```

- `setCurve` plots one solid curve, `addGhostCurve` dimmed references,
  `addFillCurve` a filled area down to y=0 for coverage plots like the bands.
- Handles carry `mLockX`/`mLockY`. **Read those as "this axis cannot change".**
  A handle that slides horizontally to set a value locks *Y*.
- `draw_diagonal="true"` draws the identity, which is meaningful for a tone
  curve and meaningless for anything else.

**Graph maths belongs in the model, next to a test.** `ALCurveModel` mirrors the
shader's own functions, and `alcurvemodel_test` transcribes the GLSL
independently and compares. A graph that merely illustrates the shader is worse
than none: it will be believed. If you plot something new, transcribe it.

### 4c. Tools

Three controls act on something other than a setting.

- **Eyedropper.** A `button` calling `LightBox.PickWhiteBalance` installs
  `ALToolScenePicker` as a transient tool. On mouse-up it asks
  `LLPipeline::requestScenePixel`, which reads the linear scene buffer in
  `renderFinalize` **before** `colorCorrect` and answers on the next frame.
  `ALWhiteBalanceSolver` inverts the renderer's own temperature/tint map — that
  map lives in the solver and pipeline.cpp calls it, so the two cannot drift.
  Sample somewhere else and reuse the same route; do not add a second readback.
- **Hold-to-compare.** `LLPipeline::sGradeBypass`, driven by the bindable
  `grade_bypass_key` action. If you add another temporary "show me without it"
  affordance, follow the same rule: **never toggle a whitelisted setting to do
  it**, or the active Look goes dirty and can be saved mid-comparison.
  Registered *global*, which it has to be — a normal in-world action does not
  fire while the Lightbox has focus, and that is exactly when you want the
  comparison. The cost is that global bindings are dispatched before view
  handling and are non-consuming (`llviewerwindow.cpp`, "like voice"), so they
  fire while a text field has focus too. Bind it to a function key or a mouse
  button; a letter would flash the grade every time that letter is typed.
- **Per-section bypass.** `LLPipeline::sGradeBypassMask`, one bit per group,
  driven by the `LightBox.ToggleBypass` checkboxes in the Color Grading section.
  A set bit makes `colorCorrect` upload that group's **identity** values instead
  of its settings, which lands in the early-out the shader already has for that
  step — so this needed no new uniform, no new variant and no recompile, and a
  bypassed section costs slightly *less* than an active one. If you add a
  grading step, add its identity to that block or the bypass will quietly skip
  it.
- **Reference still.** `LLPipeline::requestReferenceStill` grabs the frame about
  to be presented; `RenderReferenceWipeMode` then wipes the live image against
  it. Where hold-to-compare shows you *no* grade, this shows you the grade you
  had ten minutes ago, which is the comparison that matters once a look has
  taken more than a moment to build.
  - Grabbed at the same point the scopes sample — after every post pass, before
    the print effects — and substituted **before** those effects in
    `blitWithEffectsF.glsl`, so vignette and grain land on both sides of the
    seam. The comparison is then about the grade rather than the print
    treatment.
  - The mode is forced to zero unless a still exists, which is what lets the
    shader sample the reference without checking.
  - Both settings are `Persist=0`: a still cannot outlive the session, so a
    mode that did would come back pointing at nothing.
  - A resize drops the still. Sampling a still of one resolution against a
    frame of another would stretch it, and a reference you cannot trust
    geometrically is worse than none.

  The groups match Reset All's grouping (`sec_<id>` plus `sec_<id>_adv`
  together), so there is only one idea of "a section" to learn. The toggles sit
  together rather than one per section header, because A/B work means flipping
  between them and hunting through collapsed accordions to do that is worse than
  a row in the one section that is about the chain as a whole.

The first two reach across a frame or a click; the third outlives the floater
outright. **Anything deferred like that must capture an `LLHandle`, never
`this`** — the Lightbox declares neither `single_instance` nor `reuse_instance`,
so closing it *destroys* it, and an armed picker holding a raw pointer is a
use-after-free with no window of luck involved.

**And anything that parks state outside the floater must clear it in the
destructor.** The bypass mask lives in the pipeline; left set, a closed Lightbox
would leave a section suppressed with nothing on screen to say so and no setting
to inspect. That is harder to diagnose than a crash. The reference still is the
same rule with a second reason — it is a full-resolution target, so leaving one
behind holds real memory for a comparison nobody can see or switch off any more.
`~ALFloaterLightBox` clears all three.

### 4d. Scopes

The Scopes floater is a separate window on purpose — a scope inside a Lightbox
tab is hidden behind whichever tab you are editing. `ALScopeData` does the
measuring with no GL or UI, so `alscopedata_test` can exercise it: four
histogram channels, a two-dimensional chroma grid for the vectorscope, and a
per-column grid for the waveform and parade.

Each scope answers a different question, and that is the reason to have three:
a histogram says **how much** of the frame is at a level, a waveform says
**where** it is (a blown sky and a blown face are the same histogram bin and
obviously different waveforms), and a vectorscope says **what colour**.

Because they answer different questions, the floater shows up to four at once.
`AlchemyScopeLayout` picks the arrangement (single, two side by side, two
stacked, four in a grid) and `AlchemyScopePane0` through `AlchemyScopePane3`
say what each pane holds; right-clicking a pane sets its own. `computePaneRects`
is the single place that divides the plot area, and it returns rects in the
floater's coordinate space — the same space `draw()` paints in and
`handleRightMouseDown` is handed — so the rect that drew a pane is the rect
that hit-tests it. If those ever diverge, the menu opens on the wrong pane.

The draw functions therefore take `(mode, rect)` rather than reading the mode
and the panel themselves. Anything new must too: a scope that reaches for
`mPlotPanel->getRect()` draws over all four panes.

**Adding a scope is three edits, not one.** A new `EMode` needs an entry in
`modeStringName`, a `floater.string` for its corner label, and an item in
`menu_scopes_pane.xml` — the menu is hand-written rather than generated from the
enum, so a mode added without one is reachable only by editing the setting.
Keep `MODE_COUNT` last: `getPaneMode` clamps against it, and that clamp is what
stops a stale setting from indexing off the end.

**A button that opens another floater wants `Floater.Toggle`.** Not
`Floater.ToggleOrBringToFront`, which is written for toolbar buttons: it closes
its target only after falling through `else if (!instance->isFrontmost())`, and
pressing a button inside a floater makes *that* floater frontmost, so the close
branch is unreachable. The button then opens and raises its target and can never
shut it. Both are global commit callbacks registered in `llui.cpp`, so such a
button needs no C++ at all.

**Spawn an `LLContextMenu` with `show()`, never `LLMenuGL::showPopup()`.**
`LLContextMenu` overrides `setVisible` to ignore everything except `false`:

```cpp
void LLContextMenu::setVisible(bool visible) { if (!visible) hide(); }
```

`showPopup`'s only attempt to reveal a menu is `setVisible(true)`, so against a
context menu it does nothing — silently. The menu still loads, parents,
populates and resolves its callbacks, so there is no warning in the log and
nothing to find at the point of failure; it simply never appears. Copying the
spawn code from a view that uses a plain `LLMenuGL` (`llnetmap` is the obvious
one to reach for) walks straight into this, because that call is correct
*there*. `LLContextMenu::show` also does its own arranging and edge-flipping,
so it replaces `showPopup` rather than joining it.

Its coordinates are **screen** space — it calls `screenPointToLocal` internally
— while `handleRightMouseDown` is handed coordinates local to the view. Convert
with `localPointToScreen` or the menu opens in the wrong place on any window
that is not at the screen origin, which is easy to miss when testing maximised.

Two rules if you add another:

- The vectorscope bins through `ALColorWheelModel::toChroma`, the same basis the
  wheels edit. That is load-bearing: push a wheel and the trace must move the
  same way. Go through that function rather than copying the basis.
- Anything per-column must normalise by **that column's own pixel count**, not
  by the sample height. The sample's width rarely divides the grid evenly, so
  columns cover unequal numbers of source columns, and a sample narrower than
  the grid leaves some empty.

Two costs to know about. The waveform grid is a quarter of a megabyte, so it
lives in a `std::vector` and not in the object — `captureScopeSample` builds a
whole `ALScopeData` as a **stack local**, and a member array that size would put
it on the stack every capture. It is also empty until something is measured, so
a viewer whose scopes have never been opened pays nothing.

Note what the pane layout does *not* cost. `accumulate` fills every channel, the
chroma grid and the waveform grid on each capture whatever is displayed, so a
fourth pane adds drawing and nothing else — no extra sampling, read-back or
binning. Drawing is where it is lopsided: a histogram is `BIN_COUNT` bins, but a
waveform is `WAVE_COLUMNS * WAVE_LEVELS` cells **per channel**, so a parade pane
is worth roughly two hundred histograms. Four is the ceiling for that reason,
not because the tiling could not go further.

And capture is gated on the floater existing (`LLPipeline::sScopeCapture`), so a
closed window costs nothing at all. That gate is worth reusing: the cursor
readout gets its value from `LLPipeline::getScopePixel`, which is a lookup into
the sample `captureScopeSample` already took rather than a second readback. If
you want a value off the frame and can accept it being one sample interval old
and point-decimated, take it from there rather than adding another `glReadPixels`.

### 4e. Adding a grading step to the shader

The post shaders are separate GL shader objects linked together:
`colorCorrectF.glsl` calls helpers that live in `colorGradeUtilF.glsl`,
`tonemapUtilF.glsl` and `postEffectUtilsF.glsl`. **GLSL requires a declaration
before use within each object**, so a new helper needs *two* edits to the
caller: the call in `main()`, and a forward declaration in the block at the top.

Forget the declaration and the file does not compile — **at runtime, on the
user's machine**. The C++ builds clean, every unit test passes, and the viewer
then fails to link the shader, binds a null program and dies on the first frame
with an access violation in `LLGLSLShader::bind`. There is no build-time signal
at all; this has happened.

So: after touching any `*F.glsl` under `shaders/class1/alchemy/`, either launch
the viewer or run the declaration checker (`check_glsl_decls.py` in the
scratchpad), which cross-references every call against what the attached util
files define. Nothing in `cmake --build` or `ctest` covers shaders.

**A new uniform is the other half of this, and it fails even more quietly.**
`LLShaderMgr::mReservedUniforms` maps an enum index to a name *string*, and
nothing checks that string against the shaders. A typo, or an array written as
`uThing[0]`, produces no error at all: `mapUniform` never records a location,
every upload silently does nothing, and the shader reads the uniform as zero.
That surfaces as a rendering fault — a black screen, in the case that prompted
this — a long way from the cause.

Two rules follow. **Declare an array uniform by its bare name**, with no `[0]`:
`mapUniform` strips the subscript from whatever GL reports before matching, so a
name carrying one can never match. (`initAttribsAndUniforms` now refuses such a
name outright, next to the size-sync and duplicate checks.) And run
`check_glsl_uniforms.py`, which verifies every `uCamelCase` entry in the table is
actually declared by some shader — it catches both the subscript case and plain
typos.

### 4f. Undo

`ALGradeHistory` (tested by `algradehistory_test`) plus the wiring in
`ALFloaterLightBox`. **A new grading setting gets undo for free by being on the
Looks whitelist** — the floater watches exactly `getLooksControlNames()`, on the
principle that a setting worth saving into a Look is one worth undoing, and one
list means a new control cannot join one and miss the other.

The recorder needs no shadow copy of the settings: `LLControlVariable`'s commit
signal carries the **previous** value as its third argument, and only fires when
the value actually changed (`setValue` and `resetToDefault` both gate on
`llsd_compare`), so a commit that rewrites the same value cannot leave a
do-nothing step on the stack.

Two rules if you add a control:

- **One user gesture must be one step.** A drag emits a commit per mouse-move,
  and the history collapses those by coalescing successive writes *to the same
  control* inside 500 ms. Every interactive control here writes exactly one
  setting per commit — the tone-curve graph picks toe *or* shoulder *or*
  strength, the band graph writes only balance — which is what makes that
  enough. **A control that wrote two settings per commit would defeat it**, and
  produce one undo step per setting per mouse-move. If you need one, the fix is
  in `ALGradeHistory`, not in the caller.
- **A discrete action that moves many controls needs `ScopedHistoryGroup`.**
  Reset All, applying a Look and Look revert each wrap one, so they undo in a
  single step. Note that a group deliberately does *not* coalesce, so it must
  only ever wrap a discrete action — wrapping a per-move commit in one would
  give you back the hundred-step drag.

Undo restores **values only**. The active Look stays dirty, exactly as it would
had the user typed the old numbers back in; restoring that faithfully would mean
modelling the Looks system's history too. The stack is a floater member, so it
dies with the window rather than outliving it to rewrite a later session's edits.

Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z are bound in `handleKeyHere`, **floater-local and
not a global action** — the opposite choice from hold-to-compare, and for the
opposite reason: a global Ctrl+Z fires while the user is typing anywhere in the
viewer. Being handled there also means a focused text field keeps Ctrl+Z for its
own undo, since the focus chain is offered the key first.

The Undo and Redo buttons in the top bar are the visible half of that, and they
exist because **the Looks buttons were turned into icons to pay for them**: four
labels cost 192px of a bar with 412 to spend at `min_width`, the same four icons
cost 80px, and the pair fits in the change with room over (36px of slack became
92px). Icons also sidestep §5's silent clipping if this floater is ever
translated, where "Save As" becomes "Speichern unter". Both buttons, and the
reference row below, are greyed from `draw()` rather than from a signal — the
undo stack moves on every commit, every undo and every Look apply, and hanging a
refresh off each of those is more places to forget than a polled compare costs.

### 5. Height math (the part everyone gets wrong)

- `accordion_tab` height **must be** inner panel height **+ 29**
  (25px header + 2+2 padding). The tab's rect *is* its expand height and
  `fit_panel` squeezes the panel into what remains; an undersized tab clips
  the bottom rows and the overflow draws over the sections below (panels do
  not clip children).
- Compute the panel height by walking the `top_pad` chain to the **last
  widget's bottom**, then add 8. `top_pad` chains from the *previous widget*,
  which for a slider row is its reset button (18px tall, hanging 1px below the
  16px slider) — so slider+reset rows pitch **26px**, not 25.
- Worked example: slider row at `top="8"` (slider 8-24, button 7-25), second
  slider `top_pad="9"` (34-50, button 33-51), Reset All `top_pad="10"`
  (61-79) → panel height 87, tab height 116.
- **Side-by-side widgets break the chain.** A bank of three wheels all use
  `top="8"`, so the bank's bottom is *one* wheel's height, not three; the next
  row's `top_pad` chains from the last one declared. Get this wrong and the
  panel is either 350px too tall or clipped.
- Widths never reflow. Usable inner width is the floater's width − 28, and
  ~15px less again whenever the accordion's scrollbar shows. **Overflow clips
  silently, with no scrollbar and no warning**, so check the narrowest case:
  the floater at its `min_width` *with* the vertical scrollbar visible.
- `min_width` is the only declarative way to widen an existing user's floater
  (`LLFloater::applyRectControl` prefers a saved rect over the XUI width), and
  it force-widens everyone permanently. Design to the current width instead.

### 6. Gating

`enabled_control="SomeBool"` / `disabled_control="SomeBool"` on each dependent
widget greys it live (they connect to the control's signal). Boolean controls
only; apply per row, not on the parent panel. Reference patterns:

- HDR fork: bloom rows `enabled_control="RenderHDREnabled"`, legacy glow rows
  `disabled_control="RenderHDREnabled"` — greying, not visibility, so the
  layout never gets holes and both modes stay discoverable.
- **`RenderColorGrade` is the master switch for the entire grading suite**
  (LUT *and* Basic Grade, White Balance, Split Toning, Lift/Gamma/Gain, Tone
  Curve). Any new grading control must gate on it or it will look inert.
- Int-selected modes can't gate declaratively; either leave rows enabled with
  a "(X only)" tooltip or add a small signal handler like
  `updateTonemapperRows()`.
- **`gSnapshotNoPost` gates the renderer, not the UI, and it is easy to miss.**
  The snapshot floater's "No post-processing" box has to mean it, so a new
  *print* effect must check it wherever its strength is uploaded — there are
  `clean_plate` gates in **two** places, because post-grade is two passes.
  Effects in the final blit (vignette, grain, CVD, the preview modes) gate in
  `renderFinalize`; effects applied *inside* the colorCorrect program
  (chromatic aberration, lens flare) gate in `colorCorrect`, because they run
  in every variant including the no-post ones — those two leaked through the
  first time for exactly that reason. The one deliberate exception is dither,
  which is a quantisation aid rather than a look and which an 8-bit PNG wants
  either way.

### 7. Slider text width

Any slider with `max_val` below 1.0 (or ≤ 0) **must** set an explicit
`text_width` (56 fits a signed 4-decimal value). Without it `LLSliderCtrl`
auto-sizes the value box from `log10(max_value)` and truncates the number.

### 8. Cadence and tooltips

- Most keys are read per-frame: live preview, nothing to say.
- Keys wired to reallocation/rebuild handlers in `llviewercontrol.cpp` still
  apply automatically but hitch — say so: "Changing causes a brief hitch." /
  "Toggling rebuilds shaders (brief hitch)."
- Keys with **no** handler need a restart note, or better, don't expose them.
- Check with: `grep <Key> indra/newview/llviewercontrol.cpp`.

### 9. Looks whitelist

If the new effect is **aesthetic** (Look/Lens material), add its keys to
`getLooksControlNames()` in `indra/newview/llpresetsmanager.cpp` — the single
source of truth for save, dirty-watching, and the whitelist-filtered apply.
Skip it and Looks silently won't carry the effect. Do **not** add: Scene-tab
keys, `Persist=0` keys, structural buffer-shape knobs, or debug toggles.
A startup `LL_WARNS("Presets")` fires for whitelist names that stop existing,
so renames get caught.

Bundled starter Looks live in `app_settings/looks/` as full whitelist
snapshots ({Comment, Persist, Type, Value} per key, URI-escaped filenames).
**Add your keys to all three at their defaults**, or applying a bundled Look
will leave the new effect at whatever the user had rather than clearing it —
`loadLooksPreset` writes only keys present in *both* the whitelist and the file,
so a missing key is a silent no-op and "Neutral" stops meaning neutral. To
refresh them after tuning: save the Look in the viewer, then copy the saved file
from `<user_settings>/presets/looks/` over the bundled one.

A Look whose settings have since changed is shown as `Name *` in the combo, via
the `look_name_modified` string — **on the name, not beside it**. A detached
marker can only say that something somewhere has changed; an attached one says
which Look it was. It is display-only (`setLabel`, and nothing is selected while
modified), and `onLookSelected` reads the chosen item's own text, so the marker
cannot travel into a Look name.

Seeding is recorded per name in `looks_seeded.xml` (user settings root, *not*
the looks directory — anything `*.xml` in there is enumerated as a Look), so a
Look bundled in a later release still reaches an existing user while one they
deleted stays deleted. Nothing is ever copied over a file that already exists.

### 10. Verify

- XML well-formedness before launching (any XML-capable tool).
- Two-way binding: move the row, watch the key in Debug Settings; edit the key
  there, watch the row follow.
- Gating flips live; section Reset All touches exactly the section's keys
  (including its Advanced sibling); the tab opens to full height with nothing
  clipped or drawing over the next section.
- If added to the whitelist: save a Look, change the setting (dirty `*`
  appears), re-apply (value returns).
- For a wheel: drag the puck and watch the fields and Debug Settings follow;
  type a value and watch the puck move to match. Drag hard into a corner and
  confirm it rides the reachable boundary rather than freezing or jumping, and
  that the number shown is the clamped one.
- For a graph: drag each handle to its limit and confirm the setting clamps and
  the handle is put back where the setting actually landed.
- For anything measured (scopes, vectorscope): change the thing it measures and
  confirm the readout moves the way the control says it should.
- For a bypass: tick it and confirm the image changes **and the Looks `*` does
  not appear**. Then close the Lightbox with it still ticked and confirm the
  render comes back — state parked outside the floater is the failure mode here,
  and it looks like a renderer bug rather than a UI one.
- If you added a print effect: take a snapshot with "No post-processing" ticked
  and confirm the effect is absent from the saved file, not just from the
  preview.
- **Developer-build staging trap:** non-package builds do not reliably restage
  XUI or `app_settings` next to the executable, and the rule is worth knowing
  rather than guessing at, because a stale copy looks exactly like an edit that
  did not work.

  The copy is a `POST_BUILD` custom command on the **viewer binary target**
  (`viewer_manifest.py --actions=copy`, `newview/CMakeLists.txt`). So:

  - **Edited an existing XUI/settings file and nothing else?** No C++ changed,
    so the exe does not relink, so `POST_BUILD` never runs and the staged copy
    stays stale *however many times you build*. Copy the file into
    `build-.../newview/<config>/skins/...` or `.../app_settings/...` yourself.
  - **Added a new file?** The manifest's file list is built from globs at
    **configure** time, so it is not staged at all until you re-run CMake —
    a rebuild alone will not find it.
  - Editing XUI alongside C++ hides both cases, because the relink drags the
    copy along with it. That is why this bites on the one change that happened
    to be XML-only.
