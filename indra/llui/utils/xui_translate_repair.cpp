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
//                        [--file <name>] [--quiet]

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
    };

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

    std::cout << (options.dry_run ? "would move " : "moved ") << moved
              << " values into place across " << files << " files in " << options.language
              << "; " << ambiguous << " left for a person, " << failed << " files with an error\n";
    return failed ? 1 : 0;
}
