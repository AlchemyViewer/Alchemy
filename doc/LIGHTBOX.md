# Lightbox maintainer guide

How to add a new effect section to the Lightbox floater. The floater is built
so that exposing a new post-processing effect is (almost always) a pure XUI
change: no new C++, no layout mathematics beyond the rules below, resets and
live preview for free.

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

The C++ never grows per effect. It provides exactly:

- `LightBox.ResetControlDefault` — per-row reset; `parameter` = setting name.
- `LightBox.ResetSection` — data-driven section reset; `parameter` = `sec_<id>`.
  It walks the panels named `sec_<id>` and `sec_<id>_adv`, collects every
  descendant's bound control (plus settings named by `vec3_*` spinners), and
  resets them. New rows enroll automatically.
- `LightBox.CommitVec3` — the component binder for Vector3/Color3 settings,
  driven entirely by the widget naming contract `vec3_<SettingName>_<0|1|2>`.
  `postBuild` discovers the spinners, seeds them, and keeps them synced with
  the control both ways. Partial exposure is supported: expose only the
  meaningful components and the binder preserves the rest on write.
- The Looks bar and tonemapper-row greying (effect-specific, already done).

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

**Group headers** (inside a long Advanced panel): a `view_border`
(`bevel_style="none" height="0"`, `top_pad="12"`) then a bold `text`
(`font="SansSerifBold"`, `top_pad="6"`); the first group needs no border.

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
snapshots ({Comment, Persist, Type, Value} per key, URI-escaped filenames,
seeded into the user presets dir on first run). To refresh them after adding
keys: tune and save the Look in the viewer, then copy the saved file from
`<user_settings>/presets/looks/` over the bundled one.

### 10. Verify

- XML well-formedness before launching (any XML-capable tool).
- Two-way binding: move the row, watch the key in Debug Settings; edit the key
  there, watch the row follow.
- Gating flips live; section Reset All touches exactly the section's keys
  (including its Advanced sibling); the tab opens to full height with nothing
  clipped or drawing over the next section.
- If added to the whitelist: save a Look, change the setting (dirty `*`
  appears), re-apply (value returns).
- **Developer-build staging trap:** non-package builds do not restage XUI or
  `app_settings` next to the executable. After editing, copy the changed files
  into `build-.../newview/<config>/skins/...` and `.../app_settings/...` or
  the viewer keeps loading the stale copies.
