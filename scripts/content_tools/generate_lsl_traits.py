#!/usr/bin/env python3
"""Writes indra/alscript/allsltraits.inc and allsluuids.inc from lsl_definitions.yaml.

The optimizer folds a call to a library function only when the definitions
say the function has no side effects, and refuses to drop a call whose
result is unused when they say the result is the point. Those flags live
in lsl_definitions.yaml, which the lsl-definitions port ships; this reads
them without a YAML parser, since the file is regular, and writes a table
the library compiles in.

The LSL to SLua assistant reads the same table for what SLua makes of each
function: whether SLua's `ll` counts an index from one where LSL counts from
nought (an argument or the result with index-semantics), answers a boolean
where LSL answers 1 or 0 (a result with bool-semantics), or gives booleans
in the list it answers (bool-semantics on a list); whether `ll` lacks it,
leaving it to `llcompat` alone, or SLua has it nowhere -- read from what
secondlife.d.luau, beside the YAML, declares in each; and what SLua would
have used in its stead (slua-deprecated's `use`). And the constants LSL
types a string that SLua types a uuid, NULL_KEY among them, for
allsluuids.inc. Run it whenever the definitions change:

    python3 scripts/content_tools/generate_lsl_traits.py \
        build-<OS>-<preset>/vcpkg_installed/<triplet>/share/lsl-definitions/lsl_definitions/lsl_definitions.yaml
"""

import re
import sys
from pathlib import Path

