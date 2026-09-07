/**
 * @file xui_overlay_check.cpp
 * @brief Runs the viewer's layer merge over every skin and language and counts what it drops.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

// xui_overlay_check <skins directory> [baseline file] [--write-baseline] [--examples N]
//
// For every skin under the directory and every language under its xui
// directory, every file is merged over its base by the same call the
// viewer makes, with an observer that counts each decision: layers that
// did not parse, roots whose name or tag differs, children that applied
// to nothing and why, attributes dropped, text blanked, and the
// translation checks a merged pair allows, placeholders that differ and
// text under translate="false".
// The census is printed by language. With a baseline file, the gated
// counts are compared with it and the exit code says whether any grew;
// --write-baseline writes the counts as the new baseline.

#include "linden_common.h"

#include "alxmllayermerge.h"
#include "fsyspath.h"
#include "llerrorcontrol.h"
#include "llfile.h"
#include "llxmlnode.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    const std::set<std::string> TEXT_ATTRS = {
        "label", "label_selected", "tool_tip", "title", "short_title", "value", "yestext", "notext",
        "canceltext", "ignoretext", "text", "initial_value", "description", "longdescription"
    };
    const std::set<std::string> LAYOUT_ATTRS = {
        "width", "height", "left", "right", "top", "bottom", "left_delta", "left_pad", "top_delta",
        "top_pad", "bottom_delta", "label_width"
    };

    // The counts, in the order they print. The first four are the gate.
    const char* const KEYS[] = {
        "unmatched", "value_dropped", "text_blanked", "parse_error",
        "files", "orphan_file", "root_name_differs", "root_tag_differs", "duplicate_name", "misnested",
        "misnested_unique", "misnested_outside", "misnested_ambiguous", "unmatched_absent", "unmatched_no_name", "tag_mismatch",
        "attr_not_in_base", "layout_attr_overridden", "layout_attr_dropped", "placeholder_mismatch",
        "translated_despite_translate_false", "covered", "text_kept", "value_as_text"
    };
    constexpr size_t GATED = 4;

    typedef std::map<std::string, S64> counts_t;

    std::vector<std::string> listDir(const std::string& dir, bool directories)
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(fsyspath(dir), ec))
        {
            std::error_code ec2;
            if (entry.is_directory(ec2) != directories)
            {
                continue;
            }
            std::string name = fsyspath(entry.path().filename()).string();
            if (!directories && !name.ends_with(".xml"))
            {
                continue;
            }
            names.push_back(std::move(name));
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    std::string join(const std::string& a, const std::string& b)
    {
        return a + "/" + b;
    }

    bool exists(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::exists(fsyspath(path), ec);
    }

    std::string matchKey(const LLXMLNode* node)
    {
        std::string key;
        if (!node->getAttributeString("name", key) || key.empty())
        {
            node->getAttributeString("value", key);
        }
        return key;
    }

    bool hasText(const LLXMLNode* node)
    {
        return node->getValue().find_first_not_of(" \t\r\n") != std::string::npos;
    }

    std::string trimmed(const std::string& text)
    {
        const size_t begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
        {
            return std::string();
        }
        const size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(begin, end - begin + 1);
    }

    // The [PLACEHOLDER] tokens of a string, as a set.
    std::set<std::string> placeholders(const std::string& text)
    {
        std::set<std::string> found;
        size_t open = text.find('[');
        while (open != std::string::npos)
        {
            size_t close = open + 1;
            while (close < text.size() && (std::isalnum((unsigned char)text[close]) || text[close] == '_'))
            {
                ++close;
            }
            if (close < text.size() && text[close] == ']' && close > open + 1)
            {
                found.insert(text.substr(open + 1, close - open - 1));
                open = text.find('[', close);
            }
            else
            {
                open = text.find('[', open + 1);
            }
        }
        return found;
    }

    // Whether the element or an ancestor says translate="false".
    bool translateOff(const LLXMLNode* node)
    {
        for (const LLXMLNode* cur = node; cur; cur = cur->mParent)
        {
            std::string value;
            if (cur->getAttributeString("translate", value) && value == "false")
            {
                return true;
            }
        }
        return false;
    }

    bool overlayHasTranslation(const LLXMLNode* overlay)
    {
        for (const auto& [name, attribute] : overlay->mAttributes)
        {
            if (TEXT_ATTRS.count(name->mString))
            {
                return true;
            }
        }
        return hasText(overlay);
    }

    S32 countDescendantsNamed(const LLXMLNode* node, const std::string& key)
    {
        S32 n = 0;
        for (LLXMLNodePtr child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
        {
            n += matchKey(child) == key;
            n += countDescendantsNamed(child, key);
        }
        return n;
    }

    void indexNames(const LLXMLNode* node, std::set<std::string>& names)
    {
        for (LLXMLNodePtr child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
        {
            const std::string key = matchKey(child);
            if (!key.empty())
            {
                names.insert(key);
            }
            indexNames(child, names);
        }
    }

    std::string pathOf(const LLXMLNode* node)
    {
        std::vector<std::string> steps;
        for (const LLXMLNode* cur = node; cur; cur = cur->mParent)
        {
            const std::string key = matchKey(cur);
            steps.push_back(std::string(cur->getName()->mString) + (key.empty() ? "" : "[" + key + "]"));
        }
        std::reverse(steps.begin(), steps.end());
        std::string path;
        for (const std::string& step : steps)
        {
            path += (path.empty() ? "" : "/") + step;
        }
        return path;
    }

    // Counts one file's merge. The base tree's names are indexed once so
    // an unmatched child can be told apart: moved elsewhere in the base,
    // or absent from it.
    class Counter final : public ALXmlMergeObserver
    {
    public:
        Counter(counts_t& counts, std::map<std::string, std::vector<std::string>>& examples,
                const std::string& where)
        :   mCounts(counts),
            mExamples(examples),
            mWhere(where)
        {
        }

        void example(const char* key, const std::string& text)
        {
            mExamples[key].push_back(mWhere + ": " + text);
        }

        void layerParsed(S32 layer, const std::string& path) override {}
        void layerSkipped(S32 layer, const std::string& path, const std::string& reason, S32 line) override
        {
            ++mCounts["parse_error"];
            example("parse_error", reason + " at line " + std::to_string(line));
        }

        void rootMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override
        {
            mNames.clear();
            indexNames(base, mNames);
            element(base, overlay);
        }

        void rootNameDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override
        {
            ++mCounts["root_name_differs"];
            example("root_name_differs", "overlay root " + pathOf(overlay) + " vs base " + pathOf(base));
        }

        void rootTagDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override
        {
            ++mCounts["root_tag_differs"];
            example("root_tag_differs", "overlay root " + pathOf(overlay) + " vs base " + pathOf(base));
        }

        void childMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override
        {
            if (base->getName() != overlay->getName())
            {
                ++mCounts["tag_mismatch"];
                example("tag_mismatch", pathOf(overlay) + " vs base <" + base->getName()->mString + ">");
            }
            element(base, overlay);
        }

        void childUnmatched(S32 layer, LLXMLNode* parent, LLXMLNode* overlay, Miss why) override
        {
            ++mCounts["unmatched"];
            const std::string key = matchKey(overlay);
            if (why == Miss::Unnamed)
            {
                ++mCounts["unmatched_no_name"];
                example("unmatched_no_name", pathOf(overlay));
            }
            else if (isDuplicate(parent, overlay, key))
            {
                // More children of this name here than the base has: the
                // ones past the base's count applied to nothing.
                ++mCounts["duplicate_name"];
                example("duplicate_name", pathOf(overlay));
            }
            else if (mNames.count(key))
            {
                ++mCounts["misnested"];
                const S32 hits = countDescendantsNamed(parent, key);
                const char* kind = hits == 1 ? "misnested_unique" : hits == 0 ? "misnested_outside" : "misnested_ambiguous";
                ++mCounts[kind];
                example("misnested", pathOf(overlay) + " (the base has the name elsewhere; "
                        + std::to_string(hits) + " below " + pathOf(parent) + ")");
            }
            else
            {
                ++mCounts["unmatched_absent"];
                example("unmatched_absent", pathOf(overlay));
            }
        }

        void textApplied(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override
        {
            // The base's text is already overwritten when this arrives;
            // the element's own record of it was taken at the match.
            if (!mBaseText.empty() && !translateOff(base))
            {
                ++mCounts["covered"];
                if (placeholders(mBaseText) != placeholders(overlay->getValue()))
                {
                    ++mCounts["placeholder_mismatch"];
                    example("placeholder_mismatch", pathOf(base) + " text: " + trimmed(mBaseText).substr(0, 40)
                            + " -> " + trimmed(overlay->getValue()).substr(0, 40));
                }
            }
        }

        void textKept(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override
        {
            if (overlay->getFirstChild().isNull())
            {
                ++mCounts["text_kept"];
            }
        }

        void valueAppliedAsText(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override
        {
            ++mCounts["value_as_text"];
            if (!mBaseText.empty() && !translateOff(base))
            {
                ++mCounts["covered"];
                if (placeholders(mBaseText) != placeholders(overlay_attribute->getValue()))
                {
                    ++mCounts["placeholder_mismatch"];
                    example("placeholder_mismatch", pathOf(base) + " value: " + trimmed(mBaseText).substr(0, 40)
                            + " -> " + overlay_attribute->getValue().substr(0, 40));
                }
            }
        }

        void attributeApplied(S32 layer, LLXMLNode* base_attribute, LLXMLNode* overlay_attribute) override
        {
            const std::string name = base_attribute->getName()->mString;
            const LLXMLNode* element = base_attribute->mParent;
            if (TEXT_ATTRS.count(name) && element && !translateOff(element))
            {
                ++mCounts["covered"];
                // The base's value is overwritten by now; it was recorded
                // at the match.
                auto it = mBaseAttributes.find(name);
                const std::string& before = it == mBaseAttributes.end() ? LLStringUtil::null : it->second;
                if (placeholders(before) != placeholders(overlay_attribute->getValue()))
                {
                    ++mCounts["placeholder_mismatch"];
                    example("placeholder_mismatch", pathOf(element) + " " + name + ": " + before.substr(0, 40)
                            + " -> " + overlay_attribute->getValue().substr(0, 40));
                }
            }
            if (LAYOUT_ATTRS.count(name))
            {
                ++mCounts["layout_attr_overridden"];
            }
        }

        void attributeDropped(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override
        {
            const std::string name = overlay_attribute->getName()->mString;
            if (LAYOUT_ATTRS.count(name))
            {
                ++mCounts["layout_attr_dropped"];
            }
            else
            {
                ++mCounts["attr_not_in_base"];
                example("attr_not_in_base", pathOf(base) + " " + name + "=");
            }
            if (name == "value")
            {
                ++mCounts["value_dropped"];
            }
        }

    private:
        // Whether this overlay child is one of more children of its name
        // than the base element has: those past the base's count match
        // nothing, since a base child is matched at most once.
        static bool isDuplicate(const LLXMLNode* parent, const LLXMLNode* overlay, const std::string& key)
        {
            S32 in_base = 0;
            for (LLXMLNodePtr child = parent->getFirstChild(); child.notNull(); child = child->getNextSibling())
            {
                in_base += matchKey(child) == key;
            }
            if (in_base == 0)
            {
                return false;
            }
            S32 before = 0;
            for (const LLXMLNode* sibling = overlay->mPrev.get(); sibling; sibling = sibling->mPrev.get())
            {
                before += !sibling->mIsAttribute && matchKey(sibling) == key;
            }
            return before >= in_base;
        }

        // What the base element held before the merge overwrites it: the
        // text and the translatable attributes, for the checks that compare.
        void element(LLXMLNode* base, LLXMLNode* overlay)
        {
            mBaseText = hasText(base) ? base->getValue() : std::string();
            mBaseAttributes.clear();
            for (const auto& [name, attribute] : base->mAttributes)
            {
                if (TEXT_ATTRS.count(name->mString))
                {
                    mBaseAttributes[name->mString] = attribute->getValue();
                }
            }
            if (translateOff(base) && overlayHasTranslation(overlay))
            {
                ++mCounts["translated_despite_translate_false"];
                example("translated_despite_translate_false", pathOf(base));
            }
        }

        counts_t&                                           mCounts;
        std::map<std::string, std::vector<std::string>>&    mExamples;
        std::string                                         mWhere;
        std::set<std::string>                               mNames;
        std::string                                         mBaseText;
        std::map<std::string, std::string>                  mBaseAttributes;
    };

    void countUnits(const LLXMLNode* node, S64& units, bool translate_off)
    {
        std::string value;
        if (node->getAttributeString("translate", value) && value == "false")
        {
            translate_off = true;
        }
        if (!translate_off)
        {
            for (const auto& [name, attribute] : node->mAttributes)
            {
                if (TEXT_ATTRS.count(name->mString) && !trimmed(attribute->getValue()).empty())
                {
                    ++units;
                }
            }
            if (hasText(node))
            {
                ++units;
            }
        }
        for (LLXMLNodePtr child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
        {
            countUnits(child, units, translate_off);
        }
    }

    void countCollisions(const LLXMLNode* node, S64& collisions)
    {
        std::map<std::string, S32> seen;
        for (LLXMLNodePtr child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
        {
            const std::string key = matchKey(child);
            if (!key.empty())
            {
                ++seen[key];
            }
            countCollisions(child, collisions);
        }
        for (const auto& [key, n] : seen)
        {
            collisions += n > 1;
        }
    }

    void printRow(const std::string& label, const counts_t& counts)
    {
        std::cout << std::left << std::setw(12) << label;
        for (const char* key : KEYS)
        {
            auto it = counts.find(key);
            std::cout << std::right << std::setw(9) << (it == counts.end() ? 0 : it->second);
        }
        std::cout << "\n";
    }

    void printHeader()
    {
        std::cout << std::left << std::setw(12) << "language";
        for (const char* key : KEYS)
        {
            std::cout << std::right << std::setw(9) << std::string(key).substr(0, 8);
        }
        std::cout << "\n";
    }

    counts_t readBaseline(const std::string& path)
    {
        counts_t baseline;
        llifstream in(path);
        std::string key;
        S64 value;
        while (in >> key >> value)
        {
            baseline[key] = value;
        }
        return baseline;
    }

    bool writeBaseline(const std::string& path, const counts_t& totals)
    {
        llofstream out(path, std::ios::binary);
        if (!out)
        {
            return false;
        }
        for (size_t i = 0; i < GATED; ++i)
        {
            auto it = totals.find(KEYS[i]);
            out << KEYS[i] << " " << (it == totals.end() ? 0 : it->second) << "\n";
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    LLError::initForApplication(".", ".");
    LLError::setDefaultLevel(LLError::LEVEL_ERROR);

    std::string skins_dir;
    std::string baseline_path;
    bool write_baseline = false;
    S32 show_examples = 5;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--write-baseline")
        {
            write_baseline = true;
        }
        else if (arg == "--examples" && i + 1 < argc)
        {
            show_examples = std::atoi(argv[++i]);
        }
        else if (skins_dir.empty())
        {
            skins_dir = arg;
        }
        else if (baseline_path.empty())
        {
            baseline_path = arg;
        }
    }
    if (skins_dir.empty())
    {
        std::cerr << "usage: xui_overlay_check <skins directory> [baseline file] [--write-baseline] [--examples N]\n";
        return 2;
    }

    counts_t totals;
    std::map<std::string, counts_t> by_language;
    std::map<std::string, std::vector<std::string>> examples;

    for (const std::string& skin : listDir(skins_dir, true))
    {
        const std::string xui = join(join(skins_dir, skin), "xui");
        const std::vector<std::string> languages = listDir(xui, true);
        if (languages.empty())
        {
            continue;
        }

        // The base of every merge in this skin: its own en file when it
        // has one, else default's, which is what the directory object
        // answers for the viewer.
        const std::string own_en = join(xui, "en");
        const std::string default_en = join(join(join(skins_dir, "default"), "xui"), "en");

        if (skin == "default")
        {
            S64 units = 0;
            S64 collisions = 0;
            S64 files = 0;
            for (const std::string& file : listDir(own_en, false))
            {
                LLXMLNodePtr root;
                if (LLXMLNode::parseFile(join(own_en, file), root, nullptr))
                {
                    ++files;
                    countUnits(root, units, false);
                    countCollisions(root, collisions);
                }
            }
            std::cout << "en: " << files << " files, " << units << " translatable units, "
                      << collisions << " sibling-name collisions\n";
        }

        for (const std::string& language : languages)
        {
            if (language == "en")
            {
                continue;
            }
            const std::string dir = join(xui, language);
            counts_t& counts = by_language[skin == "default" ? language : skin + "/" + language];
            for (const std::string& file : listDir(dir, false))
            {
                ++counts["files"];
                std::string base = join(own_en, file);
                if (!exists(base))
                {
                    base = join(default_en, file);
                }
                if (!exists(base))
                {
                    ++counts["orphan_file"];
                    examples["orphan_file"].push_back(skin + "/" + language + "/" + file);
                    continue;
                }
                Counter counter(counts, examples, skin + "/" + language + "/" + file);
                LLXMLNodePtr root;
                ALXmlLayerMerge::load({ base, join(dir, file) }, root, &counter);
            }
        }
    }

    printHeader();
    for (const auto& [language, counts] : by_language)
    {
        printRow(language, counts);
        for (const auto& [key, value] : counts)
        {
            totals[key] += value;
        }
    }
    printRow("all", totals);

    if (show_examples > 0)
    {
        for (const auto& [key, lines] : examples)
        {
            std::cout << "\n== " << key << " (" << lines.size() << ")\n";
            for (size_t i = 0; i < lines.size() && i < (size_t)show_examples; ++i)
            {
                std::cout << "   " << lines[i] << "\n";
            }
        }
    }

    if (baseline_path.empty())
    {
        return 0;
    }
    if (write_baseline)
    {
        if (!writeBaseline(baseline_path, totals))
        {
            std::cerr << "could not write " << baseline_path << "\n";
            return 2;
        }
        std::cout << "\nbaseline written to " << baseline_path << "\n";
        return 0;
    }

    const counts_t baseline = readBaseline(baseline_path);
    if (baseline.empty())
    {
        std::cout << "\nno baseline at " << baseline_path << "; nothing gated\n";
        return 0;
    }
    int failures = 0;
    std::cout << "\n";
    for (size_t i = 0; i < GATED; ++i)
    {
        const S64 now = totals[KEYS[i]];
        auto it = baseline.find(KEYS[i]);
        const S64 allowed = it == baseline.end() ? 0 : it->second;
        const bool over = now > allowed;
        failures += over;
        std::cout << (over ? "FAIL " : "ok   ") << std::left << std::setw(16) << KEYS[i]
                  << " " << now << " (baseline " << allowed << ")\n";
    }
    return failures ? 1 : 0;
}
