#!/usr/bin/env python3
"""Writes indra/alscript/lsl/allsltraits.inc and allsluuids.inc from lsl_definitions.yaml.

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
have used in its stead (slua-deprecated's `use`), and whether that takes
other arguments. The performance lints
read how long each makes the script sleep, under LSO and under Mono,
where the two differ (sleep, mono-sleep). And the constants LSL
types a string that SLua types a uuid, NULL_KEY among them, for
allsluuids.inc; and, for allslitemargs.inc, each argument that names an
item among the object's contents and of what kind (inventory-kind, which
the port's patch adds), for the names Script Studio offers in a string.
Run it whenever the definitions change:

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


# The kinds of item an argument may name (inventory-kind), as
# ALLSLTraits::Item calls them: `any` for an item of whatever kind.
ITEM_KINDS = {
    "any": "Any",
    "sound": "Sound",
    "texture": "Texture",
    "animation": "Animation",
    "notecard": "Notecard",
    "object": "Object",
    "material": "Material",
    "settings": "Settings",
    "landmark": "Landmark",
    "script": "Script",
}


def ll_params(line):
    """The parameters' types of an ll member's signature, in order: after
    its attributes, whose reasons may hold brackets and quotes, and any
    generics."""
    rest = line.split(":", 1)[1].strip()
    while rest.startswith("@"):
        if rest.startswith("@["):
            depth, quote, at = 0, None, 1
            while at < len(rest):
                c = rest[at]
                if quote:
                    quote = None if c == quote else quote
                elif c in "'\"":
                    quote = c
                elif c == "[":
                    depth += 1
                elif c == "]":
                    depth -= 1
                    if depth == 0:
                        break
                at += 1
            rest = rest[at + 1:].strip()
        else:
            rest = rest.split(None, 1)[1] if " " in rest else ""
    if rest.startswith("<"):
        rest = rest[rest.index(">") + 1:].strip()
    if not rest.startswith("("):
        return []
    depth, at = 0, 0
    for at, c in enumerate(rest):
        depth += c in "({[<"
        depth -= c in ")}]>"
        if depth == 0:
            break
    inner, params, depth, start = rest[1:at], [], 0, 0
    for i, c in enumerate(inner + ","):
        if c in "({[<":
            depth += 1
        elif c in ")}]>":
            depth -= 1
        elif c == "," and depth == 0:
            piece = inner[start:i].strip()
            if piece:
                params.append(piece.split(":", 1)[1].strip() if ":" in piece else piece)
            start = i + 1
    return params


def seconds(value):
    """A sleep as C++ writes a float: 0.2f, 1.0f."""
    return repr(float(value))


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    source = Path(argv[1])
    out = Path(__file__).resolve().parents[2] / "indra" / "alscript" / "lsl" / "allsltraits.inc"
    uuids_out = out.with_name("allsluuids.inc")
    # What SLua declares in ll and in llcompat, by their bare names; and
    # what it marks deprecated in ll.
    declared = {"ll": set(), "llcompat": set()}
    deprecated = set()
    # The arguments of each ll function SLua takes either a string or a uuid
    # for, as bits by their places; and each one's parameters' types.
    text_args = {}
    params = {}
    # What secondlife.d.luau's deprecation says to use, which is what Luau's
    # lint says and its fix puts in.
    luau_use = {}
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
            if table == "ll":
                bits = 0
                params[m.group(1)] = ll_params(line)
                said = re.search(r"use='([^']+)'", line)
                if said:
                    luau_use[m.group(1)] = said.group(1)
                for i, t in enumerate(params[m.group(1)]):
                    if re.search(r"\bstring\b", t) and re.search(r"\buuid\b", t) and "{" not in t:
                        bits |= 1 << i
                text_args[m.group(1)] = bits
    if not declared["ll"] or not declared["llcompat"]:
        print("no ll or llcompat in " + str(source.parent / "secondlife.d.luau"))
        return 1
    functions = {}
    # Each argument that names an item of the object's, by its function,
    # its place and the kind of item.
    item_args = []
    uuids = []
    # Each event's parameters LSL types a key that SLua passes as a string.
    event_text = []
    event = None
    event_param = -1
    param_type = None
    name = None
    section = None
    constant = None
    for line in source.read_text(encoding="utf-8").split("\n"):
        m = re.match(r"^([A-Za-z-]+):\s*$", line)
        if m:
            section = m.group(1)
            name = constant = None
            continue
        if section == "events":
            m = re.match(r"^  ([A-Za-z_][A-Za-z0-9_]*):\s*$", line)
            if m:
                event, event_param = m.group(1), -1
                continue
            if re.match(r"^    - [A-Za-z_][A-Za-z0-9_]*:\s*$", line):
                event_param += 1
                param_type = None
                continue
            m = re.match(r"^        type: (\S+)\s*$", line)
            if m:
                param_type = m.group(1)
            m = re.match(r"^        slua-type: (\S+)\s*$", line)
            if m and param_type == "key" and m.group(1).strip("'\"") == "string":
                event_text.append((event, event_param))
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
                "sleep": 0.0,
                "mono-sleep": None,
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
        m = re.match(r"^        inventory-kind: (\S+)\s*$", line)
        if m and argument >= 0:
            kind = m.group(1).strip("'\"")
            if kind not in ITEM_KINDS:
                print("an inventory-kind not known: %s of %s" % (kind, name))
                return 1
            item_args.append((name, argument, ITEM_KINDS[kind]))
        if re.match(r"^    bool-semantics: true\s*$", line):
            functions[name]["bool"] = True
        m = re.match(r"^    return: (\S+)\s*$", line)
        if m:
            functions[name]["return"] = m.group(1)
        m = re.match(r"^    (sleep|mono-sleep): ([0-9.]+)\s*$", line)
        if m:
            functions[name][m.group(1)] = float(m.group(2))
        if re.match(r"^    slua-deprecated:", line):
            in_deprecated = True
            folded = None
            continue
        if in_deprecated:
            m = re.match(r"^      (use|reason): (.*?)\s*$", line)
            if m:
                functions[name][m.group(1)] = m.group(2).strip("'\"")
                folded = m.group(1)
            elif line.startswith("        ") and folded:
                # A value folded onto the lines after its key.
                functions[name][folded] = (functions[name][folded] + " " + line.strip()).strip("'\"")
            elif not line.startswith("      "):
                in_deprecated = False
            else:
                folded = None
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
        "// index, and those SLua takes text for, by their places; what SLua",
        "// would use, and why; and the seconds it sleeps under LSO and Mono.",
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
        # What SLua's deprecation says to use, where it is ll's, taking
        # other arguments: its name alone put in place is no call that works.
        used = luau_use.get(bare, "")
        used = used[3:] if used.startswith("ll.") else None
        if used and bare in params and used in params and params[used] != params[bare]:
            slua.append("SluaUseDiffers")
        use = '"%s"' % t["use"].replace('\\', '\\\\').replace('"', '\\"') if t["use"] else "nullptr"
        reason = '"%s"' % t["reason"].replace('\\', '\\\\').replace('"', '\\"') if t["reason"] else "nullptr"
        lines.append(
            '{ "%s", %s, %s, %s, %s, 0x%x, 0x%x, %s, %s, %sf, %sf },'
            % (
                fn,
                "true" if t["pure"] else "false",
                "true" if t["must-use"] else "false",
                "true" if t["native"] else "false",
                " | ".join(slua) if slua else "0",
                t["index-args"],
                text_args.get(bare, 0),
                use,
                reason,
                seconds(t["sleep"]),
                seconds(t["mono-sleep"] if t["mono-sleep"] is not None else t["sleep"]),
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
    events_out = out.with_name("allsleventtext.inc")
    events_out.write_text(
        "// Generated by scripts/content_tools/generate_lsl_traits.py from\n"
        "// lsl_definitions.yaml; do not edit. Each event's parameters LSL\n"
        "// types a key that SLua passes as a string, by their places.\n"
        + "".join('{ "%s", %d },\n' % e for e in event_text),
        encoding="utf-8",
    )
    print("%d event parameters SLua passes as text -> %s" % (len(event_text), events_out))
    items_out = out.with_name("allslitemargs.inc")
    items_out.write_text(
        "// Generated by scripts/content_tools/generate_lsl_traits.py from\n"
        "// lsl_definitions.yaml; do not edit. Each argument that names an item\n"
        "// among the object's contents: its function, its place from nought,\n"
        "// and the kind of item (ALLSLTraits::Item).\n"
        "// clang-format off\n"
        + "".join('{ "%s", %d, Item::%s },\n' % a for a in sorted(item_args))
        + "// clang-format on\n",
        encoding="utf-8",
    )
    print("%d arguments naming an item -> %s" % (len(item_args), items_out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
