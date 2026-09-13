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
| Where every section and setting is, found once | `indra/newview/allightboxdirectory.{h,cpp}` |
| Looks preset system + whitelist | `indra/newview/llpresetsmanager.{h,cpp}` |
| Bundled starter Looks | `indra/newview/app_settings/looks/` |
| Colour wheel widget + its maths | `indra/newview/alcolorwheel{ctrl,model}.{h,cpp}` |
| Curve/band graph widget + its maths (the spline, the split-tone ramps, and the `ALToneCurveSet` the renderer bakes) | `indra/newview/alcurve{editorctrl,model}.{h,cpp}` |
| Scopes floater + measurement | `indra/newview/alfloaterscopes.{h,cpp}`, `alscopedata.{h,cpp}` |
| Scope pane assignment menu | `.../menu_scopes_pane.xml` |
| Tone curve preset menu | `.../menu_lightbox_curve_presets.xml` |
| Undo/redo stack | `indra/newview/algradehistory.{h,cpp}` |
| White-balance map and its inverse | `indra/newview/alwhitebalancesolver.{h,cpp}` |
| Scene colour picker tool | `indra/newview/altoolscenepicker.{h,cpp}` |
| 3D LUT (.cube) parser | `indra/newview/lutcube.{h,cpp}` |
| Header checkbox on `accordion_tab` | `indra/llui/llaccordionctrltab.{h,cpp}` |
| A swatch handing its picker to its owner | `indra/newview/llcolorswatch.{h,cpp}` |
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
| One setting on one line, with its reset | `<setting_row>` | §4 |
| Colour wheel | `<color_wheel>` | §4b |
| Curve / band graph | `<curve_editor>` | §4b |
| Checkbox on an accordion header | `<accordion_tab.header_check_box>` | §3b |
| Anti-aliased polyline and area fill | `gl_polyline_2d`, `gl_polyfill_2d` | §4b |
| Floater top bar (Looks, history, scopes, find) and the tab strip's pop-out | ordinary buttons, `Floater.Toggle` | §4g |
| Scopes window | its own floater | §4d |
| Sky tab (day cycle freeze) | ordinary rows, `LLEnvironment` behind them | §4h |

`accordion_tab` was the first **stock** widget altered, and the change is
additive: a tab that does not ask for `header_check_box` gets exactly the header
it always had. `color_swatch` is the second, and additive the same way: it can
hand its picker to an owner (`setPickerOverride`), which only the Lightbox does,
so every other swatch opens the colour picker floater exactly as before. That mattered rather a lot — 26 other files in the English skin alone
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
  Dropdown, colour and vector rows call it from their reset buttons; setting
  rows reach the same function through `setResetHandler`, set in `postBuild`.
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
  graph's commit, which interprets a drag, an added point or a removed point
  against the curve the channel combo selects and writes that one setting,
  and the combo's refresh.
- `LightBox.ResetToneCurveChannel` / `LightBox.ToneCurvePreset` — the reset
  glyph beside the combo (the selected curve only) and the preset menu
  (`parameter` = preset id), each one write to the selected curve.
- `LightBox.CommitSplitToneGraph` — the split-tone band graph's handles: the
  middle one writes `RenderSplitToneBalance`, the two edges write
  `RenderSplitToneShadowWidth` and `RenderSplitToneHighlightWidth`.
- `LightBox.PickWhiteBalance` — arms the eyedropper.
- `LightBox.OpenLUTFolder` — reveals the user's colour-LUT folder, creating it
  on first use.
- `LightBox.Find` — the Find popover (§4j).
- `LightBox.History` — the undo history popover (§4f).
- `LightBox.PopOut` — the tab that is up, out into a window of its own or back
  (§4k).
- The Looks bar and tonemapper-row greying (effect-specific, already done).

**Asset-picker rows are a third kind of dropdown**, distinct from the enum
recipe below. They bind a `combo_box allow_text_entry="true"` to a *string*
setting naming a file, and are filled from C++ by
`populateAssetCombo(combo_name, dir_name, extensions, setting_name)`: bundled
entries from `app_settings/<dir>` first, then the user's own from
`user_settings/<dir>` behind a separator, matching the order the renderer
itself resolves names in. Four things are not optional. The XUI carries one
literal `<combo_box.item value="" label="None"/>` and nothing else. `postBuild`
must call the populate helper, because the list does not exist until it does.
The helper's closing `selectByValue` is load-bearing rather than cosmetic —
with `allow_text_entry` nothing else restores the saved value when the floater
opens. And the extension whitelist must list only what the loader can actually
decode, or the picker offers files that silently fail. Pair it with an
`openUserAssetFolder(dir_name)` button so the folder is discoverable.

The colour LUT is currently the only picker — the lens dirt plate that shared
these helpers is generated now. Both stay parameterised by directory anyway,
because the shape is the shared part and collapsing them back to a constant
only has to be undone for the next asset. Before reaching for a picker at all,
ask whether the asset could be generated instead: a texture a shader can draw
into a render target once needs no file, no packaging entry, no extension
whitelist and no fitting to the window, and it can be put on sliders. See
`generateLensDirt` in pipeline.cpp for the shape — gate, cached parameter set,
allocate, draw, release when the effect goes off.

