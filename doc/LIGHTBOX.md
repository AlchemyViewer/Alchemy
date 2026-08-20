# Lightbox maintainer guide

How to add a new effect section to the Lightbox floater. The floater is built
so that exposing a new post-processing effect through the usual rows is
(almost always) a pure XUI change: no new C++, no layout mathematics beyond the
rules below, resets and live preview for free.

Richer controls — colour wheels, graphs, the eyedropper, the scopes, a switch on
a section header — do have C++ behind them, written once each and then reused
from XUI like any other row. Sections 3b and 4b to 4d cover those; if you only
need sliders and checkboxes you can skip them.

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
| Sky tab (environment + client-side sky effects) | `.../panel_lightbox_sky.xml` |
| Day cycle landmark search | `indra/newview/aldaycyclelandmarks.{h,cpp}` |
| The C++ (callbacks, Vec3 binder, section reset, Looks bar) | `indra/newview/alfloaterlightbox.{h,cpp}` |
| Looks preset system + whitelist | `indra/newview/llpresetsmanager.{h,cpp}` |
| Bundled starter Looks | `indra/newview/app_settings/looks/` |
| Colour wheel widget + its maths | `indra/newview/alcolorwheel{ctrl,model}.{h,cpp}` |
| Curve/band graph widget + its maths | `indra/newview/alcurve{editorctrl,model}.{h,cpp}` |
| Scopes floater + measurement | `indra/newview/alfloaterscopes.{h,cpp}`, `alscopedata.{h,cpp}` |
| Scope pane assignment menu | `.../menu_scopes_pane.xml` |
| Undo/redo stack | `indra/newview/algradehistory.{h,cpp}` |
| White-balance map and its inverse | `indra/newview/alwhitebalancesolver.{h,cpp}` |
| Scene colour picker tool | `indra/newview/altoolscenepicker.{h,cpp}` |
| 3D LUT (.cube) parser | `indra/newview/lutcube.{h,cpp}` |
| Header checkbox on `accordion_tab` | `indra/llui/llaccordionctrltab.{h,cpp}` |
| Anti-aliased 2D polyline and fill | `indra/llrender/llrender2dutils.{h,cpp}` |

Seven of those have unit tests, and the tests are the reason the maths in them
can be trusted: `alcolorwheelmodel_test`, `alcurvemodel_test`,
`aldaycyclelandmarks_test`, `algradehistory_test`, `alscopedata_test`,
`alwhitebalancesolver_test`, `lutcube_test`. Anything with arithmetic in it —
including a parser fed files from the internet — belongs on that list.

### What v2 added, and what it altered

Two of these are new XUI tags, one is a new param on a stock widget, and one is a
new pair of drawing primitives everything else is painted with. Nothing else in
the viewer's widget set was touched.

| Thing | Written as | Where |
|---|---|---|
| Colour wheel | `<color_wheel>` | §4b |
| Curve / band graph | `<curve_editor>` | §4b |
| Checkbox on an accordion header | `<accordion_tab.header_check_box>` | §3b |
| Anti-aliased polyline and area fill | `gl_polyline_2d`, `gl_polyfill_2d` | §4b |
| Floater top bar (Looks, history, scopes) | ordinary buttons, `Floater.Toggle` | §4g |
| Scopes window | its own floater | §4d |
| Sky tab (day cycle freeze) | ordinary rows, `LLEnvironment` behind them | §4h |

`accordion_tab` is the only **stock** widget altered, and the change is additive:
a tab that does not ask for `header_check_box` gets exactly the header it always
had. That mattered rather a lot — 26 other files in the English skin alone
declare accordion tabs, 75 of them, and every one is outfit editing, profiles,
preferences or the About box. Which is the standard to hold anything else here
to: if the Lightbox needs something from a shared widget, it asks for it by an
optional param and leaves the default behaviour alone.

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
  **Scene** = render quality and performance. **Sky** = the sky being shot:
  client-side sky effects, which are ordinary settings rows, plus the day cycle
  controls, which are the odd ones out because they reach past this floater and
  change the world's environment. Read §4h before adding day cycle controls;
  a sky *effect* needs nothing special.
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

