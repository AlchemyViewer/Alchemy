/**
 * @file alscriptinspectorpane.cpp
 * @brief Script Studio's inspector: what is known of the name at the caret, and what is wrong there.
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterscriptstudio.h"

#include "alscriptstudioplaces.h"
#include "altextview.h"

using ALScriptPlaces::declaredOf;

void ALFloaterScriptStudio::inspected(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at)
{
    // Still about the word the caret is on, and this script: by the
    // source's place it was asked about, which the result's is not where
    // the analyzer read the expansion.
    if (&doc != active() || at != doc.inspectAt)
    {
        return;
    }
    std::string      text;
    std::vector<S32> code_lines;
    Declared         declared;
    auto             lines_so_far = [&text]() { return static_cast<S32>(std::count(text.begin(), text.end(), '\n')); };
    if (result.hover.found)
    {
        text = result.hover.label;
        code_lines.push_back(0);
        LLStringUtil::format_map_t args;
        declared = declaredOf(doc, result, preprocessed(doc));
        if (declared.line >= 0)
        {
            args["[LINE]"] = std::to_string(declared.line + 1);
            args["[FILE]"] = declared.name;
            text += "\n" + getString(declared.path.empty() ? "InspectDeclared" : "InspectDeclaredIn", args);
        }
        if (!result.hover.expected.empty())
        {
            args["[TYPE]"] = result.hover.expected;
            text += "\n" + getString("HoverExpected", args);
        }
        if (!result.hover.typeDetail.empty())
        {
            text += "\n\n";
            const S32 first = lines_so_far();
            text += result.hover.typeDetail;
            for (S32 line = first; line <= lines_so_far(); ++line)
            {
                code_lines.push_back(line);
            }
        }
        std::string documentation = result.hover.documentation;
        std::string link          = result.hover.link;
        // What the keyword file says, where the analyzer has no words of
        // its own: LSL's declarations come without any.
        const std::string at_caret = doc.editor->document().text(doc.editor->identifierAtCaret());
        const Vocab*      word     = documentation.empty() ? ALScriptStudioWords::word(doc.language.lua, at_caret) : nullptr;
        if (word)
        {
            documentation = word->tooltip;
            if (link.empty())
            {
                link = ALScriptStudioWords::helpUrl(doc.language.lua, word->text);
            }
        }
        if (!documentation.empty())
        {
            text += "\n\n" + documentation;
        }
        if (!link.empty())
        {
            text += "\n" + link;
        }
    }
    // What is wrong where the caret is, said under the name.
    const std::string problems = problemsAt(doc, doc.inspectAt);
    if (!problems.empty())
    {
        text += (text.empty() ? "" : "\n\n") + problems;
    }
    showSymbol(text, declared, code_lines);
}

void ALFloaterScriptStudio::showSymbol(const std::string& text, const Declared& declared, const std::vector<S32>& code_lines)
{
    mSymbol->setText(text);
    std::vector<ALTextView::Style> styles;
    if (Doc* doc = active(); doc && !code_lines.empty())
    {
        // The declaration read as code: its words in the colours the
        // script's own text gives them.
        const ALTextRange word = doc->editor->identifierAtCaret();
        const std::string name = doc->editor->document().text(word);
        const ALSyntaxKind kind = word.empty() ? ALSyntaxKind::Text : doc->editor->semanticKindAt(word.begin);
        for (const S32 line : code_lines)
        {
            if (line < mSymbol->document().lineCount())
            {
                doc->editor->styleAsCode(*mSymbol, line, styles, name, kind);
            }
        }
    }
    mSymbol->setStyles(std::move(styles));
    const S32 lines = mSymbol->document().lineCount();
    for (S32 line = 0; line < lines; ++line)
    {
        mSymbol->linkUrlsOn(line);
    }
    // The declaration's line comes right after the name.
    if (declared.line >= 0 && lines > 1 && mSymbol->document().lineLength(1) > 0)
    {
        ALTextView::Substitution to_line;
        to_line.range           = ALTextRange(ALTextPos(1, 0), mSymbol->document().lineEnd(1));
        to_line.link            = true;
        to_line.tooltip         = getString("InspectDeclaredTip");
        to_line.value           = declared.value();
        mSymbol->addSubstitution(std::move(to_line));
    }
}

std::string ALFloaterScriptStudio::problemsAt(const Doc& doc, const ALTextPos& at) const
{
    // From the checkers and the compiler alike: whatever is squiggled
    // under the position, with what it says.
    std::string problems;
    for (const ALCodeEditor::Decoration& decoration : doc.editor->decorations())
    {
        if (decoration.style == ALCodeEditor::Decoration::Style::Squiggle && !decoration.message.empty() && decoration.range.contains(at))
        {
            LLStringUtil::format_map_t args;
            args["[MESSAGE]"] = decoration.message;
            problems += (problems.empty() ? "" : "\n") + getString("InspectProblem", args);
        }
    }
    return problems;
}
