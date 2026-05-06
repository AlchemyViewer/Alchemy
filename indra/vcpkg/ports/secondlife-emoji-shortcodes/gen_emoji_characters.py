#!/usr/bin/env python3
"""\
@file gen_emoji_characters.py

$LicenseInfo:firstyear=2025&license=viewerlgpl$
Second Life Viewer Source Code
Copyright (C) 2025, Linden Research, Inc.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation;
version 2.1 of the License only.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
$/LicenseInfo$
"""
import argparse
import glob
import json
import os
import re
import sys

# Languages we ship emoji_characters.xml for. Kept in sync with
# `country_codes` in indra/newview/CMakeLists.txt.
ALLOWED_FOLDERS = {'da', 'de', 'en', 'es', 'fr', 'it', 'ja', 'pl', 'pt', 'ru', 'tr', 'zh'}

# Skin-tone modifiers (Fitzpatrick) — emojibase uses tone integers 1..5.
# We carry the integer through to LLSD; the C++ side maps 1..5 to
# light / medium-light / medium / medium-dark / dark.
#
# Gender (man/woman/person) and hair (*_red_haired, *_white_haired, *_bald,
# etc.) are NOT collapsed into Variants — those entries stay as their own
# top-level emoji in the picker grid. Only skin-tone is folded.


def hexcode_to_chars(hexcode):
    """Convert "1F468-200D-1F680" → "👨‍🚀"."""
    return ''.join(chr(int(part, 16)) for part in hexcode.split('-'))


def lookup_shortcodes(hexcode, emojibase_map, cldr_map):
    """Return the shortcode list (list[str]) or single string, or None."""
    sc = emojibase_map.get(hexcode) if emojibase_map else None
    if not sc:
        sc = cldr_map.get(hexcode)
    return sc


def normalize_shortcodes(sc):
    """Normalize shortcode lookup result to list[str]."""
    if sc is None:
        return []
    if isinstance(sc, list):
        return list(sc)
    return [sc]


def primary_shortcode(shortcodes):
    """First shortcode is the canonical one for matching/grouping."""
    return shortcodes[0] if shortcodes else None


def collect_skin_variants(entry, emojibase_map, cldr_map):
    """Walk entry['skins'] and return list of variant dicts.

    Single-tone children get Tone=N. Tone-pair children (`tone: [a, b]`)
    get Tone=a, Tone2=b. Children with no tone are skipped — they aren't
    a tone variant in the strict sense.
    """
    variants = []
    for child in entry.get('skins') or []:
        tone = child.get('tone')
        if tone is None:
            continue
        if isinstance(tone, list):
            if len(tone) < 2:
                continue
            tone1, tone2 = int(tone[0]), int(tone[1])
        else:
            tone1, tone2 = int(tone), 0
        child_sc = normalize_shortcodes(
            lookup_shortcodes(child['hexcode'], emojibase_map, cldr_map))
        variants.append({
            'character': hexcode_to_chars(child['hexcode']),
            'hexcode': child['hexcode'],
            'tone': tone1,
            'tone2': tone2,
            'shortcodes': child_sc,
        })
    return variants


def xml_escape_amp(s):
    """LLSD's XML serializer needs literal & escaped outside CDATA."""
    return s.replace(' & ', ' &amp; ')


def emit_variant_xml(variant):
    parts = ['\t\t\t\t<map>\n']
    parts.append('\t\t\t\t\t<key>Character</key>\n')
    parts.append(f'\t\t\t\t\t<string>{variant["character"]}</string>\n')
    if variant['tone']:
        parts.append('\t\t\t\t\t<key>Tone</key>\n')
        parts.append(f'\t\t\t\t\t<integer>{variant["tone"]}</integer>\n')
    if variant.get('tone2'):
        parts.append('\t\t\t\t\t<key>Tone2</key>\n')
        parts.append(f'\t\t\t\t\t<integer>{variant["tone2"]}</integer>\n')
    if variant['shortcodes']:
        parts.append('\t\t\t\t\t<key>ShortCodes</key>\n')
        parts.append('\t\t\t\t\t<array>\n')
        for code in variant['shortcodes']:
            parts.append(f'\t\t\t\t\t\t<string>:{code}:</string>\n')
        parts.append('\t\t\t\t\t</array>\n')
    parts.append('\t\t\t\t</map>\n')
    return ''.join(parts)


