#!/usr/bin/env python3
"""
check_look_ranges.py -- hold the Look ranges to the Lightbox's rows.

A Look from outside is held, setting by setting, to what the Lightbox's row for that setting offers
(ALLookFormat::settingRange, a table in indra/newview/allookformat.cpp): a slider's min_val and
max_val, a colour wheel's min_value and max_value, a swatch's 0 to 1 per channel, a dropdown's item
values. The table is written by hand, and nothing in the viewer notices when a row's range moves and
the table does not. This does:

  (a) every number a Look carries (F32, S32, U32, Vector3 and Color3 settings on the list in
      LLPresetsManager::getLooksControlNames) has a range, and nothing else has one;
  (b) each range is its row's: the slider's, the wheel's, the swatch's, or the dropdown's values;
  (c) every value in every bundled Look (app_settings/looks) is inside its range, so that applying
      one of them never moves anything.

Standard library only. Run from anywhere: the repository is found from this file's place in it.
Exit status 0 on RESULT: OK, 1 on RESULT: MISMATCH.
"""

import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
NEWVIEW = ROOT / "indra" / "newview"
XUI = NEWVIEW / "skins" / "default" / "xui" / "en"
PANELS = ["panel_lightbox_look.xml", "panel_lightbox_lens.xml"]
NUMERIC = {"F32", "S32", "U32", "Vector3", "Color3"}


class Checker:
    def __init__(self):
        self.failures = 0

    def expect(self, what, ok, detail=""):
        print("  %s %s%s" % ("ok  " if ok else "FAIL", what, (" -- " + detail) if detail else ""))
        if not ok:
            self.failures += 1


def table():
    """The ranges in allookformat.cpp: name -> (lo, hi, [choices])."""
    text = (NEWVIEW / "allookformat.cpp").read_text(encoding="utf-8")
    entry = re.compile(r'\{\s*"(\w+)",\s*R\{\s*([-\d.]+),\s*([-\d.]+),\s*\{([^}]*)\}\s*\}\s*\}')
    ranges = {}
    for name, lo, hi, choices in entry.findall(text):
        ranges[name] = (float(lo), float(hi), [int(c) for c in choices.replace(" ", "").split(",") if c])
    return ranges


def whitelist():
    text = (NEWVIEW / "llpresetsmanager.cpp").read_text(encoding="utf-8")
    body = re.search(r"void LLPresetsManager::getLooksControlNames\(.*?\{(.*?)\n\}", text, re.S).group(1)
    return re.findall(r'"([A-Za-z0-9_]+)"', body)


def settings_types():
    types = {}
    for name in ("settings.xml", "settings_alchemy.xml"):
        top = ET.parse(NEWVIEW / "app_settings" / name).getroot().find("map")
        items = list(top)
        for i in range(0, len(items), 2):
            fields = list(items[i + 1])
            for j in range(0, len(fields), 2):
                if fields[j].text == "Type":
                    types[items[i].text] = fields[j + 1].text
    return types


def rows():
    """Each setting's row: name -> (lo, hi, [choices]), as the row offers it."""
    found = {}
    for panel in PANELS:
        for element in ET.parse(XUI / panel).getroot().iter():
            name = element.get("control_name")
            if not name:
                continue
            if element.tag == "setting_row" and element.get("min_val") is not None:
                found[name] = (float(element.get("min_val")), float(element.get("max_val")), [])
            elif element.tag == "color_wheel":
                found[name] = (float(element.get("min_value", "-0.5")), float(element.get("max_value", "0.5")), [])
            elif element.tag == "color_swatch":
                found[name] = (0.0, 1.0, [])
            elif element.tag == "combo_box":
                values = [item.get("value") for item in element.iter() if item.tag.endswith("combo_box.item")]
                if values and all(re.fullmatch(r"-?\d+", v or "") for v in values):
                    choices = sorted(int(v) for v in values)
                    found[name] = (float(choices[0]), float(choices[-1]), choices)
    return found


def bundled_values():
    """Every bundled Look's values: (file, name, value) with value a number or a list of numbers."""
    out = []
    for path in sorted((NEWVIEW / "app_settings" / "looks").glob("*.xml")):
        top = ET.parse(path).getroot().find("map")
        items = list(top)
        for i in range(0, len(items), 2):
            name = items[i].text
            fields = list(items[i + 1])
            for j in range(0, len(fields), 2):
                if fields[j].text != "Value":
                    continue
                value = fields[j + 1]
                if value.tag in ("real", "integer"):
                    out.append((path.name, name, float(value.text or 0)))
                elif value.tag == "array":
                    out.append((path.name, name, [float(v.text or 0) for v in value if v.tag in ("real", "integer")]))
    return out


def main():
    ck = Checker()
    ranges, names, types, offered = table(), whitelist(), settings_types(), rows()

    print("(a) every number a Look carries has a range, and nothing else")
    numeric = [n for n in names if types.get(n) in NUMERIC]
    missing = [n for n in numeric if n not in ranges]
    ck.expect("every numeric setting on the list has a range (%d)" % len(numeric), not missing, ", ".join(missing))
    stray = [n for n in ranges if n not in numeric]
    ck.expect("no range for a setting a Look does not carry as a number", not stray, ", ".join(stray))

    print("(b) each range is its row's")
    for name in sorted(ranges):
        if name not in offered:
            ck.expect("%s has a row to hold it to" % name, False)
            continue
        ck.expect("%s" % name, ranges[name] == offered[name], "table %s, row %s" % (ranges[name], offered[name]))

    print("(c) the bundled Looks are inside their ranges")
    outside = []
    for look, name, value in bundled_values():
        if name not in ranges:
            continue
        lo, hi, choices = ranges[name]
        for component in (value if isinstance(value, list) else [value])[:3]:
            if choices and int(component) not in choices:
                outside.append("%s %s=%g" % (look, name, component))
            elif not lo <= component <= hi:
                outside.append("%s %s=%g" % (look, name, component))
    ck.expect("every bundled value is inside its range", not outside, "; ".join(outside))

    print("RESULT: %s" % ("OK" if ck.failures == 0 else "MISMATCH"))
    return 0 if ck.failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