### 3b. A switch on the section header (optional)

`accordion_tab` takes an optional `header_check_box`, drawn at the right end of
the header. It is a full `check_box` params block, so `control_name`,
`enabled_control`, `tool_tip` and `commit_callback` all behave as they do
anywhere else:

```xml
<accordion_tab name="atab_sec_myfx" title="My Effect" ...>
    <accordion_tab.header_check_box
     name="section_myfx"
     initial_value="true"
     enabled_control="RenderColorGrade"
     tool_tip="Whether My Effect is applied.">
        <commit_callback function="LightBox.ToggleSection" parameter="myfx" />
    </accordion_tab.header_check_box>
    <panel ...>
```

Note `<commit_callback>` and not `<check_box.commit_callback>`: inside a nested
params block the dotted prefix has to be the *block's* name, so plain
`<commit_callback>` is what resolves. `<header_check_box.commit_callback>` works
too, and `<check_box.commit_callback>` is silently ignored.

Omit the block and there is no checkbox — not a hidden one, none at all — which
is what keeps every other accordion in the viewer exactly as it was. Four things
worth knowing before you use it elsewhere:

- **Size.** It defaults to a bare 24×16 box. A header checkbox carrying a
  `label` has to state its own `width`, because nothing measures the text for
  you. The title ellipses at the checkbox's left edge either way. The area that
  actually takes the click is `LLCheckBoxCtrl`'s bounding rect — the box and
  label, not the full rect — so a press inside the rect can still be refused;
  the tab falls through to expand/collapse when that happens rather than
  leaving a dead ring of pixels.
- **The tab eats mouse-downs.** `LLAccordionCtrlTab::handleMouseDown` claims the
  whole header band and toggles expand/collapse *before* any child is offered
  the press, so a control living there is unreachable by default.
  `pointInHeaderCheckBox` is the exemption; anything else you add to a header
  needs the same treatment.
- **The tab eats tooltips too**, and worse: `handleToolTip` forwards the header
  band to `mHeader` without converting coordinates, so a header child's own
  tooltip is never found once the tab is expanded. Same exemption, and it does
  convert.
- **No `control_name` for a comparison switch.** See the bypass note in
  *Architecture in brief*: a whitelisted setting toggled to compare dirties the
  active Look. A header checkbox that *is* a setting — the grading master —
  takes `control_name` as normal, and `LightBox.ResetSection` reaches it: the
  walker checks `atab_<section>`'s header checkbox as well as the panel, so a
  bound switch on a header is still covered by that section's Reset All.

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

Two widgets exist beyond the standard rows, and each is one XUI tag. (The third
richer control, the switch on a section header, is §3b — it is a param on
`accordion_tab` rather than a tag of its own.)

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
- The rest of its params are appearance and have working defaults you should
  need to override only for a genuinely different control: `ring_thickness`,
  `ring_steps` (segments the ring is drawn in), `puck_radius`, `decimal_digits`
  for the three channel fields, and `border_color` / `face_color` /
  `crosshair_color`. `label` names the wheel above the ring.

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
- `grid_divisions` sets the backing grid; `curve_samples` how finely the
  sampling function is evaluated across the width; `handle_radius` and
  `curve_width` the hit target and the stroke. Colours are `background_color`,
  `border_color`, `grid_color`, `curve_color`, `handle_color`.

**Graph maths belongs in the model, next to a test.** `ALCurveModel` mirrors the
shader's own functions, and `alcurvemodel_test` transcribes the GLSL
independently and compares. A graph that merely illustrates the shader is worse
than none: it will be believed. If you plot something new, transcribe it.

**The two of them draw with `gl_polyline_2d` and `gl_polyfill_2d`**
(`llrender2dutils`), added for this work and available to anything else that
plots. Use them rather than reaching for `LLRender::setLineWidth` and
`GL_LINE_SMOOTH`: smoothing appears nowhere else in this tree, core profiles
routinely ignore it, and `setLineWidth` clamps to `mAliasedLineRange`, which is
`[1,1]` on most core drivers — so neither width nor smoothing can be relied on
from the fixed pipeline. The polyline lays a ribbon of triangles with a
one-pixel alpha falloff and mitred, clamped joins, so a curve has no notches at
its vertices and a hairpin is blunted rather than shot off to infinity.