def emit_base_xml(entry, group, subgroup):
    short_codes = entry['shortcodes']
    xml_shortcodes = ''.join(f'\t\t\t\t<string>:{c}:</string>\n' for c in short_codes)

    out = ['\t\t<map>\n']
    out.append('\t\t\t<key>Character</key>\n')
    out.append(f'\t\t\t<string>{entry["character"]}</string>\n')
    out.append('\t\t\t<key>ShortCodes</key>\n')
    out.append('\t\t\t<array>\n')
    out.append(xml_shortcodes)
    out.append('\t\t\t</array>\n')
    out.append('\t\t\t<key>Categories</key>\n')
    out.append('\t\t\t<array>\n')
    if group:
        out.append(f'\t\t\t\t<string>{group}</string>\n')
    if subgroup:
        out.append(f'\t\t\t\t<string>{subgroup}</string>\n')
    out.append('\t\t\t</array>\n')

    if entry['variants']:
        out.append('\t\t\t<key>Variants</key>\n')
        out.append('\t\t\t<array>\n')
        for v in entry['variants']:
            out.append(emit_variant_xml(v))
        out.append('\t\t\t</array>\n')

    out.append('\t\t</map>')
    return ''.join(out)


def build_entries(data, cldr, emojibase, messages):
    """Two-pass build of top-level entries with their Variants.

    Returns list of entry dicts, each shaped:
        { 'character': str, 'hexcode': str,
          'shortcodes': list[str], 'group': str, 'subgroup': str,
          'variants': list[variant_dict] }
    """
    groups_msg = messages['groups']
    subgroups_msg = messages['subgroups']

    # Pass 1 — collect skin-tone variants per top-level entry, and remember
    # which hexcodes are themselves children-as-tone-variants so we can
    # exclude them from the top-level emission.
    skin_child_hexcodes = set()
    for item in data:
        for child in item.get('skins') or []:
            skin_child_hexcodes.add(child['hexcode'])

    entries = []
    for item in data:
        hexcode = item['hexcode']
        if hexcode in skin_child_hexcodes:
            # This is itself a tone variant — it gets emitted nested under
            # its parent below, not as a top-level entry.
            continue

        sc = normalize_shortcodes(lookup_shortcodes(hexcode, emojibase, cldr))
        if not sc:
            # Multi-codepoint sequences (ZWJ families, tag subdivision flags,
            # etc.) without a shortcode are common in upstream data — skip
            # quietly. Single-codepoint entries without a shortcode remain a
            # signal-bearing error (missing data we'd want to fix).
            if '-' not in hexcode:
                print(f"Error: Shortcode not found for hexcode '{hexcode}'", file=sys.stderr)
            continue

        skin_variants = collect_skin_variants(item, emojibase, cldr)

        group = next((g['message'] for g in groups_msg
                      if item.get('group') is not None and g['order'] == item['group']), '')
        subgroup = next((sg['message'] for sg in subgroups_msg
                         if item.get('subgroup') is not None and sg['order'] == item['subgroup']), '')

        entry = {
            'character': hexcode_to_chars(hexcode),
            'hexcode': hexcode,
            'shortcodes': sc,
            'group': xml_escape_amp(group),
            'subgroup': xml_escape_amp(subgroup),
            'variants': skin_variants,
        }
        entries.append(entry)

    # Gender + hair entries (man_X, woman_X, person_X, *_red_haired,
    # *_white_haired, *_bald, …) stay as their own top-level emoji.
    # Skin-tone children remain folded into each entry's Variants list
    # via collect_skin_variants above — that's the only axis we collapse.
    return entries


def render_xml(entries):
    parts = []
    parts.append('<?xml version="1.0" ?>\n')
    parts.append('<llsd xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="llsd.xsd">\n')
    parts.append('\t<!--\n')
    parts.append('\tNOTE: changes made to this file locally will be overwritten\n')
    parts.append('\twhen CMake is invoked during autobuild.\n')
    parts.append('\tTo modify these files, update the 3p package here:\n')
    parts.append('\thttps://github.com/secondlife/3p-emoji-shortcodes\n')
    parts.append('\tand add the resulting artifact to autobuild.xml\n')
    parts.append('\t-->\n')
    parts.append('\t<array>\n')
    parts.append('\n'.join(emit_base_xml(e, e['group'], e['subgroup']) for e in entries))
    parts.append('\n\t</array>\n</llsd>')
    return ''.join(parts)


def process_folder(data_file, cldr_file, emojibase_file, messages_file):
    with open(data_file, 'r', encoding='utf-8') as f:
        data = json.load(f)
    with open(cldr_file, 'r', encoding='utf-8') as f:
        cldr = json.load(f)
    if os.path.isfile(emojibase_file):
        with open(emojibase_file, 'r', encoding='utf-8') as f:
            emojibase = json.load(f)
    else:
        emojibase = None
    with open(messages_file, 'r', encoding='utf-8') as f:
        messages = json.load(f)

    return build_entries(data, cldr, emojibase, messages)


