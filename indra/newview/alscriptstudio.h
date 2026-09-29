/**
 * @file alscriptstudio.h
 * @brief Script Studio's ways in, for the viewer's files that open it or ask it something.
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

#pragma once

#include <string>

class ALCodeEditor;
class LLFloater;
class LLFontGL;
class LLSD;
class LLUUID;
struct ALScriptRef;

// Script Studio's ways in, for the viewer's files that open it or ask it
// something -- the inventory, an object's contents, the build tool, login,
// the floater registry, the preferences -- without the window's own header
// (alfloaterscriptstudio.h), which is every pane and unit it is made of.
// Defined in the window's source, beside what they reach.
namespace ALScriptStudio
{
    // The window, as the floater registry builds it.
    LLFloater* build(const LLSD& key);

    // The studio, with this script open in it: the window that has it
    // open already, else the main one; nothing where a restriction keeps
    // the studio from opening.
    void open(const ALScriptRef& ref, const std::string& name = std::string(), bool take_focus = true);
    // The studio, with this object pinned in its explorer and chosen
    // there: what the build tool's Explore in IDE button means when no
    // external editor is listening.
    void explore(const LLUUID& root);
    // A script or notecard gone from its object, so that a tab holding
    // it goes too.
    void itemRemoved(const ALScriptRef& ref);
    // Whether a script is open with changes not saved, in any of the
    // studio's windows: what a recompile of the server's text leaves out.
    bool unsavedIn(const ALScriptRef& ref);
    // At login: what an earlier session left unsaved -- a crash, a lost
    // connection -- offered back, to open, to leave for later, or to
    // discard.
    void offerRecovery();

    // The scripter's own snippets for a language, opened in the studio as
    // the XML they are kept in: made with an example in it where there is
    // none yet. What is saved there is offered at once.
    void editSnippets(bool lua);
    // The vimrc opened in the studio to be edited: the notecard where one
    // is the vimrc, else the file, made with a few lines saying what it
    // takes where there is none yet.
    void openVimrc();
    // The options every editor shares -- font, keys, wrap, gutter, map --
    // put on every studio window's again, when a setting behind one
    // changes.
    void refreshAll();
    // The typing settings put on an editor: tabs, completion, pairs, the
    // caret, the hover card. The studio's editors and the preferences'
    // preview share them.
    void applyTypingOptions(ALCodeEditor& editor);
    // The font the settings name, or the monospace default, zoomed as the
    // text has been, within ALFloaterScriptStudio's MIN_TEXT_POINTS and
    // MAX_TEXT_POINTS.
    const LLFontGL* editorFont();
}