The fill's edge is deliberately left aliased. Pass it the translucent colour
such a fill wants and outline it separately if you need a crisp edge; a feathered
fill under a feathered outline doubles the coverage along the shared path and
draws a darker seam.

### 4c. Tools

Four controls act on something other than a setting. None of them writes to
`gSavedSettings`, and that is the point of grouping them: each parks state
somewhere else, which is a liability the ordinary rows do not have.

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
  driven by the `LightBox.ToggleSection` checkbox on each section's accordion
  header. Ticked is the section switched **on**, because that is the only way a
  box beside a section title reads; `onToggleSection` inverts, since the bit it
  drives suppresses. A set bit makes `colorCorrect` upload that group's
  **identity** values instead of its settings, which lands in the early-out the
  shader already has for that step — so this needed no new uniform, no new
  variant and no recompile, and a bypassed section costs slightly *less* than an
  active one. If you add a grading step, add its identity to that block or the
  bypass will quietly skip it.

  The groups match Reset All's grouping (`sec_<id>` plus `sec_<id>_adv`
  together), so a section with a long tail in an Advanced sibling is one switch
  and one idea of "a section". Put the checkbox on the essentials tab only; the
  Advanced sibling is the tail of a section, not a section.

  These lived as a row of five in the Color Grading section until it turned out
  that the objection to putting them on the headers — that A/B work means
  flipping between them, and hunting through collapsed accordions is worse than
  one row — was answered by the header itself. A collapsed accordion still shows
  its header, so a header checkbox is visible in every state the section has,
  and it is beside the controls it suppresses instead of a scroll away from
  them. All five headers are in view at once whenever their sections are shut,
  which is the state A/B work is done in anyway.

  They carry no `control_name` on purpose. Every grading setting is on the Looks
  whitelist, so a comparison built out of one would dirty the active Look and
  could then be saved mid-comparison; and since the floater is destroyed on
  close (see below), a fresh one comes back with all five ticked, which is what
  makes "clears when the Lightbox closes" true without any code to do it.
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

The first two reach across a frame or a keypress; the last two park state that
outlives the floater. **Anything deferred like that must capture an `LLHandle`,
never `this`** — the Lightbox declares neither `single_instance` nor
`reuse_instance`, so closing it *destroys* it, and an armed picker holding a raw
pointer is a use-after-free with no window of luck involved.

**And anything that parks state outside the floater must clear it in the
destructor.** The bypass mask lives in the pipeline; left set, a closed Lightbox
would leave a section suppressed with nothing on screen to say so and no setting
to inspect. That is harder to diagnose than a crash. The reference still is the
same rule with a second reason — it is a full-resolution target, so leaving one
behind holds real memory for a comparison nobody can see or switch off any more.
`~ALFloaterLightBox` clears all three: the armed picker, the bypass mask, and
the still along with its wipe mode.

That the destructor is enough turns on the floater being destroyed on close, so
do not "tidy up" by declaring `single_instance` on it without moving this
cleanup to `onClose` first. It would keep every one of these switched on behind
a closed window.

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

Its XUI is deliberately thin, because almost none of that window is widgets. A
`combo_box` bound to `AlchemyScopeLayout`, a `check_box` on
`AlchemyScopeLogScale`, an empty `panel` named `scope_plot` that the scopes are
painted into, and a `text` for the clipping readout. Everything else is drawn.
The nine pane labels are `floater.string` entries named `mode_*` rather than
literals, so they translate, and `menu_scopes_pane.xml` is a `context_menu` of
`menu_item_check` rows wired to `Scopes.SetPaneMode` / `Scopes.IsPaneMode`.

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

So: after touching any `*F.glsl` under `shaders/class1/alchemy/`, launch the
viewer — a shader that fails to compile or link says so in the log before the
frame dies, and that log line is the only automated check there is. Nothing in
`cmake --build` or `ctest` covers shaders, and there is no offline checker in
the tree; if you write one, commit it under `scripts/` and name it here.

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
name outright, next to the size-sync and duplicate checks.) And after adding an
entry, **grep the shaders for the exact string you put in the table** — a name
no shader declares produces no error anywhere, only zero-filled uniforms, so
the grep is the whole check and there is no tool that does it for you.

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