def run_self_test(entries):
    """Sanity-check the entry list and print a summary.

    Returns 0 on success, 1 on assertion failure.
    """
    failures = []

    # 1. No top-level entry should also appear as a Variant of itself or
    #    of any other entry.
    top_level_hex = {e['hexcode'] for e in entries}
    variant_hex = set()
    for e in entries:
        for v in e['variants']:
            if v['hexcode'] in variant_hex:
                failures.append(f"variant hexcode {v['hexcode']} appears twice")
            variant_hex.add(v['hexcode'])
    overlap = top_level_hex & variant_hex
    if overlap:
        failures.append(f"top-level/variant overlap: {sorted(overlap)[:5]}...")

    # 2. Every variant must carry a tone (tone1 only) or a tone pair
    #    (tone1 + tone2). Skin-tone is the only axis we fold now.
    for e in entries:
        for v in e['variants']:
            if not v['tone']:
                failures.append(f"variant without tone: {v['hexcode']} under {e['hexcode']}")

    # 3. Stats.
    n_with_variants = sum(1 for e in entries if e['variants'])
    n_variants = sum(len(e['variants']) for e in entries)
    print(f"  entries: {len(entries)}")
    print(f"  bases with variants: {n_with_variants}")
    print(f"  total variants: {n_variants}")

    # 4. Spot check: thumbs_up should have 5 tone variants.
    for e in entries:
        if e['shortcodes'] and e['shortcodes'][0] == 'thumbs_up':
            tones = sorted({v['tone'] for v in e['variants'] if v['tone']})
            if tones != [1, 2, 3, 4, 5]:
                failures.append(f":thumbs_up: tones = {tones}, expected [1..5]")
            break

    # 5. Hair-tagged shortcodes (man_white_haired, woman_red_haired, …)
    #    MUST appear as their own top-level entries — we no longer fold
    #    them under :man:/:woman:.
    top_shortcodes = {sc for e in entries for sc in (e['shortcodes'] or [])}
    for hair_sc in ('woman_white_haired', 'man_white_haired',
                    'woman_red_haired', 'man_curly_haired', 'woman_bald'):
        if hair_sc not in top_shortcodes:
            failures.append(f":{hair_sc}: should be a top-level entry, not folded as a variant")

    # 6. Same for gender siblings — woman_astronaut, man_astronaut, etc.
    #    must remain top-level distinct emoji.
    for gender_sc in ('woman_astronaut', 'man_astronaut'):
        if gender_sc not in top_shortcodes:
            failures.append(f":{gender_sc}: should be a top-level entry, not folded as a gender variant")

    if failures:
        print("FAIL:")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("OK")
    return 0


def main():
    parser = argparse.ArgumentParser(description='Generate per-language emoji character list')
    parser.add_argument('root_path', help='Path to the root directory containing subfolders with JSON files.')
    parser.add_argument('output_dir', nargs='?', default=None,
                        help='Path to the output directory where generated XML files will be stored.')
    parser.add_argument('--self-test', action='store_true',
                        help='Run sanity checks on the en folder; do not write output.')
    args = parser.parse_args()

    if args.self_test:
        en_dir = os.path.join(args.root_path, 'en')
        entries = process_folder(
            os.path.join(en_dir, 'data.raw.json'),
            os.path.join(en_dir, 'shortcodes', 'cldr.raw.json'),
            os.path.join(en_dir, 'shortcodes', 'emojibase.raw.json'),
            os.path.join(en_dir, 'messages.raw.json'),
        )
        sys.exit(run_self_test(entries))

    if not args.output_dir:
        parser.error('output_dir required (or pass --self-test)')

    print(args.root_path)
    print(args.output_dir)

    output_path = os.path.join(os.path.dirname(os.path.realpath(__file__)), args.output_dir)
    os.makedirs(output_path, exist_ok=True)

    for subfolder in glob.glob(os.path.join(args.root_path, '*')):
        if not os.path.isdir(subfolder):
            continue
        folder_name = os.path.basename(subfolder)
        if folder_name not in ALLOWED_FOLDERS:
            continue

        print(f"Processing folder: {folder_name}")

        data_file = os.path.join(subfolder, 'data.raw.json')
        cldr_file = os.path.join(subfolder, 'shortcodes', 'cldr.raw.json')
        emojibase_file = os.path.join(subfolder, 'shortcodes', 'emojibase.raw.json')
        messages_file = os.path.join(subfolder, 'messages.raw.json')

        if not (os.path.isfile(data_file) and os.path.isfile(cldr_file) and os.path.isfile(messages_file)):
            continue

        output_subfolder = os.path.join(output_path, folder_name)
        os.makedirs(output_subfolder, exist_ok=True)
        output_file = os.path.join(output_subfolder, 'emoji_characters.xml')

        entries = process_folder(data_file, cldr_file, emojibase_file, messages_file)
        with open(output_file, 'w', encoding='utf-8') as outfile:
            outfile.write(render_xml(entries))


if __name__ == '__main__':
    main()
