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

# Gender codes used in the Variants LLSD: -1 unset, 0 man, 1 woman, 2 person.
GENDER_MAN = 0
GENDER_WOMAN = 1
GENDER_PERSON = 2

# Match a leading gender token in a primary shortcode such as
# "man_health_worker", "woman_astronaut", "person_climbing". Used to roll
# gender-ZWJ siblings up under a single base.
_GENDER_PREFIX_RE = re.compile(r'^(man|woman|person)_(.+)$')

# Recognised hair tags inside a shortcode suffix. Order matters for the
# `_red_hair` / `_curly_hair` / `_white_hair` / `_bald` regex below.
_HAIR_SUFFIXES = {
    '_red_hair':   'red',
    '_curly_hair': 'curly',
    '_white_hair': 'white',
    '_bald':       'bald',
}
_HAIR_SUFFIX_RE = re.compile(r'^(.*?)(_red_hair|_curly_hair|_white_hair|_bald)$')


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
            'gender': -1,
            'hair': '',
            'shortcodes': child_sc,
        })
    return variants


def group_gender_triples(top_level_entries):
    """Identify man/woman/person shortcode siblings.

    Returns (demoted_hexcodes, base_to_extra_variants) where
    base_to_extra_variants maps a "promoted" entry's hexcode to a list of
    additional variant dicts (the demoted gender siblings, plus their own
    skin children with Gender set).
    """
    by_suffix = {}  # suffix → {gender_token: entry}
    for entry in top_level_entries:
        sc = primary_shortcode(entry['shortcodes'])
        if not sc:
            continue
        m = _GENDER_PREFIX_RE.match(sc)
        if not m:
            continue
        token, suffix = m.group(1), m.group(2)
        by_suffix.setdefault(suffix, {})[token] = entry

    demoted = set()
    base_to_extras = {}

    for suffix, group in by_suffix.items():
        # Need at least 2 of the 3 to consider this a real gender axis.
        if len(group) < 2:
            continue

        # Promote person > woman > man.
        if 'person' in group:
            base_token = 'person'
        elif 'woman' in group:
            base_token = 'woman'
        else:
            base_token = 'man'
        base_entry = group[base_token]

        gender_for = {'man': GENDER_MAN, 'woman': GENDER_WOMAN, 'person': GENDER_PERSON}

        # Annotate the base's pre-existing skin variants with the base's
        # gender so findVariant on the C++ side can match by gender even
        # when the user has chosen a tone for the base form.
        for v in base_entry['variants']:
            if v['gender'] == -1:
                v['gender'] = gender_for[base_token]

        extras = []
        for token, entry in group.items():
            if token == base_token:
                continue
            g = gender_for[token]
            # The demoted entry itself becomes a (no-tone, gender) variant.
            extras.append({
                'character': entry['character'],
                'hexcode': entry['hexcode'],
                'tone': 0,
                'tone2': 0,
                'gender': g,
                'hair': '',
                'shortcodes': entry['shortcodes'],
            })
            # Its existing skin variants get re-tagged with this gender.
            for v in entry['variants']:
                v['gender'] = g
                extras.append(v)
            demoted.add(entry['hexcode'])

        base_to_extras[base_entry['hexcode']] = extras

    return demoted, base_to_extras