The Undo and Redo buttons in the top bar are the visible half of that. Both, and
the reference row, are greyed from `draw()` rather than from a signal — the undo
stack moves on every commit, every undo and every Look apply, and hanging a
refresh off each of those is more places to forget than a polled compare costs.

### 4g. The top bar

`lightbox_topbar` in `floater_lightbox_settings.xml` is an ordinary `panel` of
ordinary widgets, and worth reading before you add to it, because it is the one
part of this floater with a fixed width budget: **412px at `min_width`** — the
floater's 420 less its own `left="4"`/`right="-4"`, and nothing else, because
the bar sits directly in the floater. A row inside an accordion section starts
from the same 420 and loses far more (§5); the two budgets are different on
purpose. The bar currently spends 342 of its 412.

Left to right: the Looks `combo_box`, then Save / Save As / Delete / Revert,
then Undo / Redo, then Scopes. Three groups, separated by 12px where the
adjacent buttons inside a group are separated by 4. **That gap is the only thing
that says they are different kinds of thing** — the first group acts on the
Look, the second on the grade's own history, the third opens another window — so
keep it if you add a fourth kind, and use 4px if you are extending a group.

Everything after the combo is an **18px icon with an empty label**, and the
tooltip carries the name. That is not decoration. Four text labels cost 192px of
the 412; the same four icons cost 80. It also sidesteps §5's silent clipping the
day this floater is translated and "Save As" becomes "Speichern unter" — a bar
of labels has no reflow and no scrollbar to save it. Take the overlays from the
viewer's existing set (`Script_Save`, `Conv_toolbar_plus`, `TrashItem_Off`,
`Refresh_Off`, `Script_Undo`, `Script_Redo`, `Command_Stats_Icon`) rather than
adding art; picking a glyph that already means the right thing elsewhere is most
of the work.

**A button that opens another floater needs no C++ at all** — `Floater.Toggle`
is a global commit callback in `llui.cpp`, with the floater's registered name as
`parameter`. Use that one and not `Floater.ToggleOrBringToFront`; §4d explains
why the second can only ever open.

### 4h. The Sky tab, and changing the world

The Sky tab holds two kinds of thing and they do not behave alike.

**Sky Effects is ordinary settings rows** and wants nothing from this section:
`control_name`, a reset glyph, a Reset All, done. Aurora, meteors and the star
field are drawn entirely on the client, so nothing is sent to or from the region
and no land setting turns them on — which is what makes them safe to expose here
at all, and why they sit beside the day cycle rather than in Scene. All three
gate on the same star brightness the night sky uses, so they simply do not
appear in daylight, and an HDRI sky replaces the dome and leaves nothing to draw
into. None is on the Looks whitelist: a Look is the aesthetic settings of the
Look and Lens tabs, and a Look that switched the aurora on would be a surprise.

Two shaping decisions in that section are worth copying. It is **one** section
rather than three because two of the effects are a single control each, and a
section per slider reads as filing rather than grouping. And the star count is a
**dropdown**, by §2's rule: committing it regenerates every star position and
rebuilds the vertex buffer, so a slider — which commits on every mouse-move —
would hitch the whole way across its own travel.

**Day cycle is the odd one out.** It changes `LLEnvironment`, which is shared
with the whole viewer, and that makes it a different kind of thing to work on.
Six rules, all learned the hard way and all still true for anything else that
reaches out of this floater.

**There is no clock to stop.** `DayInstance::getProgress()` computes the cycle
position from `LLDate::now()` plus the day offset, every frame, so nothing can
be paused. "Freeze" means sampling the running cycle at one position and
installing the result as a *fixed* local environment; the motion stops because
there is no longer a day cycle in effect. That is also how `@setenv_daytime`
and the day cycle editor's timeline do it — sample with
`LLTrackBlenderLoopingManual(target, day, track)->setPosition(0..1)`.