The graph, eyedropper and picker callbacks are examples of the per-control
cost: a graph or a tool needs something to interpret its input, so it gets one
callback and one `setup*` call in `postBuild`. Both graphs follow the same shape — `setupX` connects to
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

First row uses `top="8"` (`top="7"` for a setting row, which is two pixels
taller than a slider); later rows chain with `top_pad`. Every value row gets an
18px reset glyph. Copy these verbatim and edit names/keys/ranges:

**Scalar row** — one `setting_row`, `top_pad="8"` between setting rows:

```xml
<setting_row
 follows="left|top|right" layout="topleft" left="8" top_pad="8" right="-8"
 height="18" label="Strength" decimal_digits="2" increment="0.01" min_val="0"
 max_val="1" name="myfx_strength" tool_tip="..." control_name="RenderMyFxStrength" />
```

`setting_row` (`indra/llui/alsettingrow.{h,cpp}`, tested by
`alsettingrow_test`) is the slider and its reset glyph as one widget, and it
takes the slider's attributes by the slider's names. What it does for you:

- **The reset glyph is built in** and shows only while the setting differs from
  its default *as shown* — rounded to the row's `decimal_digits`, so a drag that
  comes back to 0.7 and lands on 0.69999999 leaves nothing lit. Its slot is kept
  while it is hidden, so nothing moves when it appears. In the Lightbox its reset
  comes through `onClickResetControlDefault` (the floater takes it with
  `setResetHandler` in `postBuild`), so it is a named undo step like any other.
- **Only the row is bound.** Reset All's walk finds one key per row, a commit is
  one write, and `enabled_control` / `disabled_control` grey the slider, its
  label and value *and* the reset glyph together.
- **The value box sizes itself** where a slider's would cut the number short —
  `max_val` under 1, at or below 0, or a `min_val` wider than `max_val` — so §7
  no longer applies to it. `text_width` still wins where it is written.
- `label_width="140"` and `can_edit_text="true"` are its defaults and need not
  be written.
- **It says when a drag begins and ends**, by the slider's own names: child
  elements `setting_row.mouse_down_callback` and `setting_row.mouse_up_callback`.
  A setting that is dear to apply uses them to leave its work for the release —
  the seven lens dirt generation rows raise and lower the pipeline's
  slider-held flag this way, so the plate is drawn once per drag.

