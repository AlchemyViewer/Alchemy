/**
 * @file alvimmappings_test.cpp
 * @brief Vim's key notation, the :map family's table, and which mapping keys typed make.
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

#include "alvimmappings.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alvimmappings_data
    {
        ALVimMappings maps;
        std::string   listing;
        std::string   error;

        // A command of the family run, what it said kept; whether it was one.
        bool run(const std::string& name, const std::string& args, bool vimrc = false)
        {
            listing.clear();
            error.clear();
            return maps.command(name, args, vimrc, listing, error);
        }
        std::string spelt(const char* notation) const { return ALVimMappings::shown(maps.keysOf(notation)); }
        std::vector<ALVimInput> chars(const char* text) const
        {
            std::vector<ALVimInput> out;
            for (const char* c = text; *c; ++c)
            {
                out.push_back(ALVimInput::character(static_cast<llwchar>(*c)));
            }
            return out;
        }
    };

    // The notation: names in any case, modifiers, the characters it spells
    // by name, and a name it does not know as its characters.
    TEST_CASE_METHOD(alvimmappings_data, "alvimmappings: keysOf reads key names in any case and spells unknown ones out", "[alvimmappings]")
    {
        const std::vector<ALVimInput> keys = maps.keysOf("a<esc><CR><c-w><C-S-x><S-Tab><Space><lt><bar><Bslash><F5><nop><foo>");
        REQUIRE(keys.size() == size_t(16));
        CHECK(keys[0].isChar);
        CHECK(keys[0].ch == 'a');
        {
            INFO("<esc> in any case");
            CHECK_FALSE(keys[1].isChar);
            CHECK(keys[1].key == KEY_ESCAPE);
            CHECK(keys[1].mask == MASK_NONE);
        }
        UNSCOPED_INFO("<CR>");
        CHECK(keys[2].key == KEY_RETURN);
        {
            INFO("<c-w> is Control and W");
            CHECK(keys[3].key == 'W');
            CHECK(keys[3].mask == ALVimInput::CONTROL);
        }
        {
            INFO("<C-S-x>: two modifiers");
            CHECK(keys[4].key == 'X');
            CHECK(keys[4].mask == (ALVimInput::CONTROL | MASK_SHIFT));
        }
        {
            INFO("<S-Tab>");
            CHECK(keys[5].key == KEY_TAB);
            CHECK(keys[5].mask == MASK_SHIFT);
        }
        {
            INFO("<Space> <lt> <bar> <Bslash> are characters");
            CHECK(keys[6].ch == ' ');
            CHECK(keys[7].ch == '<');
            CHECK(keys[8].ch == '|');
            CHECK(keys[9].ch == '\\');
        }
        UNSCOPED_INFO("<F5>");
        CHECK(keys[10].key == KEY_F5);
        {
            INFO("<nop> is nothing, and <foo> is five characters");
            CHECK(keys[11].isChar);
            CHECK(keys[11].ch == '<');
            CHECK(keys[15].ch == '>');
        }
        UNSCOPED_INFO("shown back as :map lists them");
        CHECK(spelt("a<esc><c-w><Space><lt>") == std::string("a<Esc><C-W><Space><lt>"));
        const std::vector<ALVimInput> shifted = maps.keysOf("<S-a>");
        REQUIRE(shifted.size() == size_t(1));
        CHECK(shifted[0].isChar);
        CHECK(shifted[0].ch == 'A');
        const std::vector<ALVimInput> escape = maps.keysOf("<C-[>");
        REQUIRE(escape.size() == size_t(1));
        CHECK(escape[0].key == KEY_ESCAPE);
    }

    TEST_CASE_METHOD(alvimmappings_data, "alvimmappings: <Leader> is the leader :let set when the mapping was made", "[alvimmappings]")
    {
        UNSCOPED_INFO("vim's own leader is a backslash");
        CHECK(spelt("<Leader>w") == std::string("<Bslash>w"));
        CHECK(maps.let("mapleader = \",\"", error));
        CHECK(error.empty());
        CHECK(spelt("<leader>w") == std::string(",w"));
        {
            INFO("g: and a key by name");
            CHECK(maps.let("g:mapleader = \"\\<Space>\"", error));
            CHECK(error.empty());
        }
        CHECK(spelt("<Leader>w") == std::string("<Space>w"));
        {
            INFO("single quotes as they are");
            CHECK(maps.let("maplocalleader='<'", error));
            CHECK(error.empty());
        }
        UNSCOPED_INFO("a < in single quotes is itself");
        CHECK(spelt("<LocalLeader>x") == std::string("<lt>x"));
        {
            INFO("unclosed");
            CHECK(maps.let("mapleader = \",", error));
            CHECK_FALSE(error.empty());
        }
        error.clear();
        UNSCOPED_INFO("another variable is not the leaders'");
        CHECK_FALSE(maps.let("foo = 1", error));
        UNSCOPED_INFO("a mapping made keeps the leader of its time");
        CHECK(run("nnoremap", "<leader>w :w<CR>"));
        maps.let("mapleader = ','", error);
        const ALVimMappings::Match m = maps.match(ALVimMappings::NORMAL, maps.keysOf("<Space>w"), true);
        UNSCOPED_INFO("found by the leader it was made with");
        CHECK(m.full != nullptr);
    }

    TEST_CASE_METHOD(alvimmappings_data, "alvimmappings: mappings are made per mode and listed and removed by unmap and mapclear", "[alvimmappings]")
    {
        CHECK(run("nmap", "j gj"));
        UNSCOPED_INFO("ino is inoremap");
        CHECK(run("ino", "jk <Esc>"));
        UNSCOPED_INFO("map is three modes");
        CHECK(run("map", "Q gq"));
        UNSCOPED_INFO("map! is two");
        CHECK(run("map!", "<C-l> <Right>"));
        CHECK(maps.mappings().size() == size_t(4));
        CHECK(maps.match(ALVimMappings::INSERT, chars("jk"), true).full != nullptr);
        {
            INFO("in normal mode, j alone is one");
            REQUIRE(maps.match(ALVimMappings::NORMAL, chars("jk"), true).full);
            CHECK(maps.match(ALVimMappings::NORMAL, chars("jk"), true).full->from.size() == 1);
        }
        UNSCOPED_INFO("Q in operator-pending mode too");
        CHECK(maps.match(ALVimMappings::OPERATOR, chars("Q"), true).full != nullptr);
        const std::vector<ALVimInput> control_l = maps.keysOf("<C-l>");
        REQUIRE(control_l.size() == size_t(1));
        UNSCOPED_INFO("the : line has <C-l>");
        CHECK(maps.starts(ALVimMappings::COMMAND_LINE, control_l[0]));
        CHECK(run("map", ""));
        CHECK(listing.find("Q") != std::string::npos);
        CHECK(listing.find("gj") != std::string::npos);
        CHECK(listing.find("jk") == std::string::npos);
        {
            INFO("the noremap marked");
            CHECK(run("imap", ""));
            CHECK(listing.find("* <Esc>") != std::string::npos);
        }
        {
            INFO("keys alone list what starts with them");
            CHECK(run("nmap", "Q"));
            CHECK(listing.find("Q") != std::string::npos);
            CHECK(listing.find("gj") == std::string::npos);
        }
        {
            INFO("xunmap takes visual mode from Q");
            CHECK(run("xunmap", "Q"));
            CHECK(error.empty());
        }
        UNSCOPED_INFO("but not normal mode");
        CHECK(maps.match(ALVimMappings::NORMAL, chars("Q"), true).full != nullptr);
        UNSCOPED_INFO("nor is it in visual");
        CHECK(maps.match(ALVimMappings::VISUAL, chars("Q"), true).full == nullptr);
        {
            INFO("unmapping what is not there");
            CHECK(run("iunmap", "zz"));
            CHECK_FALSE(error.empty());
        }
        CHECK(run("imapclear", ""));
        CHECK(maps.match(ALVimMappings::INSERT, chars("jk"), true).full == nullptr);
        {
            INFO("mapclear! leaves normal mode's");
            CHECK(run("mapclear!", ""));
            CHECK(maps.match(ALVimMappings::NORMAL, chars("j"), true).full != nullptr);
            CHECK(maps.match(ALVimMappings::COMMAND_LINE, maps.keysOf("<C-l>"), true).full == nullptr);
        }
        {
            INFO("not one of the family");
            CHECK_FALSE(run("mapx", "a b"));
            CHECK_FALSE(run("nnoremap!", "a b"));
            CHECK_FALSE(run("marks", ""));
        }
        {
            INFO("an empty list says so");
            CHECK(run("cmap", ""));
            CHECK(listing == "No mapping found");
        }
    }

    TEST_CASE_METHOD(alvimmappings_data, "alvimmappings: match finds the longest whole mapping and whether more keys could extend it", "[alvimmappings]")
    {
        run("nmap", "g gg");
        run("nmap", "gx x");
        run("nmap", "<leader>ab A");
        {
            INFO("g alone is whole, and gx may follow");
            CHECK(maps.match(ALVimMappings::NORMAL, chars("g"), true).full != nullptr);
            CHECK(maps.match(ALVimMappings::NORMAL, chars("g"), true).longer);
        }
        UNSCOPED_INFO("nothing more is coming: not longer");
        CHECK_FALSE(maps.match(ALVimMappings::NORMAL, chars("g"), false).longer);
        const ALVimMappings::Match gx = maps.match(ALVimMappings::NORMAL, chars("gxq"), true);
        {
            INFO("gx the longest whole, the q after it none of it");
            REQUIRE(gx.full);
            CHECK(gx.full->from.size() == 2);
            CHECK_FALSE(gx.longer);
        }
        const ALVimMappings::Match leader_a = maps.match(ALVimMappings::NORMAL, chars("\\a"), true);
        {
            INFO("\\a waits for b");
            CHECK(leader_a.longer);
            CHECK_FALSE(leader_a.full);
        }
        UNSCOPED_INFO("\\c is nothing");
        CHECK_FALSE(maps.match(ALVimMappings::NORMAL, chars("\\c"), true).longer);
        {
            INFO("a mapping made again replaces the one there");
            CHECK(run("nmap", "gx y"));
            CHECK(maps.mappings().size() == 3);
        }
        {
            INFO("<nowait> kept");
            CHECK(run("nnoremap", "<nowait> g G"));
            const ALVimMappings::Match nowait = maps.match(ALVimMappings::NORMAL, chars("g"), true);
            REQUIRE(nowait.full);
            CHECK(nowait.full->nowait);
        }
        {
            INFO("<unique> refuses one there");
            CHECK(run("nmap", "<unique> gx z"));
            CHECK_FALSE(error.empty());
        }
        {
            INFO("<expr> is refused");
            CHECK(run("nmap", "<expr> q 1"));
            CHECK_FALSE(error.empty());
        }
        UNSCOPED_INFO("a bar ends it");
        CHECK(run("nmap", "zb a\\|b|c"));
        const ALVimMappings::Match bar = maps.match(ALVimMappings::NORMAL, chars("zb"), true);
        REQUIRE(bar.full);
        UNSCOPED_INFO("\\| is one");
        CHECK(ALVimMappings::shown(bar.full->to) == std::string("a<Bar>b"));
    }

    TEST_CASE_METHOD(alvimmappings_data, "alvimmappings: forgetVimrc drops the vimrc's mappings and leader but keeps typed ones", "[alvimmappings]")
    {
        maps.let("mapleader = ','", error);
        run("nnoremap", "<leader>a A", true);
        run("nnoremap", "Y y$");
        maps.forgetVimrc();
        CHECK(maps.mappings().size() == size_t(1));
        UNSCOPED_INFO("the typed one");
        CHECK(maps.match(ALVimMappings::NORMAL, chars("Y"), true).full != nullptr);
        UNSCOPED_INFO("the leader vim's again");
        CHECK(maps.leader() == std::string("\\"));
    }

    // As vim's does: its keys twelve columns wide by the columns they take,
    // a blank after them however long, the star of a noremap right after.
    TEST_CASE_METHOD(alvimmappings_data, "alvimmappings: :map lists mappings in vim's columns with a noremap's star", "[alvimmappings]")
    {
        run("map", "Q gj");
        run("noremap", "ab cd");
        run("map", "\xC3\xA9 x");
        run("nmap", "abcdefghijklmn z");
        CHECK(run("map", ""));
        const std::string expected = "   Q" + std::string(13, ' ') + "gj\n" +
                                     "   ab" + std::string(10, ' ') + "* cd\n" +
                                     "   \xC3\xA9" + std::string(13, ' ') + "x\n" +
                                     "n  abcdefghijklmn   z";
        CHECK(listing == expected);
    }
}