**Sample water as well as sky.** Track 0 is water and the sky tracks are 1 to 4,
chosen by altitude via `calculateSkyTrackForAltitude`. RLVa's version freezes
only the sky, and a frozen sky over a moving sea is not frozen.

**RLVa is enforced below you, not by you.** `setSelectedEnvironment` returns
early when `!RlvActions::canChangeEnvironment()`, inside `LLEnvironment`. A
control that does not check it looks live and silently does nothing under
`@setenv=n`, so the Light rows are greyed from the same `draw()` poll that reads
their state back. There is no `enable_callback` on ordinary widgets in XUI —
menus have `on_enable`, widgets do not — so this has to be done in C++.

**Freezing covers up whatever `ENV_LOCAL` held**, which is where Personal
Lighting and an inventory-applied sky live. Unticking Freeze puts back what was
captured on the way in; "Restore region environment" is the unconditional way
out and *does* discard it. Clearing without capturing first is a silent way to
lose someone's sky.

**Reverting the sky invalidates every reflection probe** that was lit by it.
`gPipeline.mReflectionMapManager.reset()`, the same call the World menu's own
revert makes.

**Read the state from the world, not from a mirror.** Whether the sky is frozen
is `getEnvironmentFixedSky(ENV_LOCAL) != nullptr`; the cycle to scrub is the
first day found across `ENV_LOCAL`, `ENV_PUSH`, `ENV_PARCEL`, `ENV_REGION`.
Deriving both means the tab stays honest when the World menu, an attachment or
another floater changes the environment underneath it — and it is what lets
scrubbing survive closing and reopening the floater, since nothing about the
freeze is remembered in the floater at all.

#### A cycle position is not a time

Nothing in this viewer maps a cycle position to a clock. The day cycle editor
labels its timeline as a **percentage**, and a region can put its keyframes
wherever it likes, so "noon is 0.5" is a property of some day cycles and not
others. Even the stock day is not what you would guess: `LLSettingsSky::defaults`
computes sun altitude as `π × position`, and caches its result in a `static`, so
the viewer's own default day cycle is eight identical frames.

So the four preset buttons do not use fractions. `ALDayCycleLandmarks::find`
samples the cycle, reads `getSunDirection().mV[VZ]` — the sun's height above the
horizon — and takes noon and midnight from the extremes and sunrise and sunset
from the horizon crossings, interpolated between samples so the answer beats the
grid. A cycle without a given landmark reports it absent and the button greys,
because a sun that never sets has a noon and no sunrise, and inventing one would
be worse than offering nothing.

It takes a sampler rather than a day cycle, the same shape `curve_editor` uses,
which is what lets `aldaycyclelandmarks_test` exercise it with a sine wave and
no viewer around it. Sampling costs ninety-six blends, so it is cached against
the day it was computed from and never runs on the frame path.

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
- **A row of buttons has to be sized for `min_width`, not for the default.**
  The arithmetic that matters is 420 − 28 for the chrome − 15 for the accordion
  scrollbar − 16 for `left="8"`/`right="-8"`, which leaves **361px**. The Light
  tab's four presets are 85 wide with 6px gaps and end at 366 of 369. Laid out
  against the 460 default they looked fine and lost their last button the
  moment the floater was narrowed.
- Widths never reflow. Usable inner width is the floater's width − 28, and
  ~15px less again whenever the accordion's scrollbar shows. **Overflow clips
  silently, with no scrollbar and no warning**, so check the narrowest case:
  the floater at its `min_width` *with* the vertical scrollbar visible.
- `min_width` is the only declarative way to widen an existing user's floater
  (`LLFloater::applyRectControl` prefers a saved rect over the XUI width), and
  it force-widens everyone permanently. Design to the current width instead.
- **All of this is checkable without launching**, and worth checking that way
  because the failure is silent: walk each `sec_*` panel's children, track
  `top = prev_bottom + top_pad` (or the absolute `top`), and assert the panel's
  declared height is the lowest bottom + 8 and the tab's is the panel's + 29.
  Thirty lines of Python over the XUI, and it catches the arithmetic slip that
  otherwise shows up as a row you cannot see.

