/**
 * @file alcodecards.cpp
 * @brief A code editor's cards: the hover card over the text, and signature help for a call being typed.
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

#include "alcodecards.h"

#include "alplace.h"
#include "alsaid.h"
#include "llstring.h"

#include <algorithm>

// --- what a card says ---------------------------------------------------------

// static
ALCodeCards::Composition ALCodeCards::compose(const std::vector<Problem>& problems, const std::vector<ALCodeFix>& fixes, const std::string& says,
                                              const std::vector<Link>& links)
{
    Composition out;
    // The problems, a line or more each, then a blank line, then what the
    // word is, its first line the head.
    S32 line_count = 0;
    for (size_t p = 0; p < problems.size(); ++p)
    {
        const Problem& problem = problems[p];
        if (!out.text.empty())
        {
            out.text += "\n";
        }
        out.text += problem.message;
        const S32 made = 1 + static_cast<S32>(std::count(problem.message.begin(), problem.message.end(), '\n'));
        for (S32 i = 0; i < made; ++i)
        {
            out.problemLines.emplace_back(line_count++, p);
        }
    }
    // What would put them right, each a link on a line of its own under
    // them that makes it.
    for (const ALCodeFix& fix : fixes)
    {
        LLStringUtil::format_map_t args;
        args["[TITLE]"] = fix.title;
        out.text += "\n" + alSaid("CodeFixLink", "Fix: [TITLE]", args);
        out.fixLines.emplace_back(line_count++, fix.value);
    }
    if (!says.empty())
    {
        if (!out.text.empty())
        {
            out.text += "\n\n";
            line_count += 1;
        }
        out.headLine = line_count;
        out.text += says;
    }
    // The card's lines, as its document will have them.
    std::vector<std::string> lines;
    for (size_t at = 0;;)
    {
        const size_t end = out.text.find('\n', at);
        lines.push_back(out.text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos)
        {
            break;
        }
        at = end + 1;
    }
    const S32 count = static_cast<S32>(lines.size());
    for (S32 line = out.headLine + 1; line < count && out.headLine >= 0; ++line)
    {
        if (lines[line].find(deprecatedNote()) != std::string::npos)
        {
            out.deprecatedLines.push_back(line);
        }
    }
    // The caller's own links, each on the line that says it, below the
    // head.
    for (size_t l = 0; l < links.size(); ++l)
    {
        for (S32 line = llmax(0, out.headLine + 1); line < count; ++line)
        {
            if (lines[line] == links[l].line)
            {
                out.linkLines.emplace_back(line, l);
                break;
            }
        }
    }
    return out;
}

// static
const std::string& ALCodeCards::deprecatedNote()
{
    static const std::string note = alSaid("CodeDeprecated", "(deprecated)");
    return note;
}

// --- where it goes ------------------------------------------------------------

// static
S32 ALCodeCards::widthLimit(S32 text_width)
{
    return llmin(MAX_WIDTH, llmax(80, text_width - 2 * PAD));
}

// static
S32 ALCodeCards::width(S32 content_width, S32 limit)
{
    return llmin(limit, llmax(40, content_width + 2 * PAD + 2));
}

// static
S32 ALCodeCards::height(S32 content_height)
{
    return content_height + 2 * (PAD - 2) + 2;
}

// static
LLRect ALCodeCards::place(const LLRect& anchor, const LLRect& text, S32 width, S32 height)
{
    return ALPlace::under(anchor, width, height, text, 2);
}

// --- what the analyzer says of a word ---------------------------------------

void ALCodeCards::asking(const ALTextRange& word, U32 version)
{
    mAsked        = word;
    mAskedVersion = version;
    mAnswer.clear();
    mLinks.clear();
}

bool ALCodeCards::heard(const ALTextPos& at, U32 version, const std::string& text, std::vector<Link> links)
{
    // An answer of nothing is an answer: what the definitions say is shown
    // in its place.
    if (mAsked.empty() || at != mAsked.begin || version != mAskedVersion)
    {
        return false;
    }
    mAnswer = text;
    mLinks  = std::move(links);
    return true;
}

// --- signature help -----------------------------------------------------------

void ALCodeCards::showSignature(const ALTextPos& at, Signature signature)
{
    mSignature   = std::move(signature);
    mSignatureAt = at;
}

void ALCodeCards::setSignatureActive(S32 active)
{
    if (mSignature)
    {
        mSignature->active = active;
    }
}

bool ALCodeCards::stepOverload(S32 step)
{
    if (!mSignature || mSignature->overloads.size() < 2)
    {
        return false;
    }
    const S32 count          = static_cast<S32>(mSignature->overloads.size());
    mSignature->overload     = ((mSignature->overload + step) % count + count) % count;
    const auto& shown        = mSignature->overloads[static_cast<size_t>(mSignature->overload)];
    mSignature->label        = shown.label;
    mSignature->parameters   = shown.parameters;
    return true;
}

bool ALCodeCards::signatureFor(const ALTextPos& caret) const
{
    return mSignature && caret.line == mSignatureAt.line && !(caret < mSignatureAt);
}

// static
LLRect ALCodeCards::signatureBox(S32 wanted_width, S32 height, const LLRect& anchor, const LLRect& view)
{
    // No wider than the view. A box sized to a signature longer than the
    // window ran off the right edge and was cut there by the view's own
    // rect, silently -- and a call with a long list of parameters is the
    // one whose signature was worth reading.
    return ALPlace::over(anchor, llmin(wanted_width, llmax(4 * SIGNATURE_PAD, view.getWidth())), height, view);
}

// static
F32 ALCodeCards::labelShift(F32 label_width, F32 through, F32 room)
{
    if (label_width <= room)
    {
        return 0.f;
    }
    return llclamp(through - room, 0.f, label_width - room);
}
