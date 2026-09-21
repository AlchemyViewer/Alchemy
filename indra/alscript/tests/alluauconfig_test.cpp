/**
 * @file alluauconfig_test.cpp
 * @brief A .luaurc read for its aliases and its mode.
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

#include "linden_common.h"

#include "../alluauconfig.h"

#include "../test/lltut.h"

namespace tut
{
    struct alluauconfig_data
    {
    };

    typedef test_group<alluauconfig_data> alluauconfig_group;
    typedef alluauconfig_group::object    alluauconfig_object;
    alluauconfig_group                    alluauconfig_instance("alluauconfig");

    template<> template<>
    void alluauconfig_object::test<1>()
    {
        set_test_name("aliases come out by lower-case name with their paths, and the mode where it is said");
        ALLuauConfig config;
        std::string  error;
        ensure("parses: " + error, ALLuauConfig::parse("{\n  \"languageMode\": \"strict\",\n  \"aliases\": { \"Lib\": \"./lib\", \"shared\": \"../shared\", \"abs\": \"/opt/lua\" }\n}\n", config, error));
        ensure_equals("three aliases", config.aliases.size(), size_t(3));
        ensure_equals("by lower-case name", config.aliases["lib"], std::string("./lib"));
        ensure_equals("a path up", config.aliases["shared"], std::string("../shared"));
        ensure_equals("an absolute one", config.aliases["abs"], std::string("/opt/lua"));
        ensure_equals("the mode", config.mode, std::string("strict"));
        ensure("no mode where none is said", ALLuauConfig::parse("{ \"aliases\": {} }", config, error) && config.mode.empty());
        ensure("not a configuration", !ALLuauConfig::parse("{ \"aliases\": 3 }", config, error) && !error.empty());
        ensure("nor an alias Luau refuses", !ALLuauConfig::parse("{ \"aliases\": { \"bad name\": \"x\" } }", config, error));
    }

    template<> template<>
    void alluauconfig_object::test<2>()
    {
        set_test_name("a require name's alias is what follows the @ up to the slash, and absolute paths are known on both platforms");
        std::string alias, rest;
        ensure("found", ALLuauConfig::aliasOf("@Lib/util/strings", alias, rest));
        ensure_equals("lower case", alias, std::string("lib"));
        ensure_equals("the rest", rest, std::string("util/strings"));
        ensure("the alias alone", ALLuauConfig::aliasOf("@lib", alias, rest) && alias == "lib" && rest.empty());
        ensure("not without the @", !ALLuauConfig::aliasOf("lib/util", alias, rest));
        ensure("not an empty one", !ALLuauConfig::aliasOf("@/util", alias, rest));
        ensure("absolute on posix", ALLuauConfig::absolute("/opt/lua"));
        ensure("absolute on windows", ALLuauConfig::absolute("C:\\lua") && ALLuauConfig::absolute("c:/lua"));
        ensure("relative", !ALLuauConfig::absolute("./lib") && !ALLuauConfig::absolute("lib"));
    }
}