### 6. Gating

`enabled_control="SomeBool"` / `disabled_control="SomeBool"` on each dependent
widget greys it live (they connect to the control's signal). Boolean controls
only; apply per row, not on the parent panel. Reference patterns:

- HDR fork: bloom rows `enabled_control="RenderHDREnabled"`, legacy glow rows
  `disabled_control="RenderHDREnabled"` — greying, not visibility, so the
  layout never gets holes and both modes stay discoverable.
- **`RenderColorGrade` is the master switch for the entire grading suite**
  (LUT *and* Basic, White Balance, Split Toning, Lift/Gamma/Gain, Tone Curve).
  Any new grading control must gate on it or it will look inert. It lives on
  the Color Grading accordion's own header, as a `header_check_box` with
  `control_name` — so it is legible and throwable whether or not the section is
  open, and it reads the same way as the five section switches beneath it. Note
  what tells the two kinds apart: the master has a `control_name` because it is
  a setting, the section switches have none because they are a viewing state.
  That section is also the only one that opens by default, which is why its
  body is kept to one line of text plus the reference still.
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

Two things that only show up on screen, both of which did:

- **A one-line `text` needs `height="16"`, not `height="30"` with `word_wrap`.**
  Three lines of prose in a 30px box do not scroll or ellipse, they draw over
  the row beneath. Count the characters: roughly 70 fit on a line at
  `SansSerifSmall` across a section at `min_width`.
- **A button's `width` has to fit its own label**, and an `image_overlay` eats
  18px of it before the text starts. There is no reflow and no ellipsis; the
  label is simply cut. Prefer a name the viewer already uses — "Use shared
  environment" is the World menu's own wording for dropping a local environment
  — over a longer one you invent.

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
- For a section switch: **untick** it (ticked is on) and confirm the image
  changes **and the Looks `*` does not appear**. Then close the Lightbox with it
  still unticked and confirm the render comes back — state parked outside the
  floater is the failure mode here, and it looks like a renderer bug rather than
  a UI one.
- For the Sky tab's day cycle: freeze, wait past the point the sky would have
  moved, and confirm it has not. Then untick and confirm you get back *what you
  had*, not the region default — set a Personal Lighting sky first, since that
  is the case a missing capture loses. Fly through an altitude band while frozen
  and confirm the sky holds; check the water stopped too, not just the sky.
  Finally, confirm the presets land somewhere plausible on a region whose day
  cycle is not the default one, because a hardcoded fraction would also look
  right on a default region.
- For anything on an accordion header: click it and confirm the section does
  **not** expand or collapse, then hover it with the section expanded and
  confirm its own tooltip appears rather than the title's. Those are the two
  interceptions in §3b, and the tooltip one only shows up when expanded — test
  it collapsed and it will look fine.
- If you added a print effect: take a snapshot with "No post-processing" ticked
  and confirm the effect is absent from the saved file, not just from the
  preview.
- **Developer-build staging trap: a build never stages XUI at all.** Not
  "unreliably" — never. This is worth knowing rather than guessing at, because a
  stale copy looks exactly like an edit that did not work.

  The `POST_BUILD` custom command on the viewer binary target does run
  (`viewer_manifest.py --actions=copy`, `newview/CMakeLists.txt`), but the
  manifest's entire `skins` / `app_settings` / `character` / `fonts` block sits
  behind `if self.is_packaging_viewer():`, which is `'package' in actions` — and
  the build passes `--actions=copy`. So the copy stage refreshes the exe, the
  DLLs and the plugins, and nothing else.

  - Relinking does **not** help. Neither does changing C++ alongside the XML,
    neither does re-running CMake, and it makes no difference whether the file
    is new or existing.
  - Copy changed files into `build-.../newview/<config>/skins/...` yourself, and
    check the result by **hash**: the staged tree has files of many different
    ages, so a timestamp tells you nothing.
  - To find drift across the whole tree, walk `indra/newview/skins/**/*.xml` and
    compare each against its counterpart under
    `build-.../newview/<config>/skins/`.

  An earlier revision of this file blamed the relink. It was wrong, and it cost
  a debugging round each of the three times it was believed.