Every scalar row on the four tabs is a `setting_row`. The form they replaced — a
`slider` (`right="-32" height="16"`) followed by a separate reset `button`
(`top_pad="-17"`, `LightBox.ResetControlDefault` with the setting repeated as
its `parameter`) — still works, but do not add one: it is two widgets to lay out
against each other and a setting name to keep in step by hand. A slider that is
*not* a scalar row (a vector bank's component, or one driven from C++ like the
day cycle's time) stays a plain `slider`.

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
alpha handling lives in the widget — nothing else needed. A swatch bound to a
**Color3** setting picks inline without any XUI to say so (§4j): `postBuild`
hands every such swatch's picker to the floater, and the popover it opens under
the swatch previews live, puts the colour back on Escape, and records the whole
pick as one undo step. Its "Full picker..." button is the way on to the
viewer's own picker, for typed values and the eyedropper. Keep tints within
0-1: the inline picker's tracks stop there.

**Vector row**: label `text` (width 110, `top_pad="10"`) + spinners named
`vec3_<Setting>_<0|1|2>` at `left_delta="110" top_pad="-16" width="68"
height="18"` (then `left_delta="72" top_delta="0"`), each with per-axis
`min_val`/`max_val` and `<spinner.commit_callback function="LightBox.CommitVec3" />`,
+ one reset glyph (`top_delta="0"`, parameter = the setting). Setting names
contain no underscores, so the name parse is unambiguous. Omit components that
are unused — label the ones you keep by meaning.

Give each spinner its channel letter as `label` (`label_width="10"`) and
`scrub="true"`: dragging the letter sideways changes the value, 4px per
increment, with Shift for 0.01×, Ctrl for 0.1× and Alt for 10×. Undo folds a
whole scrub into one step on its own, because every move writes the same
setting. Leave `scrub` off a spinner **without** a label: the drag then moves
onto its arrow buttons, and they lose hold-to-repeat. The two SSAO spinners on
the Scene tab have no label, their caption being the text beside them, so they
do not scrub.

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
and a handle list, which is why one widget serves both. The tone curve also
lets the user add and remove points; that is a param, not a second widget:

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
  A handle that slides horizontally to set a value locks *Y*; a split-tone
  edge held at the plot boundary locks *X* as well, and its consumer treats
  that lock as "write nothing".
- `draw_diagonal="true"` draws the identity, which is meaningful for a tone
  curve and meaningless for anything else.
- `grid_divisions` sets the backing grid; `curve_samples` how finely the
  sampling function is evaluated across the width (the tone curve raises it
  to 192, because a sharp toe facets on a tall graph at the default 96);
  `handle_radius` and `curve_width` the hit target and the stroke. Colours are
  `background_color`, `border_color`, `grid_color`, `curve_color`,
  `handle_color`. A handle's own `mColor` fills its disc; the ring is the
  widget's `handle_color`, and the ring is what the hover highlight brightens,
  so a coloured handle stays recognisable under it.
- `points_editable="true"` turns on the gestures: a double-click on empty plot
  area asks for a point there, a double-click on a handle asks for its
  removal. **Asks**, because the widget still owns no curve. The commit
  callback reads `getAction()` — `ACTION_DRAG`, `ACTION_ADD` (with
  `getActionX()`/`getActionY()`) or `ACTION_REMOVE` (with `getActiveHandle()`
  naming the target) — decides against its own model whether to honour it,
  and hands back a new handle list. The model refuses an endpoint removal and
  a 17th point; the floater then just refreshes, which puts the handle back.
  A double-click on the frame outside the plot is ignored, and an add at an
  edge lands one gap inside the pinned end rather than on it. A file may
  carry up to 64 points, which the editor keeps but will not add to; more
  than that is rejected as garbage and renders as no curve.
- **A press that does not move never commits.** The viewer calls a captured
  widget's `handleHover` every frame, not only on motion, so without the
  no-motion guard a plain click on any handle rewrote its value at pixel
  resolution once per frame — a dirty Look and an undo step for a click, and
  the first half of every double-click. Keep that guard if you touch the
  drag.
- `edited_settings="A,B,C"` names the settings the graph edits. The widget
  never reads them; the section's Reset All walker does, because those
  settings are not bound through `control_name` (an LLSD point list cannot
  be) and would otherwise be invisible to it. A typo is silent, so
  `setupToneCurve` warns once for a name that is not a control.

**Graph maths belongs in the model, next to a test.** `ALCurveModel` holds the
split-tone ramps in exactly the `{scale, bias}` form pipeline.cpp uploads, and
`ALToneCurveSet` is what `LLPipeline::bakeToneCurveLut` bakes into the tone
curve texture; `alcurvemodel_test` transcribes the GLSL independently — the
ramps, and the half-texel fetch through GL's linear filter — and compares. A
graph that merely illustrates the shader is worse than none: it will be
believed. If you plot something new, transcribe it.

The tone curve is four monotone splines, master and one per channel, stored as
LLSD arrays of `[x, y]` pairs and composited as `master(channel(x))`. The
renderer never evaluates them: the CPU bakes a 512-texel RGBA16 row on a dirty
flag and the shader does one clamped fetch per channel behind an amount that is
0 whenever the section is bypassed, every curve is straight, or the texture is
missing. That is the same shape as the 3D LUT and is documented in §4e.

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

  While any section is off, the Look tab carries a **badge counting them**
  (`refreshBypassBadge`, the `bypass_badge` string). A bypass is the one piece of
  grading state that nothing else on screen shows once its header is scrolled
  away or another tab is up, and forgetting one is in force is how a grade gets
  judged wrong. The badge is counted from `sGradeBypassMask`, not from the
  checkboxes, and refreshed from `onToggleSection`, which is the mask's only
  writer. A new section switch therefore needs its bit in `refreshBypassBadge`'s
  list as well as in `onToggleSection`'s.

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

**A step that needs a texture follows the tone curve LUT, not the 3D LUT.**
`LLPipeline::bakeToneCurveLut` is the pattern: a raw GL name allocated empty in
`createGLBuffers` (one-shot, immutable storage), released in `releaseGLBuffers`,
a dirty flag set from the settings' commit signals in `init()`, and the bake
itself run lazily at the top of `colorCorrect` — before that pass binds
anything, because the upload borrows texture slot 0 — through
`LLImageGL::setManualSubImage`. Lazy rather than in the signal handler because
`setShaders()` releases and recreates every GL buffer behind the settings'
back, and only a bake that runs on the way to drawing refills the texture. The
3D LUT (`mCGLut`) is the older shape and is released only by reassignment; do
not copy that. Bind the texture with a **clamp** sampler
(`ALSamplers::BilinearClamp`): a lookup table must saturate past its ends.

The step's identity for the bypass block is an amount of 0. That leaves the
sampler unread, so nothing needs binding on its unit — which is only safe
because the read sits behind a branch on a uniform. Do not rewrite such a
branch as a `mix` by zero.

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
do-nothing step on the stack. That holds for LLSD-typed settings — the curve
point lists — only because `llsd_compare` grew a `TYPE_LLSD` case with the
tone curve work; before it, every write to such a setting counted as a change.
Numbers compare as numbers there, so an integer-typed list from a hand edit or
the notation parser is the same value as its real-typed default.

Two rules if you add a control:

- **One user gesture must be one step.** A drag emits a commit per mouse-move,
  and the history collapses those by coalescing successive writes *to the same
  control* inside 500 ms. Every interactive control here writes exactly one
  setting per commit — the tone-curve graph writes the one curve the channel
  combo selects, whether the gesture was a drag, an added point, a removed
  point or a preset; the band graph writes balance *or* one width per handle,
  and a balance drag moves both edges by writing only the balance — which is
  what makes that enough. Two gestures on the same curve inside 500 ms
  coalesce into one step, which is acceptable. **A control that wrote two
  settings per commit would defeat it**, and produce one undo step per setting
  per mouse-move. If you need one, the fix is in `ALGradeHistory`, not in the
  caller.
- **A discrete action that moves many controls needs `ScopedHistoryGroup`.**
  Reset All, applying a Look, Look revert and a white balance pick (which sets
  temperature *and* tint) each wrap one, so they undo in a single step. Note that a group deliberately does *not* coalesce, so it must
  only ever wrap a discrete action — wrapping a per-move commit in one would
  give you back the hundred-step drag.

**A step that changed nothing is not kept.** A drag that comes back to where it
started, a group whose writes cancel out, a write of the value already there —
each would be an undo step that visibly does nothing, which reads as Ctrl+Z
being broken just as surely as a hundred steps for one drag does. The history
drops them itself (`llsd_equals` on each change's before and after), and a
no-op write does not even cost the user their redo tail.

**A group can be named** — `ScopedHistoryGroup(mHistory, label)` — and the step
it makes carries the name (`at`, `labelOf` and `revision` read the stack). The
outermost group's name wins, so a Look apply that resets a section on the way
through is still called after the Look. Every group here is named, from the
`history_*` strings in the floater XML: "Reset Bloom (HDR)", "Reset Strength",
"Look: Soft Film", "Revert to Soft Film", "White balance pick". A per-row reset
is a group too, though it is one control, so that it is named and so that a
reset straight after a drag of the same slider is its own step rather than the
end of the drag. **A new discrete action should take a label** for the same
reason.

**The History button** (after Redo) shows the stack as `ALHistoryList` in a
popover (§4j): every step by name, the present marked, and a double-click on any
step goes back or forward to it — `goToHistory` steps through `applyHistory`
one transaction at a time, so a jump writes exactly what that many Ctrl+Z
presses would. A step without a label is named after the setting it changed,
by the caption its row shows (§4i), with its section beside it; one that
changed several unnamed settings says how many. The first row, "Start of
history", is not a step: it is what lets one double-click undo everything.
While the list is up it follows the stack (`draw()` refills it when
`revision()` moves), and Ctrl+Z / Ctrl+Y step it in place.

Undo restores **values only**. The active Look stays dirty, exactly as it would
had the user typed the old numbers back in; restoring that faithfully would mean
modelling the Looks system's history too. The stack is a floater member, so it
dies with the window rather than outliving it to rewrite a later session's edits.

Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z are bound in `handleKeyHere`, **floater-local and
not a global action** — the opposite choice from hold-to-compare, and for the
opposite reason: a global Ctrl+Z fires while the user is typing anywhere in the
viewer.

Floater-local is not enough on its own, though. `LLViewerWindow::handleKey`
offers every Ctrl and Alt key to the **menu bar's accelerators before the
focused floater**, unless something in the focus chain answers
`hasAccelerators()`. Until the floater claimed them, Ctrl+Shift+Z never reached
`handleKeyHere` at all: it is World > Environment > Midnight, so the documented
redo set the sky to midnight. (Ctrl+Z and Ctrl+Y only ever got through because
the Edit menu enables Undo and Redo only while the current edit handler can
undo, and a Lightbox line editor has no undo of its own.) The floater now
returns true from `hasAccelerators`, and a key it does not handle falls through
to the menus exactly as before. The one thing the menu used to add, Edit > Undo
for a text control with an edit history, is offered by `handleKeyHere` itself: a
focused text control inside the floater that `canUndo()`/`canRedo()` gets the
key before the grade does. Any new shortcut here has to be checked against
`menu_viewer.xml`, because it is now the Lightbox that wins the clash.

The Undo and Redo buttons in the top bar are the visible half of that. Both, and
the reference row, are greyed from `draw()` rather than from a signal — the undo
stack moves on every commit, every undo and every Look apply, and hanging a
refresh off each of those is more places to forget than a polled compare costs.

### 4g. The top bar and the tab strip

**The bar** (`lightbox_topbar` in `floater_lightbox_settings.xml`) holds every
button the floater has but one. Left to right: the Looks `combo_box`, then Save
/ Save As / Delete / Revert, then Undo / Redo / History, then Scopes, then
Find. **The one button that is not there is Pop-out**, at the right end of the
tab strip: it acts on the tab that is up, so it sits at the end of the tabs.
**Only a button about a tab belongs on the strip**; Scopes and Find were tried
there and read as out of place, since neither is about a tab. The bar used to
hold Pop-out as well, and at `min_width` it had spent all 422px it had, which
had pushed `min_width` from 420 to 430. With Pop-out on the strip, `min_width`
is back to 420, and the bar has room to spare, which the Looks combo takes: it
stretches with the floater instead of being the fixed 150px that was left over.
It is 148 at `min_width` and 188 at the default 460.

The row was sized in XUI Studio: the bar is `left="6" top="16"`, 26 tall, and
everything in it is 24 tall at `top="0"`. The buttons are 24×24. The combo
takes `left="1" right="-261"` and follows the right edge; `left="1"` puts it at
the floater's x 7, over the tab container's left edge. The nine buttons follow
`top|right` and are placed by `right` rather than `left_pad`: the last is 6 in
from the edge, each one before it 25 further in (24 and a gap of 1), and 10 more
where the group changes. **Four groups, separated by 11px where the buttons
inside a group are separated by 1.** That gap is the only thing that says they
are different kinds of thing: the first group acts on the Look, the second on
the grade's own history, the third opens another window, and the fourth finds a
place in this one. So keep it if you add another kind, and use 1px if you are
extending a group. A new button here comes out of the combo's width: add 25 to
the 261, or 35 if it starts a group of its own.

**Every `top` in this file is measured from under the title bar**, not from the
window's top edge. The floater sets no `legacy_header_height`, so once its
widgets are built, `LLFloater::initFloaterXML` grows the window upward by the
whole `header_height` (25px in the default skin), and `LLView::setRect` moves
none of the widgets. So the bar's `top="16"` is 16px below the title bar. A
floater that sets `legacy_header_height="18"`, as most of the viewer's do, only
grows by 7px, so there a `top` under 18 lands in the title bar.

**The strip's button is a child of the `tab_container` itself,** after the four
pages. That is load-bearing three ways:

- A `tab_container` makes a tab of every *panel* it is given
  (`LLTabContainer::addChild`) and keeps any other child as it is, so a strip
  button must be a direct child. Wrapped in a panel, it would become a fifth
  tab.
- `tab_padding_right="37"` keeps the strip's right end clear. `fill_width`
  shares out only what is left (`stripRoom`), so the tabs stop 8px short of the
  button. The padding is the button's width plus its 6px margin plus that 8px,
  less the page border; resize the button and it has to change with it. The tabs' mouse capture (`tab_rect` in `handleMouseDown`) ends where
  the padding begins, so a button there gets its own clicks. Put a button inside
  the tabs' span and the container captures the mouse on press, and the button
  never sees its release. `lltabcontainer_test` test 28 pins all three.
- Laid over the strip as a sibling of the `tab_container` instead, it would
  overlap its rect, and XUI Studio's lint reports every pair of visible
  siblings that intersect.

Pop-out and Find both sit at `right="-6"`, and the bar and the tab container
both end at `right="-4"`, so the right edge of the two rows is one column. The
tab container starts at `left="7"`, one pixel in from the bar's `left="6"`, so
that its left edge lines up with the combo's.

**Pop-out is 24×24 like the bar's buttons, and that was judged by eye.** The
strip is 23 tall (`tab_height`; the page starts right under it), so by the
numbers a 24px button at `top="0"` reaches a pixel into the page. In the
viewer, though, 24 reads as the same height as the tabs. 23, the strip's exact
height, was tried and looked misaligned. XUI Studio's lint only reports
siblings that overlap by 2px or more, so it says nothing about this one. Change
this size by looking at it, not by arithmetic.

Everything but the combo is an **18px icon on a button with an empty label**,
and the tooltip carries the name. That is not decoration. Four text labels cost
192px of the bar; the same four icons cost 99. It also sidesteps §5's silent clipping the
day this floater is translated and "Save As" becomes "Speichern unter" — a bar
of labels has no reflow and no scrollbar to save it. Take the overlays from the
viewer's existing set (`Script_Save`, `Conv_toolbar_plus`, `TrashItem_Off`,
`Refresh_Off`, `Script_Undo`, `Script_Redo`, `Conv_toolbar_call_log`,
`Command_Stats_Icon`, `Command_Search_Icon`, `Conv_toolbar_arrow_ne`/`_sw`)
rather than adding art; picking a glyph that already means the right thing
elsewhere is most of the work. The pop-out pair is the IM window's tear-off
icon, and swaps with the state of the tab that is up.

**A button that opens another floater needs no C++ at all** — `Floater.Toggle`
is a global commit callback in `llui.cpp`, with the floater's registered name as
`parameter`. Use that one and not `Floater.ToggleOrBringToFront`; §4d explains
why the second can only ever open.

**The tab strip** below the bar is a stock `tab_container` with `fill_width`, so
the four tabs share its whole width rather than bunching at the left. A tab's
tooltip is written on its page element in the floater XML: `addTabPanel` copies
a page's `tool_tip` onto its tab button, and `postBuild` then clears it from the
page. The second half matters. `LLView::handleToolTip` offers a parent's tooltip
before its children's, so a tooltip left on the page pops up over every caption,
gap and header on it that has no tooltip of its own. A fifth tab gets both
behaviours by being declared the same way.

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

### 4i. Finding widgets: the directory

**After `postBuild`, nothing in the floater looks a widget up by name.**
`ALLightboxDirectory` walks the four pages once, at the top of `postBuild`, and
from then on everything the floater needs is held by pointer: each section
(`sec_<id>`, its accordion tab, its accordion, the page it is on) and each bound
setting (the control, and what its row calls it). Reset All finds its panels
there, the tonemapper rows and the Looks bar buttons are cached beside it, and
the only `getChild`/`findChild` calls left are in `postBuild` and the `setup*`
functions it calls — `grep -nE "getChild|findChild" alfloaterlightbox.cpp` is
the check.

The reason is that a page can leave the floater. A tab taken out into a window
of its own takes its widgets with it, and a search from the floater's root no
longer reaches them; a pointer keeps working wherever the widget goes. So a new
callback that needs a widget should take it from the directory, or cache it in
`postBuild`, never search for it when it runs.

The directory also reads each setting's **caption**, which is what finding a
setting by name and naming an undo step both show, so a new row should caption
itself in one of the ways it understands, in this order:

1. The control's own label — a slider's `label`, a checkbox's `label`, a colour
   wheel's `label` (a text box the control holds and names `...label`).
2. Otherwise a `text` on the same line to its left: the row's middle falls
   within the text's height and the text ends at the control's left edge. This
   is how dropdown, colour and vector rows are captioned already. A vector
   row's spinners are captioned this way even though they have labels, because
   their labels are channel letters.
3. Otherwise, for a switch on a section header, the section's title.
4. Otherwise the setting's key made into words — the `Render`/`Alchemy` prefix
   dropped and the words split, so `RenderReferenceWipeMode` reads "Reference
   wipe mode". Serviceable, but a sign the row should caption itself properly.
   Of the 170 settings on the four tabs, only the reference still's mode
   dropdown ends up here, because it shares its line with the Grab and Clear
   buttons.

A trailing colon is dropped, so `Radius:` reads as `Radius`.

### 4j. Popovers, and finding a setting

**Find** (the search button, or Ctrl+F) lists every section and every setting
from the directory, ranked against what is typed by XUI Studio's `ALQuickOpen`,
in a popover hanging from the top bar. A setting's second column is its
section's title, which is what tells the five Strength rows apart; sections are
listed as well as settings because the list matches what a row is *called*, and
"bloom" should reach the Bloom section though none of its rows says bloom.
Return goes to the choice: its tab is chosen, its section opened (only its own
accordion tab, since an Advanced section is a sibling of its essentials, not
inside them), the accordion scrolled to the row, the row given the keyboard, and
its caption lit for a couple of seconds (`SearchableControl::setHighlighted`,
the same highlight Preferences search uses). A section is shown by its header.

Popovers are XUI Studio's `ALPopover`: a chrome-less window under an anchor
that closes when it loses the keyboard. The floater wraps it in three rules,
which any new popover here should go through `showPopover` to get:

- **One at a time.** Opening one closes the last (`mPopover`, `mPopoverKind`),
  and `onPopoverClosed` ignores a popover that has already been replaced.
- **Handles, never `this`.** The popover is a top-level window and can outlive
  the floater by a frame; its closed callback holds an `LLHandle` to the
  floater. `onClose` settles any popover still up, and the destructor `die()`s
  one as a fallback — `die()`, so nothing calls back into a floater half torn
  down.
- **The floater's shortcuts come along.** A key pressed in a popover never
  climbs to the Lightbox, since the popover is a window of its own, so each
  popover can carry a key hook (it then claims accelerators, for the reason in
  §4f). Find's hook takes Ctrl+F back to the field; History's steps the stack
  on Ctrl+Z / Ctrl+Y, goes to the selected step on Return, and swaps itself for
  Find on Ctrl+F.

Two more things learned the hard way. A popover given a **title** through
`ALPopover::show` lays its content over the title bar, so Find and History have
none; one that wants a title lays its own controls out under
`getHeaderHeight()`. And a popover whose content is an `LLPanel` never sees
Escape — the panel takes it and drops the keyboard, and the popover then closes
as *settled*, not escaped. A popover that has to tell Escape apart must hold its
controls directly rather than in a panel.

**The colour popover** is both of those: `ALLightboxColorPopover` is titled with
the row's caption and holds XUI Studio's `ALColorPicker` directly, so Escape
reaches it. Its session is kept by the floater (`mColorKey`, `mColorOriginal`):

- The picker writes the setting on every move, as every row here does, under
  `mColorWriting`, which the undo recorder skips. The picker's own value is
  *text* ("r, g, b, a"); the colour is read from `color()` and written as three
  numbers, the way the swatch writes a Color3.
- Escape writes the original back. Anything else that closes it records one
  step, from the colour it opened on to the one it was left at, and none if
  they are the same.
- No undo group is held open for the length of a pick. A popover can die
  without saying it closed, and a group left open would swallow every later
  write into itself.
- A pick still open when the stack is stepped is put back first, since the
  stack has not heard of it yet.

**Its size is the ring's size.** `ALColorPicker` gives its channel sliders the
first 218px of the width, and the ring whatever is left, up to the picker's
height. So the picker's width is the ring plus 218. It opens 440 by 220, a ring
as tall as the picker, and can be dragged down to 330 by 150. It started at 300
wide, and the ring was 74px across. The popover is resizable, the picker grows
with it, and the next colour opens at the size the last one was left at (for
the session: `sWidth`/`sHeight`, the way XUI Studio's colour field keeps
its size). Anything added to the popover must not eat into that width, or the
ring is what pays for it.

### 4k. Tabs in windows of their own

The pop-out button (at the end of the tab strip) takes the tab that is up out into a window of
its own, so Look and Lens can be open side by side, or a tab parked on another
monitor. Each page is wrapped in XUI Studio's `ALDockPanel` at the very end of
`postBuild`; popping out *moves* the page's contents into an `ALPanelFloater`
titled "Lightbox: Look", and the page left behind shows an `ALEmptyState` with a
"Put it back" button. Closing the window puts the page back too, and so does
the pop-out button, whose icon and tooltip follow the state of the tab that is
up. Find reaches into a page that is out by raising its window.

What this asks of the rest of the floater:

- **Nothing may look a widget up by name after `postBuild`** (§4i). A search
  from this floater's root does not reach a page that is out, and it is not an
  error when it fails — it just finds nothing, and Reset All quietly resets
  nothing. That was the reason for the directory.
- **Pages go back before the floater closes.** `onClose` saves which pages are
  out and where, then docks them all: a page left in another window would
  outlive the callbacks it is wired to. It does so even when the viewer is
  quitting, because windows are closed in no particular order then and
  `onClose` is the last point at which both this floater and every torn-off
  window are sure to be whole. The destructor docks again as a fallback, and
  holds the panes by handle for it — a pane still out belongs to its window,
  which may already be gone.
- **Keys still reach the floater.** `ALPanelFloater` claims accelerators and
  passes a key it does not handle to the floater it came from, so Ctrl+Z,
  Ctrl+Y and Ctrl+F work from a torn-off tab.
- **The keyboard is not taken along.** A control with focus is let go before
  its page leaves, so no keystroke lands in a window nobody is looking at.
- **Where they were is remembered**: `ALLightboxState["panes_out"]`, keyed by
  page name, holds whether each page was out and its window's rect, and
  `restorePanes` takes them out again when the Lightbox next opens.
  `refreshPaneRow` (polled from `draw()`, since a torn-off window's close box
  docks without telling anyone) saves whenever the set of pages out changes.

### 5. Height math (the part everyone gets wrong)

**The floater now does this sum itself.** `fitSections()` runs in `postBuild`
and sizes every `sec_*` panel to its lowest row's bottom + 8, and its
accordion tab to that + 29, by the same `size_changes` notification anything
that grows inside an accordion sends (the tab takes the new height if it is
open, or remembers it for when it opens; `llaccordionctrltab_test` pins that).
So a section whose declared heights are wrong is no longer clipped or padded in
the viewer. **Keep the declared heights right anyway**: they are what the first
layout pass and XUI Studio's preview use, and XUI Studio's lint measures
against them. The rules below are how.

- `accordion_tab` height **must be** inner panel height **+ 29**
  (25px header + 2+2 padding). The tab's rect *is* its expand height and
  `fit_panel` squeezes the panel into what remains; an undersized tab clips
  the bottom rows and the overflow draws over the sections below (panels do
  not clip children).
- Compute the panel height by walking the `top_pad` chain to the **last
  widget's bottom**, then add 8. `top_pad` chains from the *previous widget*;
  a setting row is 18px tall at `top_pad="8"`, so rows pitch **26px**. (The old
  slider rows pitched 26 too — their reset button hung 1px below the 16px
  slider, and it was the button the next row chained from. A setting row is
  exactly that button's rect, which is why converting a row moved nothing.)
- Worked example: setting row at `top="7"` (7-25), second row `top_pad="8"`
  (33-51), Reset All `top_pad="10"` (61-79) → panel height 87, tab height 116.
- **Side-by-side widgets break the chain.** A bank of three wheels all use
  `top="8"`, so the bank's bottom is *one* wheel's height, not three; the next
  row's `top_pad` chains from the last one declared. Get this wrong and the
  panel is either 350px too tall or clipped.
- **A row of buttons has to be sized for `min_width`, not for the default.**
  The arithmetic that matters is 420 − 28 for the chrome − 15 for the accordion
  scrollbar − 16 for `left="8"`/`right="-8"`, which leaves **361px**. (For a
  while `min_width` was 430, for the top bar, §4g. The rows here were all laid
  out to 361 throughout.)
  The Sky tab's four presets are 85 wide with 6px gaps and end at 366. Laid out
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
  `clean_plate` gates in **three** places, because post-grade is two passes
  and the flare keeps history. Effects in the final blit (lens distortion,
  vignette, grain, CVD, the preview modes) gate in `renderFinalize`; effects
  applied *inside* the colorCorrect program (chromatic aberration, lens flare,
  lens dirt, the cross-filter composite) gate in `colorCorrect`, because they
  run in every variant including the no-post ones — the first two leaked
  through the first time for exactly that reason; and `generateLensFlareState`,
  which owns the flare's history, skips its update on a no-post frame rather
  than clearing it, so the flare does not blink after the capture. The one
  deliberate exception is dither,
  which is a quantisation aid rather than a look and which an 8-bit PNG wants
  either way.
- **A generation pass is a third case, and gating it is a mistake.**
  `generateLensDirt` deliberately ignores `gSnapshotNoPost`: the flag is true
  for the single frame a no-post snapshot is taken, so releasing the plate for
  it would buy a full regeneration on the very next frame — a hitch every time
  someone takes one. What has to be gated is the *use* of the plate, in
  `colorCorrect`, not its production.

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

Any plain `slider` with `max_val` below 1.0 (or ≤ 0) **must** set an explicit
`text_width` (56 fits a signed 4-decimal value). Without it `LLSliderCtrl`
auto-sizes the value box from `log10(max_value)` and truncates the number. A
`setting_row` works the width out itself in exactly those cases (§4), so this
is only for sliders that are not rows — vector-bank sliders, for instance.

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
so renames get caught. LLSD-typed keys (the tone curve point lists) round-trip
as arrays like any other value; Debug Settings shows them read-only as
notation, so the graph, the presets and Looks are their only editors.

`audit_bundled_looks()` runs from the `LLPresetsManager` constructor and checks
the bundled Looks two ways: any whitelisted key a Look is missing, and any key
whose stored `Comment` no longer matches the live setting's. The second is a
maintenance rule worth stating plainly — **rewording a setting's `Comment` in
`settings_alchemy.xml` obliges you to re-save the three bundled Looks**, which
each carry their own copy that nothing reads. Five keys had already drifted
that way before the check existed, silently, because presence was all anything
verified.

Bundled starter Looks live in `app_settings/looks/` as full whitelist
snapshots ({Comment, Persist, Type, Value} per key, URI-escaped filenames).
**Add your keys to all three at their defaults**, so the files stay complete
snapshots of the whitelist. `loadLooksPreset` applies the whitelisted keys the
file carries and resets to default every whitelisted key it does not mention,
so a Look saved before a key existed applies that key at its default — which
is what keeps "Neutral" neutral for a user whose seeded copies predate the
key. Three fences: only a file carrying at least half the whitelist counts as
a snapshot (a truncated or hand-trimmed file applies what it has and touches
nothing else), a key that is present but malformed is skipped as it always
was, and the `RenderColorGrade` master switch is never reset by absence. To refresh the bundled
files after tuning: save the Look in the viewer, then copy the saved file from
`<user_settings>/presets/looks/` over the bundled one.

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
- For a setting row: drag it off its default and watch its reset glyph appear;
  drag it back and watch it go; press it and confirm the default returns as one
  undo step called "Reset <caption>". With the row's gate off, the glyph greys
  with the slider.
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
  the handle is put back where the setting actually landed. A plain click on a
  handle must not mark the Look `*` or add an undo step.
- For the tone curve: double-click empty space to add a point and drag it;
  double-click it to remove it; confirm both end points refuse removal and the
  17th point is refused; confirm a drag, an add, a remove and a preset are each
  one Ctrl+Z step; switch channel and confirm the handles and ghosts swap;
  change graphics preset with a curve active and confirm the image keeps it
  (the texture is recreated and re-baked); untick the section header and set
  Amount to 0 and confirm both bypass.
- For the split-tone edges: drag each past the balance handle and confirm the
  width clamps at the slider's range and the handle is put back; at a low
  balance with a wide shadow ramp the left handle is held at the plot edge,
  locked and dimmed, while the slider keeps the true width — the slider is
  the control for it, and dragging the held handle writes nothing.
- For anything measured (scopes, vectorscope): change the thing it measures and
  confirm the readout moves the way the control says it should.
- For a section switch: **untick** it (ticked is on) and confirm the image
  changes **and the Looks `*` does not appear**, and that the Look tab's badge
  counts it. Then close the Lightbox with it still unticked and confirm the
  render comes back — state parked outside the floater is the failure mode here,
  and it looks like a renderer bug rather than a UI one.
- For the Sky tab's day cycle: freeze, wait past the point the sky would have
  moved, and confirm it has not. Then untick and confirm you get back *what you
  had*, not the region default — set a Personal Lighting sky first, since that
  is the case a missing capture loses. Fly through an altitude band while frozen
  and confirm the sky holds; check the water stopped too, not just the sky.
  Finally, confirm the presets land somewhere plausible on a region whose day
  cycle is not the default one, because a hardcoded fraction would also look
  right on a default region.
- For anything that acts on a section or a row from C++: do it once with the
  tab in place and once with it popped out, since a lookup by name after
  `postBuild` fails silently only in the second case.
- For a colour row: click the swatch, drag, and watch the render follow; press
  Escape and confirm the colour comes back with no undo step; pick again and
  click away, and confirm it is one step.
- For a new discrete action: do it, open History, and confirm it is one step
  under its own name, and that double-clicking the step before it undoes it.
- For a new row: Ctrl+F, type its caption, and confirm it is listed under the
  name its row shows (not its setting key — §4i says what a row needs for that)
  and that Return lands on it, scrolled into view with its caption lit.
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
