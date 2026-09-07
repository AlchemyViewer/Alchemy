/**
 * @file xui_translate_repair.cpp
 * @brief Move a language's values to the paths the base has them at, one file at a time.
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

// The repair the tool's Translation panel runs, over every file of one
// language, from a console: the values written at a path the base has
// moved on from are moved to where the base has them, and nothing else
// is touched. What the base names at more than one path is left alone
// and listed, because a machine cannot know which one was meant.
//
//   xui_translate_repair <skins_dir> <language> [--skin default] [--dry-run]
//                        [--file <name>] [--quiet] [--report] [--roots]
//
// --roots first gives back the files the merge ignores whole: a root
// whose name differs from the base's makes the merge refuse the file, and
// a name it never had makes that certain.
//
// --report lists what it leaves behind and why, which is the list of
// things the base has to be fixed for.

#include "linden_common.h"

#include "alxuicatalog.h"
#include "alxuiedit.h"
#include "alxuitranslate.h"

#include "llerrorcontrol.h"

#include <iostream>
#include <string>
#include <vector>

// llui's URL entries call this, and the viewer defines it.
class LLAvatarName;
const std::string gRepairAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gRepairAnonName;
}

namespace
{
    struct Options
    {
        std::string skins;
        std::string language;
        std::string skin = "default";
        std::string base = "en";
        std::string file;
        bool dry_run = false;
        bool quiet = false;
        bool report = false;
        bool roots = false;
    };

    std::string joined(const std::vector<std::string>& path)
    {
        std::string text;
        for (const std::string& step : path)
        {
            text += text.empty() ? step : "/" + step;
        }
        return text;
    }

    // What a file would say if the merge read it. A value the base has a
    // place for arrives -- at its path already, or after the move that
    // follows -- and a value naming something the base has nowhere is
    // what the file has outlived.
    void weigh(const ALXUITranslate& units, S32& arrives, S32& absent)
    {
        arrives = absent = 0;
        for (const ALXUITranslate::Unit& unit : units.units())
        {
            switch (unit.state)
            {
            case ALXUITranslate::State::Translated:
            case ALXUITranslate::State::Placeholders:
                ++arrives;
                break;
            case ALXUITranslate::State::NotApplied:
                if (unit.miss == ALXUITranslate::Miss::Absent
                    || unit.miss == ALXUITranslate::Miss::Unnamed)
                {
                    ++absent;
                }
                else
                {
                    ++arrives;
                }
                break;
            default:
                break;
            }
        }
    }

    // What the repair leaves behind for a person: a name the base has at
    // more than one path, and a name it repeats among siblings, which an
    // ancestor chain of names cannot address.
    S32 countByHand(const ALXUITranslate& units)
    {
        S32 n = 0;
        for (const ALXUITranslate::Unit& unit : units.units())
        {
            if (unit.state != ALXUITranslate::State::NotApplied)
            {
                continue;
            }
            if (unit.miss == ALXUITranslate::Miss::Ambiguous)
            {
                ++n;
                continue;
            }
            if (unit.miss == ALXUITranslate::Miss::Moved)
            {
                for (const std::string& step : unit.path)
                {
                    if (step.find('#') != std::string::npos)
                    {
                        ++n;
                        break;
                    }
                }
            }
        }
        return n;
    }
}

int main(int argc, char** argv)
{
    LLError::initForApplication(".", ".", false);
    LLError::setDefaultLevel(LLError::LEVEL_WARN);

    Options options;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--dry-run")         { options.dry_run = true; }
        else if (arg == "--quiet")      { options.quiet = true; }
        else if (arg == "--report")     { options.report = true; }
        else if (arg == "--roots")      { options.roots = true; }
        else if (arg == "--skin" && i + 1 < argc)   { options.skin = argv[++i]; }
        else if (arg == "--base" && i + 1 < argc)   { options.base = argv[++i]; }
        else if (arg == "--file" && i + 1 < argc)   { options.file = argv[++i]; }
        else                            { positional.push_back(arg); }
    }
    if (positional.size() < 2)
    {
        std::cerr << "usage: xui_translate_repair <skins_dir> <language>"
                     " [--skin default] [--base en] [--file name] [--dry-run] [--quiet]\n";
        return 2;
    }
    options.skins = positional[0];
    options.language = positional[1];

    ALXUICatalog catalog;
    catalog.scan(options.skins);
    if (catalog.entries().empty())
    {
        std::cerr << "no XUI files under " << options.skins << "\n";
        return 2;
    }

    S32 files = 0;
    S32 moved = 0;
    S32 roots = 0;
    S32 roots_done = 0;
    S32 ambiguous = 0;
    S32 failed = 0;
    for (const ALXUICatalog::Entry& entry : catalog.entries())
    {
        if (!options.file.empty() && entry.name != options.file)
        {
            continue;
        }
        const ALXUICatalog::Layer* overlay_layer = entry.layer(options.skin, options.language);
        if (!overlay_layer || !overlay_layer->root())
        {
            continue;
        }
        std::vector<const ALXUICatalog::Layer*> base_layers =
            catalog.layersFor(entry, options.skin, options.base);
        if (base_layers.empty() || !base_layers.front()->root())
        {
            continue;
        }
        const pugi::xml_node base = base_layers.front()->root();

        ALXUITranslate units;
        units.scan(base, overlay_layer->root());
        ambiguous += countByHand(units);

        // A root the merge refuses is a file it never opens. Giving it
        // the base's name is right when the file is the same file under
        // another name -- which is certain when it was given no name at
        // all, and is what the counts say otherwise: a file with more to
        // say than to lose is the same file, and one with almost nothing
        // in common is a translation of something else and is left as it
        // is for a person to look at.
        const std::string over_root = overlay_layer->root().attribute("name").as_string();
        const std::string base_root = base.attribute("name").as_string();
        if (options.roots && over_root != base_root)
        {
            S32 arrives = 0;
            S32 absent = 0;
            weigh(units, arrives, absent);
            const bool nameless = over_root.empty();
            if (nameless || arrives > absent)
            {
                std::cout << (options.dry_run ? "  would name " : "  named ")
                          << options.language << "/" << entry.name << " root \"" << base_root
                          << "\" (was \"" << over_root << "\"): " << arrives << " values arrive, "
                          << absent << " name nothing the base has\n";
                if (!options.dry_run)
                {
                    ALXUIEdit root_edit;
                    std::string why;
                    if (!root_edit.loadFile(overlay_layer->path)
                        || !root_edit.setAttribute({}, "name", base_root)
                        || !root_edit.save())
                    {
                        std::cerr << entry.name << ": " << root_edit.error() << "\n";
                        ++failed;
                    }
                    else
                    {
                        ++roots;
                        // The catalog still holds the file as it was
                        // read; the moves come on the next run, when it
                        // is read again with the name it now has.
                        ++roots_done;
                        continue;
                    }
                }
            }
            else
            {
                std::cout << "  left " << options.language << "/" << entry.name
                          << " root \"" << over_root << "\" alone: only " << arrives
                          << " of its values are the base's, " << absent << " are not\n";
            }
        }

        if (options.report)
        {
            for (const ALXUITranslate::Unit& unit : units.units())
            {
                if (unit.state != ALXUITranslate::State::NotApplied)
                {
                    continue;
                }
                const bool repeats = joined(unit.path).find('#') != std::string::npos;
                if (unit.miss == ALXUITranslate::Miss::Ambiguous)
                {
                    std::cout << "  ambiguous  " << entry.name << ": " << joined(unit.path)
                              << " @" << (unit.field.empty() ? "text" : unit.field)
                              << " (the base has that name at more than one path)\n";
                }
                else if (unit.miss == ALXUITranslate::Miss::Moved && repeats)
                {
                    std::cout << "  repeated   " << entry.name << ": " << joined(unit.path)
                              << " @" << (unit.field.empty() ? "text" : unit.field)
                              << " (the base repeats that name among siblings)\n";
                }
                else if (unit.miss == ALXUITranslate::Miss::Unnamed)
                {
                    std::cout << "  unnamed    " << entry.name << ": " << unit.where
                              << " @" << (unit.field.empty() ? "text" : unit.field) << "\n";
                }
            }
        }

        ALXUIEdit overlay;
        if (!overlay.loadFile(overlay_layer->path))
        {
            std::cerr << entry.name << ": " << overlay.error() << "\n";
            ++failed;
            continue;
        }

        std::string error;
        const S32 done = ALXUITranslate::repair(overlay, base, error);
        if (!error.empty())
        {
            std::cerr << entry.name << ": " << error << "\n";
            ++failed;
        }
        if (!done)
        {
            continue;
        }
        if (!options.dry_run && !overlay.save())
        {
            std::cerr << entry.name << ": " << overlay.error() << "\n";
            ++failed;
            continue;
        }
        ++files;
        moved += done;
        if (!options.quiet)
        {
            std::cout << (options.dry_run ? "would move " : "moved ") << done
                      << " in " << options.language << "/" << entry.name << "\n";
        }
    }

    if (options.roots)
    {
        std::cout << (options.dry_run ? "would give " : "gave ") << roots
                  << " files back the base's root name in " << options.language << "\n";
    }
    std::cout << (options.dry_run ? "would move " : "moved ") << moved
              << " values into place across " << files << " files in " << options.language
              << "; " << ambiguous << " left for a person, " << failed << " files with an error\n";
    return failed ? 1 : 0;
}