def group_hair_variants(top_level_entries, already_demoted):
    """Identify *_red_hair / *_curly_hair / *_white_hair / *_bald siblings.

    Each hair-tagged shortcode (e.g. ":man_red_hair:") is folded under the
    matching un-tagged entry (e.g. ":man:") as a Hair variant. The
    un-tagged entry stays at top level; the hair entry is demoted.

    Returns (demoted_hexcodes, base_to_extra_variants) — same shape as
    group_gender_triples so callers can merge results.
    """
    by_shortcode = {}
    for entry in top_level_entries:
        if entry['hexcode'] in already_demoted:
            continue
        sc = primary_shortcode(entry['shortcodes'])
        if sc:
            by_shortcode[sc] = entry

    demoted = set()
    base_to_extras = {}

    for entry in top_level_entries:
        if entry['hexcode'] in already_demoted:
            continue
        sc = primary_shortcode(entry['shortcodes'])
        if not sc:
            continue
        m = _HAIR_SUFFIX_RE.match(sc)
        if not m:
            continue
        bare_sc, hair_suffix = m.group(1), m.group(2)
        hair_label = _HAIR_SUFFIXES[hair_suffix]
        base_entry = by_shortcode.get(bare_sc)
        if not base_entry or base_entry is entry:
            continue

        # The hair entry itself becomes a Hair variant.
        extras = base_to_extras.setdefault(base_entry['hexcode'], [])
        extras.append({
            'character': entry['character'],
            'hexcode': entry['hexcode'],
            'tone': 0,
            'tone2': 0,
            'gender': entry['variants'][0]['gender'] if entry['variants'] else -1,
            'hair': hair_label,
            'shortcodes': entry['shortcodes'],
        })
        # Its skin children become tone+hair variants.
        for v in entry['variants']:
            v['hair'] = hair_label
            extras.append(v)
        demoted.add(entry['hexcode'])

    return demoted, base_to_extras


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
    if variant['gender'] != -1:
        parts.append('\t\t\t\t\t<key>Gender</key>\n')
        parts.append(f'\t\t\t\t\t<integer>{variant["gender"]}</integer>\n')
    if variant.get('hair'):
        parts.append('\t\t\t\t\t<key>Hair</key>\n')
        parts.append(f'\t\t\t\t\t<string>{variant["hair"]}</string>\n')
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

    # Pass 2 — group man/woman/person triples and demote the non-promoted
    # ones into Variants of the promoted entry.
    demoted, base_to_extras = group_gender_triples(entries)

    # Pass 3 — fold *_red_hair / *_curly_hair / etc. siblings under their
    # un-tagged base. Operates after gender grouping so the demote sets
    # don't overlap.
    hair_demoted, hair_extras = group_hair_variants(entries, demoted)
    demoted = demoted | hair_demoted
    for hex_, extras in hair_extras.items():
        base_to_extras.setdefault(hex_, []).extend(extras)

    final_entries = []
    for entry in entries:
        if entry['hexcode'] in demoted:
            continue
        if entry['hexcode'] in base_to_extras:
            entry['variants'] = entry['variants'] + base_to_extras[entry['hexcode']]
        final_entries.append(entry)

    return final_entries


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

    # 2. Each variant must have at least one identifying axis (tone, gender, or hair).
    for e in entries:
        for v in e['variants']:
            if not v['tone'] and v['gender'] == -1 and not v.get('hair'):
                failures.append(f"variant without tone, gender, or hair: {v['hexcode']} under {e['hexcode']}")

    # 3. Stats.
    n_with_variants = sum(1 for e in entries if e['variants'])
    n_variants = sum(len(e['variants']) for e in entries)
    print(f"  entries: {len(entries)}")
    print(f"  bases with variants: {n_with_variants}")
    print(f"  total variants: {n_variants}")

    # 4. Spot checks: thumbs_up should have 5 tone variants.
    for e in entries:
        if e['shortcodes'] and e['shortcodes'][0] == 'thumbs_up':
            tones = sorted({v['tone'] for v in e['variants'] if v['tone']})
            if tones != [1, 2, 3, 4, 5]:
                failures.append(f":thumbs_up: tones = {tones}, expected [1..5]")
            break

    # 5. Spot check: astronaut family should have woman+man variants.
    for e in entries:
        if e['shortcodes'] and 'astronaut' in e['shortcodes'][0]:
            genders = sorted({v['gender'] for v in e['variants'] if v['gender'] != -1})
            if not genders:
                failures.append(f"astronaut entry {e['shortcodes'][0]} has no gender variants")
            break

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
