/**
 * @file aluistatescope.h
 * @brief What a UI test changes of the process, put back when it ends
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

#ifndef AL_ALUISTATESCOPE_H
#define AL_ALUISTATESCOPE_H

#include "linden_common.h"

#include "alcolorsheet.h"
#include "alpopover.h"
#include "../llfloater.h"
#include "../llfocusmgr.h"
#include "../llmenugl.h"
#include "../lluicolortable.h"

#include "llcontrol.h"
#include "llkeyboard.h"
#include "llxmlnode.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Each of these is a global or a singleton the next test reads as this one
// left it. Held in a test or its fixture, they put it back however the test
// ends, a failed check included: a check throws, and only a destructor runs
// after it.
namespace ll_test
{
    // gFloaterView, which every floater adds itself to as it is built, set to
    // a test's own floater view and back to the one before. Left null or
    // pointing at a view that has gone, the next floater built anywhere in
    // the process would dereference it.
    class FloaterViewScope
    {
    public:
        explicit FloaterViewScope(LLFloaterView* view)
        :   mPrevious(gFloaterView)
        {
            gFloaterView = view;
        }
        ~FloaterViewScope() { gFloaterView = mPrevious; }

        FloaterViewScope(const FloaterViewScope&) = delete;
        FloaterViewScope& operator=(const FloaterViewScope&) = delete;

    private:
        LLFloaterView* mPrevious;
    };

    // Keyboard focus, mouse capture and the top control, as a test found
    // them. A test that leaves one on a view it made leaves the next test a
    // pointer to a view that dies with this one.
    class FocusScope
    {
    public:
        FocusScope()
        :   mKeyboard(gFocusMgr.getKeyboardFocus()),
            mCapture(gFocusMgr.getMouseCapture()),
            mTop(gFocusMgr.getTopCtrl())
        {
        }
        ~FocusScope()
        {
            if (gFocusMgr.getMouseCapture() != mCapture)
            {
                gFocusMgr.setMouseCapture(mCapture);
            }
            if (gFocusMgr.getKeyboardFocus() != mKeyboard)
            {
                gFocusMgr.setKeyboardFocus(mKeyboard);
            }
            if (gFocusMgr.getTopCtrl() != mTop)
            {
                gFocusMgr.setTopCtrl(mTop);
            }
        }

        FocusScope(const FocusScope&) = delete;
        FocusScope& operator=(const FocusScope&) = delete;

    private:
        LLFocusableElement* mKeyboard;
        LLMouseHandler*     mCapture;
        LLUICtrl*           mTop;
    };

    // What LLKeyboard names keys with, set for a test that reads an
    // accelerator's name and put back after. The viewer sets one at start;
    // without one, naming a key is an LL_ERRS.
    class KeyNamesScope
    {
    public:
        explicit KeyNamesScope(LLKeyStringTranslatorFunc* names)
        :   mPrevious(Keyboard::names())
        {
            LLKeyboard::setStringTranslatorFunc(names);
        }
        ~KeyNamesScope() { LLKeyboard::setStringTranslatorFunc(mPrevious); }

        KeyNamesScope(const KeyNamesScope&) = delete;
        KeyNamesScope& operator=(const KeyNamesScope&) = delete;

    private:
        // LLKeyboard keeps its translator to itself and its subclasses.
        struct Keyboard : public LLKeyboard
        {
            static LLKeyStringTranslatorFunc* names() { return mStringTranslator; }
        };

        LLKeyStringTranslatorFunc* mPrevious;
    };

    // The holder every menu shows itself in, set for a test that opens one.
    class MenuContainerScope
    {
    public:
        explicit MenuContainerScope(LLMenuHolderGL* holder)
        :   mPrevious(LLMenuGL::sMenuContainer)
        {
            LLMenuGL::sMenuContainer = holder;
        }
        ~MenuContainerScope() { LLMenuGL::sMenuContainer = mPrevious; }

        MenuContainerScope(const MenuContainerScope&) = delete;
        MenuContainerScope& operator=(const MenuContainerScope&) = delete;

    private:
        LLMenuHolderGL* mPrevious;
    };

    // Settings a test declares in a group that outlives it, as the HeadlessUI
    // "config" group does, put back to their defaults when it ends. The group
    // keeps the declarations, since a group has no way to forget one, but a
    // later declaration of the same name, which keeps the value it finds,
    // then finds the default.
    class SettingsScope
    {
    public:
        SettingsScope() = default;
        ~SettingsScope()
        {
            for (const LLPointer<LLControlVariable>& control : mKept)
            {
                control->resetToDefault(false);
            }
        }

        LLControlVariable* keep(LLControlVariable* control)
        {
            if (control)
            {
                mKept.emplace_back(control);
            }
            return control;
        }

        SettingsScope(const SettingsScope&) = delete;
        SettingsScope& operator=(const SettingsScope&) = delete;

    private:
        std::vector<LLPointer<LLControlVariable>> mKept;
    };

    // The colour table as a test found it. Its user layer always: a colour
    // a test sets there is reset, and one it changed is set back. With the
    // loaded layer too, a test that loads sheets of its own has what was
    // loaded before read again in their place, since the table answers only
    // to documents: rebuilt here from the sheet it had, a declaration at a
    // time.
    //
    // A name, once in the table, stays there -- widgets hold pointers into
    // it -- so a name a test loaded and the one before had not keeps a
    // place, magenta, as any name a reload drops does. A table there was
    // none of is left in being.
    class ColorTableScope
    {
    public:
        enum Layers
        {
            USER,
            USER_AND_LOADED
        };

        explicit ColorTableScope(Layers layers = USER)
        :   mLoadedToo(layers == USER_AND_LOADED)
        {
            if (!LLUIColorTable::instanceExists())
            {
                return;
            }
            const LLUIColorTable& table = LLUIColorTable::instance();
            for (const auto& [name, color] : table.getUserColors())
            {
                mUser.emplace(name, color.get());
            }
            if (mLoadedToo)
            {
                mLoadedSheet = table.getLoadedSheet();
                mUserSheet = table.getUserSheet();
                mLoaded = loadedColors(table);
            }
        }

        ~ColorTableScope()
        {
            if (!LLUIColorTable::instanceExists())
            {
                return;
            }
            LLUIColorTable& table = LLUIColorTable::instance();
            if (mLoadedToo && loadedColors(table) != mLoaded)
            {
                std::vector<LLUIColorTable::document_t> documents;
                for (size_t file = 0; file < mLoadedSheet.files().size(); ++file)
                {
                    documents.emplace_back(documentOf(mLoadedSheet, file), mLoadedSheet.files()[file]);
                }
                const bool user = !mUserSheet.files().empty();
                table.load(documents, user ? documentOf(mUserSheet, 0) : LLXMLNodePtr(),
                           user ? mUserSheet.files()[0] : std::string());
            }

            std::vector<std::string> added;
            for (const auto& [name, color] : table.getUserColors())
            {
                if (mUser.find(name) == mUser.end())
                {
                    added.push_back(name);
                }
            }
            for (const std::string& name : added)
            {
                table.resetToDefault(name);
            }
            for (const auto& [name, color] : mUser)
            {
                const auto now = table.getUserColors().find(name);
                if (now == table.getUserColors().end() || now->second.get() != color)
                {
                    table.setColor(name, color);
                }
            }
        }

        ColorTableScope(const ColorTableScope&) = delete;
        ColorTableScope& operator=(const ColorTableScope&) = delete;

    private:
        static std::map<std::string, LLColor4> loadedColors(const LLUIColorTable& table)
        {
            std::map<std::string, LLColor4> colors;
            for (const auto& [name, color] : table.getLoadedColors())
            {
                colors.emplace(name, color.get());
            }
            return colors;
        }

        // One file's declarations, in the order it made them, as a document
        // that reads back the same.
        static LLXMLNodePtr documentOf(const ALColorSheet& sheet, size_t file)
        {
            std::string xml = "<colors>\n";
            for (const ALColorSheet::Declaration& declaration : sheet.declarations())
            {
                if (declaration.file != file || declaration.kind == ALColorSheet::Declaration::Kind::Invalid)
                {
                    continue;
                }
                xml += "<color name=\"" + LLXMLNode::escapeXML(declaration.name) + "\" ";
                if (declaration.kind == ALColorSheet::Declaration::Kind::Reference)
                {
                    xml += "reference=\"" + LLXMLNode::escapeXML(declaration.reference) + "\"/>\n";
                }
                else
                {
                    char value[128];
                    std::snprintf(value, sizeof(value), "%.9g %.9g %.9g %.9g", declaration.value.mV[VRED],
                                  declaration.value.mV[VGREEN], declaration.value.mV[VBLUE], declaration.value.mV[VALPHA]);
                    xml += std::string("value=\"") + value + "\"/>\n";
                }
            }
            xml += "</colors>\n";
            LLXMLNodePtr root;
            LLXMLNode::parseBuffer(xml.data(), static_cast<U32>(xml.size()), root);
            return root;
        }

        bool                            mLoadedToo;
        std::map<std::string, LLColor4> mUser;
        std::map<std::string, LLColor4> mLoaded;
        ALColorSheet                    mLoadedSheet;
        ALColorSheet                    mUserSheet;
    };

    // Every popover still showing in the floater view, escaped, as a person
    // leaving one open would at last: what it was opened from hears it was
    // escaped and takes nothing from it, and keyboard focus goes back. A
    // popover left open holds focus, and a test that looks for "the"
    // popover showing would find this one first.
    inline void escapePopovers()
    {
        if (!gFloaterView)
        {
            return;
        }
        std::vector<LLHandle<ALPopover>> showing;
        for (LLView* child : *gFloaterView->getChildList())
        {
            ALPopover* popover = child->as<ALPopover>();
            if (popover && popover->getVisible() && !popover->isDead())
            {
                showing.push_back(popover->getDerivedHandle<ALPopover>());
            }
        }
        for (const LLHandle<ALPopover>& handle : showing)
        {
            if (ALPopover* popover = handle.get())
            {
                popover->escape();
            }
        }
    }
}

#endif // AL_ALUISTATESCOPE_H