# Functions whose answer is their arguments' alone, which the definitions
# do not mark pure: most say only that their result must be used, and
# llAcos, llAsin and llDumpList2String say nothing. Marked pure here, so
# that the optimizer may fold them where it knows how and the inliner may
# read them at another time. A function that reads the world, the clock
# or a random source is not one of these.
DETERMINISTIC = {
    "llAcos",
    "llAsin",
    "llComputeHash",
    "llDumpList2String",
    "llHMAC",
    "llHash",
    "llLinear2sRGB",
    "llList2ListSlice",
    "llList2ListStrided",
    "llListFindList",
    "llListFindListNext",
    "llListFindStrided",
    "llListSort",
    "llListSortStrided",
    "llListStatistics",
    "llMD5String",
    "llRound",
    "llSHA1String",
    "llSHA256String",
    "llVerifyRSA",
    "llsRGB2Linear",
}


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    source = Path(argv[1])
    out = Path(__file__).resolve().parents[2] / "indra" / "alscript" / "allsltraits.inc"
    uuids_out = out.with_name("allsluuids.inc")
    # What SLua declares in ll and in llcompat, by their bare names; and
    # what it marks deprecated in ll.
    declared = {"ll": set(), "llcompat": set()}
    deprecated = set()
    table = None
    for line in (source.parent / "secondlife.d.luau").read_text(encoding="utf-8").split("\n"):
        m = re.match(r"^declare (ll|llcompat): \{\s*$", line)
        if m:
            table = m.group(1)
            continue
        if table and line.startswith("}"):
            table = None
            continue
        m = re.match(r"^  ([A-Za-z_][A-Za-z0-9_]*):", line)
        if table and m:
            declared[table].add(m.group(1))
            if table == "ll" and re.match(r"^  [A-Za-z_][A-Za-z0-9_]*: @(deprecated|\[deprecated)", line):
                deprecated.add(m.group(1))
    if not declared["ll"] or not declared["llcompat"]:
        print("no ll or llcompat in " + str(source.parent / "secondlife.d.luau"))
        return 1
    functions = {}
    uuids = []
    name = None
    section = None
    constant = None
    for line in source.read_text(encoding="utf-8").split("\n"):
        m = re.match(r"^([A-Za-z-]+):\s*$", line)
        if m:
            section = m.group(1)
            name = constant = None
            continue
        if section == "constants":
            m = re.match(r"^  ([A-Za-z_][A-Za-z0-9_]*):\s*$", line)
            if m:
                constant = {"name": m.group(1), "type": None, "slua-type": None}
                uuids.append(constant)
                continue
            m = re.match(r"^    (type|slua-type): (\S+)\s*$", line)
            if constant and m:
                constant[m.group(1)] = m.group(2).strip("'\"")
            continue
        if section != "functions":
            continue
        m = re.match(r"^  ([A-Za-z_][A-Za-z0-9_]*):\s*$", line)
        if m:
            name = m.group(1)
            functions[name] = {
                "pure": False,
                "must-use": False,
                "native": False,
                "index-result": False,
                "index-args": 0,
                "bool": False,
                "return": None,
                "use": None,
                "reason": None,
            }
            in_deprecated = False
            argument = -1
            continue
        if name is None:
            continue
        m = re.match(r"^    (pure|must-use|native): (true|false)\s*$", line)
        if m:
            functions[name][m.group(1)] = m.group(2) == "true"
        # Each argument as it starts, counted from nought; an index counted
        # from nought in the result (four spaces in) or an argument (eight),
        # the arguments by their places; a result that is a boolean as 1 or 0.
        if re.match(r"^    - [A-Za-z_][A-Za-z0-9_]*:\s*$", line):
            argument += 1
        if re.match(r"^    index-semantics: true\s*$", line):
            functions[name]["index-result"] = True
        if re.match(r"^        index-semantics: true\s*$", line) and argument >= 0:
            functions[name]["index-args"] |= 1 << argument
        if re.match(r"^    bool-semantics: true\s*$", line):
            functions[name]["bool"] = True
        m = re.match(r"^    return: (\S+)\s*$", line)
        if m:
            functions[name]["return"] = m.group(1)
        if re.match(r"^    slua-deprecated:", line):
            in_deprecated = True
            continue
        if in_deprecated:
            m = re.match(r"^      (use|reason): (.*?)\s*$", line)
            if m:
                functions[name][m.group(1)] = m.group(2).strip("'\"")
            elif not line.startswith("      "):
                in_deprecated = False
    if not functions:
        print("no functions found in " + str(source))
        return 1
    for fn in DETERMINISTIC:
        if fn not in functions:
            print("not in the definitions: " + fn)
            return 1
        functions[fn]["pure"] = True
    lines = [
        "// Generated by scripts/content_tools/generate_lsl_traits.py from",
        "// lsl_definitions.yaml; do not edit. One row per library function:",
        "// its name, whether it has no side effects, whether its result must",
        "// be used, whether it needs a native implementation off LSO; what",
        "// SLua makes of it (ALLSLTraits::Slua); the arguments that are an",
        "// index, by their places; and what SLua would use, and why.",
        "// clang-format off",
    ]
    for fn in sorted(functions):
        t = functions[fn]
        slua = []
        if t["index-result"]:
            slua.append("SluaIndexResult")
        if t["index-args"]:
            slua.append("SluaIndexArgs")
        if t["bool"]:
            slua.append("SluaBoolList" if t["return"] == "list" else "SluaBool")
        bare = fn[2:] if fn.startswith("ll") else fn
        if bare not in declared["ll"]:
            slua.append("SluaRemoved" if bare in declared["llcompat"] else "SluaAbsent")
        elif bare in deprecated:
            slua.append("SluaDeprecated")
        use = '"%s"' % t["use"].replace('\\', '\\\\').replace('"', '\\"') if t["use"] else "nullptr"
        reason = '"%s"' % t["reason"].replace('\\', '\\\\').replace('"', '\\"') if t["reason"] else "nullptr"
        lines.append(
            '{ "%s", %s, %s, %s, %s, 0x%x, %s, %s },'
            % (
                fn,
                "true" if t["pure"] else "false",
                "true" if t["must-use"] else "false",
                "true" if t["native"] else "false",
                " | ".join(slua) if slua else "0",
                t["index-args"],
                use,
                reason,
            )
        )
    lines.append("// clang-format on")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    pure = sum(1 for t in functions.values() if t["pure"])
    print("%d functions, %d pure -> %s" % (len(functions), pure, out))
    named = sorted(c["name"] for c in uuids if c["type"] == "string" and c["slua-type"] == "uuid")
    uuids_out.write_text(
        "// Generated by scripts/content_tools/generate_lsl_traits.py from\n"
        "// lsl_definitions.yaml; do not edit. The constants LSL types a string\n"
        "// and SLua a uuid.\n" + "".join('"%s",\n' % n for n in named),
        encoding="utf-8",
    )
    print("%d string constants SLua types a uuid -> %s" % (len(named), uuids_out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
