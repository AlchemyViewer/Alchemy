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

#include "../alvimmappings.h"

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<alvimmappings_data> alvimmappings_group;
    typedef alvimmappings_group::object    alvimmappings_object;
    alvimmappings_group                    alvimmappings_instance("alvimmappings");

    template<> template<>
    void alvimmappings_object::test<1>()
    {
        set_test_name("the notation: names in any case, modifiers, the characters it spells by name, and a name it does not know as its characters");
        const std::vector<ALVimInput> keys = maps.keysOf("a<esc><CR><c-w><C-S-x><S-Tab><Space><lt><bar><Bslash><F5><nop><foo>");
        ensure_equals("how many", keys.size(), size_t(16));
        ensure("a character", keys[0].isChar && keys[0].ch == 'a');
        ensure("<esc> in any case", !keys[1].isChar && keys[1].key == KEY_ESCAPE && keys[1].mask == MASK_NONE);
        ensure("<CR>", keys[2].key == KEY_RETURN);
        ensure("<c-w> is Control and W", keys[3].key == 'W' && keys[3].mask == ALVimInput::CONTROL);
        ensure("two modifiers", keys[4].key == 'X' && keys[4].mask == (ALVimInput::CONTROL | MASK_SHIFT));
        ensure("<S-Tab>", keys[5].key == KEY_TAB && keys[5].mask == MASK_SHIFT);
        ensure("<Space> <lt> <bar> <Bslash> are characters", keys[6].ch == ' ' && keys[7].ch == '<' && keys[8].ch == '|' && keys[9].ch == '\\');
        ensure("<F5>", keys[10].key == KEY_F5);
        ensure("<nop> is nothing, and <foo> is five characters", keys[11].isChar && keys[11].ch == '<' && keys[15].ch == '>');
        ensure_equals("shown back as :map lists them", spelt("a<esc><c-w><Space><lt>"), std::string("a<Esc><C-W><Space><lt>"));
        ensure("<S-a> is the capital", maps.keysOf("<S-a>").front().isChar && maps.keysOf("<S-a>").front().ch == 'A');
        ensure("<C-[> is Escape", maps.keysOf("<C-[>").front().key == KEY_ESCAPE);
    }

    template<> template<>
    void alvimmappings_object::test<2>()
    {
        set_test_name("<Leader> is the leader as it was when the mapping was made, set by :let in either quoting");
        ensure_equals("vim's own leader is a backslash", spelt("<Leader>w"), std::string("<Bslash>w"));
        ensure("let is taken", maps.let("mapleader = \",\"", error) && error.empty());
        ensure_equals("a comma", spelt("<leader>w"), std::string(",w"));
        ensure("g: and a key by name", maps.let("g:mapleader = \"\\<Space>\"", error) && error.empty());
        ensure_equals("a space", spelt("<Leader>w"), std::string("<Space>w"));
        ensure("single quotes as they are", maps.let("maplocalleader='<'", error) && error.empty());
        ensure_equals("a < in single quotes is itself", spelt("<LocalLeader>x"), std::string("<lt>x"));
        ensure("unclosed", maps.let("mapleader = \",", error) && !error.empty());
        error.clear();
        ensure("another variable is not the leaders'", !maps.let("foo = 1", error));
        ensure("a mapping made keeps the leader of its time", run("nnoremap", "<leader>w :w<CR>"));
        maps.let("mapleader = ','", error);
        const ALVimMappings::Match m = maps.match(ALVimMappings::NORMAL, maps.keysOf("<Space>w"), true);
        ensure("found by the leader it was made with", m.full != nullptr);
    }

    template<> template<>
    void alvimmappings_object::test<3>()
    {
        set_test_name("mappings made, each in its modes, listed, and taken away by the unmap of their mode and by mapclear");
        ensure("nmap", run("nmap", "j gj"));
        ensure("inoremap", run("ino", "jk <Esc>"));
        ensure("map is three modes", run("map", "Q gq"));
        ensure("map! is two", run("map!", "<C-l> <Right>"));
        ensure_equals("four", maps.mappings().size(), size_t(4));
        ensure("jk in insert mode", maps.match(ALVimMappings::INSERT, chars("jk"), true).full != nullptr);
        ensure("in normal mode, j alone is one", maps.match(ALVimMappings::NORMAL, chars("jk"), true).full &&
                                                    maps.match(ALVimMappings::NORMAL, chars("jk"), true).full->from.size() == 1);
        ensure("Q in operator-pending mode too", maps.match(ALVimMappings::OPERATOR, chars("Q"), true).full != nullptr);
        ensure("the : line has <C-l>", maps.starts(ALVimMappings::COMMAND_LINE, maps.keysOf("<C-l>").front()));
        ensure("listed", run("map", "") && listing.find("Q") != std::string::npos && listing.find("gj") != std::string::npos &&
                             listing.find("jk") == std::string::npos);
        ensure("the noremap marked", run("imap", "") && listing.find("* <Esc>") != std::string::npos);
        ensure("keys alone list what starts with them", run("nmap", "Q") && listing.find("Q") != std::string::npos &&
                                                             listing.find("gj") == std::string::npos);
        ensure("xunmap takes visual mode from Q", run("xunmap", "Q") && error.empty());
        ensure("but not normal mode", maps.match(ALVimMappings::NORMAL, chars("Q"), true).full != nullptr);
        ensure("nor is it in visual", maps.match(ALVimMappings::VISUAL, chars("Q"), true).full == nullptr);
        ensure("unmapping what is not there", run("iunmap", "zz") && !error.empty());
        ensure("imapclear", run("imapclear", "") && maps.match(ALVimMappings::INSERT, chars("jk"), true).full == nullptr);
        ensure("mapclear! leaves normal mode's", run("mapclear!", "") && maps.match(ALVimMappings::NORMAL, chars("j"), true).full != nullptr &&
                                                    maps.match(ALVimMappings::COMMAND_LINE, maps.keysOf("<C-l>"), true).full == nullptr);
        ensure("not one of the family", !run("mapx", "a b") && !run("nnoremap!", "a b") && !run("marks", ""));
        ensure("an empty list says so", run("cmap", "") && listing == "No mapping found");
    }

    template<> template<>
    void alvimmappings_object::test<4>()
    {
        set_test_name("what keys make: the longest mapping they make whole, and whether more could make a longer one");
        run("nmap", "g gg");
        run("nmap", "gx x");
        run("nmap", "<leader>ab A");
        ensure("g alone is whole, and gx may follow", maps.match(ALVimMappings::NORMAL, chars("g"), true).full != nullptr &&
                                                       maps.match(ALVimMappings::NORMAL, chars("g"), true).longer);
        ensure("nothing more is coming: not longer", !maps.match(ALVimMappings::NORMAL, chars("g"), false).longer);
        const ALVimMappings::Match gx = maps.match(ALVimMappings::NORMAL, chars("gxq"), true);
        ensure("gx the longest whole, the q after it none of it", gx.full && gx.full->from.size() == 2 && !gx.longer);
        ensure("\\a waits for b", maps.match(ALVimMappings::NORMAL, chars("\\a"), true).longer && !maps.match(ALVimMappings::NORMAL, chars("\\a"), true).full);
        ensure("\\c is nothing", !maps.match(ALVimMappings::NORMAL, chars("\\c"), true).longer);
        ensure("a mapping made again replaces the one there", run("nmap", "gx y") && maps.mappings().size() == 3);
        ensure("<nowait> kept", run("nnoremap", "<nowait> g G") && maps.match(ALVimMappings::NORMAL, chars("g"), true).full->nowait);
        ensure("<unique> refuses one there", run("nmap", "<unique> gx z") && !error.empty());
        ensure("<expr> is refused", run("nmap", "<expr> q 1") && !error.empty());
        ensure("a bar ends it, \\| is one", run("nmap", "zb a\\|b|c") && ALVimMappings::shown(maps.match(ALVimMappings::NORMAL, chars("zb"), true).full->to) ==
                                                                     std::string("a<Bar>b"));
    }

    template<> template<>
    void alvimmappings_object::test<5>()
    {
        set_test_name("the vimrc's mappings and leaders forgotten before it is read again, those typed kept");
        maps.let("mapleader = ','", error);
        run("nnoremap", "<leader>a A", true);
        run("nnoremap", "Y y$");
        maps.forgetVimrc();
        ensure_equals("one left", maps.mappings().size(), size_t(1));
        ensure("the typed one", maps.match(ALVimMappings::NORMAL, chars("Y"), true).full != nullptr);
        ensure_equals("the leader vim's again", maps.leader(), std::string("\\"));
    }
}
