/**
 * @file alcodeeditor_cards.cpp
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

#include "alcodeeditor.h"

#include "alsaid.h"
#include "lllocalcliprect.h"
#include "llrender2dutils.h"
#include "lluictrlfactory.h"
#include "llurlaction.h"

#include <algorithm>

bool ALCodeEditor::hoverCardAt(S32 x, S32 y)
{
    const ALTextPos at = posAtLocal(x, y, false);
    // A problem under the mouse says what it is, and the word what it is
    // as well: what is wrong with a call is read against what it takes.
    ALTextRange                    about;
    const std::vector<CardProblem> problems = problemsUnder(at, about);
    std::string                    says;
    std::vector<CardLink>          links;
    const ALTextRange              word = identifierAt(at);
    // A string literal says its own size, anywhere in it -- the space
    // after its comma as much as the word before -- since what the
    // analyzer has to say about one is that it is a string, which the
    // quotes said already.
    {
        const ALTextRange literal = stringAt(at);
        const std::string size    = stringSize(literal);
        if (!size.empty())
        {
            says  = alSaid("CodeStringHead", "string") + "\n" + size;
            about = about.empty() ? literal : ALTextRange(std::min(about.begin, literal.begin), std::max(about.end, literal.end));
        }
    }
    if (says.empty() && mHover && !word.empty())
    {
        if (mHover(at, document().text(word), says))
        {
            about = about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end));
        }
    }
    if (says.empty() && !word.empty())
    {
        // What the analyzer said of this word, if it was asked and the
        // text has not moved on since; else asked now, for an answer
        // that shows when it comes, or the next time the mouse rests
        // here.
        const U32 version = document().version();
        if (mCards.askedAbout(word, version))
        {
            if (!mCards.answer().empty())
            {
                says  = mCards.answer();
                links = mCards.links();
                about = about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end));
            }
        }
        else if (mHoverRequest)
        {
            mCards.asking(word, version);
            mHoverRequest(word.begin, document().text(word));
        }
    }
    if (says.empty() && problems.empty())
    {
        return false;
    }
    showCard(about, says, problems, links);
    return true;
}

std::vector<ALCodeEditor::CardProblem> ALCodeEditor::problemsUnder(const ALTextPos& at, ALTextRange& about) const
{
    std::vector<CardProblem> problems;
    for (const Decoration& d : mDecorations)
    {
        const ALTextRange range = d.range.normalised();
        if (!d.message.empty() && range.begin <= at && at < range.end)
        {
            problems.push_back({ d.message, d.color });
            about = about.empty() ? range : ALTextRange(std::min(about.begin, range.begin), std::max(about.end, range.end));
        }
    }
    return problems;
}

// static
const std::string& ALCodeEditor::deprecatedNote()
{
    return ALCodeCards::deprecatedNote();
}

ALSyntaxKind ALCodeEditor::semanticKindAt(const ALTextPos& at) const
{
    auto found = std::upper_bound(mSemantics.begin(), mSemantics.end(), at, [](const ALTextPos& pos, const SemanticToken& t) { return pos < t.range.begin; });
    if (found == mSemantics.begin())
    {
        return ALSyntaxKind::Text;
    }
    --found;
    return found->range.begin <= at && at < found->range.end ? found->kind : ALSyntaxKind::Text;
}

void ALCodeEditor::styleAsCode(const ALTextView& view, S32 line, std::vector<ALTextView::Style>& styles, std::string_view name, ALSyntaxKind kind)
{
    const std::string& text = view.document().line(line);
    std::vector<ALSyntaxToken> tokens;
    if (std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter().grammar())
    {
        ALSyntaxState state = grammar->initialState();
        grammar->lexLine(text, state, tokens, highlighter().words());
    }
    if (tokens.empty())
    {
        ALTextView::Style whole;
        whole.range = ALTextRange(ALTextPos(line, 0), ALTextPos(line, static_cast<S32>(text.size())));
        whole.font  = getFont();
        styles.push_back(whole);
        return;
    }
    // The tokens cover the line without a gap, so each carries the face.
    for (const ALSyntaxToken& token : tokens)
    {
        ALTextView::Style one;
        one.range = ALTextRange(ALTextPos(line, token.begin), ALTextPos(line, token.end));
        one.font  = getFont();
        ALSyntaxKind shown = token.kind;
        if (shown == ALSyntaxKind::Text && kind != ALSyntaxKind::Text && !name.empty() && std::string_view(text).substr(token.begin, token.end - token.begin) == name)
        {
            shown = kind;
        }
        if (shown != ALSyntaxKind::Text)
        {
            one.color = colorForKind(shown);
        }
        styles.push_back(std::move(one));
    }
}

void ALCodeEditor::showCard(const ALTextRange& about, const std::string& says, const std::vector<CardProblem>& problems,
                            const std::vector<CardLink>& links)
{
    // The card is one of the studio's small floating things, and they
    // all have the one look: the ground a shade off the text's own, and
    // a frame in a quarter of the ink, which `draw` puts on over it.
    // Taken afresh each time, since the colour table may have moved.
    const LLColor4         ground    = paint(Paint::Widget);
    const S32              PAD       = ALCodeCards::PAD;
    if (!mCard)
    {
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name        = "hover_card";
        p.rect        = LLRect(0, 20, ALCodeCards::MAX_WIDTH, 0);
        p.read_only   = true;
        p.word_wrap   = true;
        p.tab_stop    = false;
        p.takes_focus = false;
        p.mouse_opaque = true;
        p.font        = LLFontGL::getFontSansSerif();
        p.bg_visible  = true;
        p.bg_color    = ground;
        p.bg_readonly_color = ground;
        p.text_readonly_color = textColor();
        p.h_pad       = PAD;
        p.v_pad       = PAD - 2;
        p.context_menu = std::string();
        mCard         = LLUICtrlFactory::create<ALTextView>(p);
        mCard->setVisible(false);
        mCard->onLinkClicked([this](const ALTextView::Substitution& link) {
            if (!link.url.empty())
            {
                LLUrlAction::clickAction(link.url, false);
                return;
            }
            // A fix: made by whoever gave it, and the card goes, being
            // about a problem the fix is to take away.
            if (link.value.isMap() && link.value.has("fix"))
            {
                const LLSD value = link.value["fix"];
                hideCard();
                if (mFixHandler)
                {
                    mFixHandler(value);
                }
                return;
            }
            // A way somewhere the caller gave: gone to, and the card with
            // it, since it is about where the caret is no longer.
            if (link.value.isDefined() && mCardLinkHandler)
            {
                const LLSD value = link.value;
                hideCard();
                mCardLinkHandler(value);
            }
        });
        addChild(mCard);
    }
    mCard->setBackgroundColor(ground);
    mCard->setTextColor(textColor());
    // What would put the problems right: the fixes of the line they are
    // on, best first.
    std::vector<Fix> fixes;
    if (!problems.empty() && mFixProvider && mFixHandler && !isReadOnly())
    {
        mFixProvider(about.begin.line, fixes);
        ALFixListModel::rank(fixes);
    }
    const ALCodeCards::Composition card = ALCodeCards::compose(problems, fixes, says, links);
    const std::string&             all  = card.text;
    if (cardShown() && about == mCardAbout && all == mCard->text())
    {
        // The mouse resting on again: the card is up already.
        return;
    }
    mCardAbout = about;
    // The words: each problem in its colour, the head as code, a note
    // about deprecation in the warning colour, every URL a link.
    mCard->setText(all);
    std::vector<ALTextView::Style> styles;
    for (const auto& [line, problem] : card.problemLines)
    {
        ALTextView::Style one;
        one.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        one.color = problems[problem].color;
        styles.push_back(one);
    }
    if (card.headLine >= 0)
    {
        // The name in the head coloured as the text colours it where the
        // analyzer said what it is: a global, a parameter, a function of
        // the script's own, which the grammar alone does not know.
        const ALTextRange word = mMouseX >= 0 ? identifierAt(posAtLocal(mMouseX, mMouseY, false)) : identifierAt(about.begin);
        styleAsCode(*mCard, card.headLine, styles, document().text(word), word.empty() ? ALSyntaxKind::Text : semanticKindAt(word.begin));
    }
    const S32 lines = mCard->document().lineCount();
    for (const S32 line : card.deprecatedLines)
    {
        ALTextView::Style note;
        note.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        note.color = markColor(Mark::Warning);
        styles.push_back(note);
    }
    mCard->setStyles(std::move(styles));
    for (const auto& [line, value] : card.fixLines)
    {
        ALTextView::Substitution fix;
        fix.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        fix.link  = true;
        fix.value = LLSD().with("fix", value);
        mCard->addSubstitution(std::move(fix));
    }
    // The caller's own links, each on the line that says it, below the
    // head; then every URL.
    for (const auto& [line, link] : card.linkLines)
    {
        ALTextView::Substitution way;
        way.range   = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        way.link    = true;
        way.tooltip = links[link].tooltip;
        way.value   = links[link].value;
        mCard->addSubstitution(std::move(way));
    }
    for (S32 line = 0; line < lines; ++line)
    {
        mCard->linkUrlsOn(line);
    }
    // Its size: as wide as its widest line up to the limit, and as tall
    // as the lines wrapped at that width come to.
    const LLRect text  = textRect();
    const S32    limit = ALCodeCards::widthLimit(text.getWidth());
    mCard->setShape(LLRect(0, 40, limit, 0));
    mCard->setWordWrap(false);
    // Laid out before it is measured: the layout guesses the width of a
    // line it has not done yet by the width of a space, which is far
    // narrower than the face the head line is in, and the card would
    // come out a few characters wide and wrap the one word in it. These
    // are a handful of lines, not a script.
    for (S32 line = 0; line < lines; ++line)
    {
        mCard->layout().line(line);
    }
    const S32 width = ALCodeCards::width(static_cast<S32>(mCard->layout().contentWidth()), limit);
    mCard->setWordWrap(true);
    mCard->setShape(LLRect(0, 40, width, 0));
    for (S32 line = 0; line < lines; ++line)
    {
        mCard->layout().line(line);
    }
    const S32 height = ALCodeCards::height(mCard->layout().totalHeight());
    // Where: under the row of what it is about, at its start; above it
    // where under would run off the bottom; within the text's width.
    S32       row;
    layout().xOf(about.begin.line, about.begin.column, &row);
    const S32 top   = screenTopOf(text, about.begin.line, row);
    const S32 row_h = layout().rowHeightOf(about.begin.line, row);
    F32       x0, x1;
    mCardAnchor = LLRect(text.mLeft, top, text.mRight, top - row_h);
    if (spanOnRow(about.begin.line, row, about, x0, x1) && !about.empty())
    {
        const F32 left     = static_cast<F32>(text.mLeft) - scrollX();
        mCardAnchor.mLeft  = static_cast<S32>(left + x0);
        mCardAnchor.mRight = static_cast<S32>(left + x1);
    }
    mCard->setShape(ALCodeCards::place(mCardAnchor, text, width, height));
    mCard->setVisible(true);
}

void ALCodeEditor::hideCard()
{
    if (mCard && mCard->getVisible())
    {
        mCard->setVisible(false);
    }
    mCardAnchor = LLRect();
}

bool ALCodeEditor::cardShown() const
{
    return mCard && mCard->getVisible();
}

void ALCodeEditor::supplyHover(const ALTextPos& at, const std::string& text, std::vector<CardLink> links)
{
    // Kept for the word, and shown now if the mouse is still on it.
    if (!mCards.heard(at, document().version(), text, std::move(links)) || mMouseX < 0 || !textRect().pointInRect(mMouseX, mMouseY))
    {
        return;
    }
    const ALTextPos   under = posAtLocal(mMouseX, mMouseY, false);
    const ALTextRange word  = identifierAt(under);
    if (word != mCards.asked())
    {
        return;
    }
    ALTextRange                    about;
    const std::vector<CardProblem> problems = problemsUnder(under, about);
    showCard(about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end)), text, problems, mCards.links());
}

// --- signature help -------------------------------------------------------------

void ALCodeEditor::showSignature(const ALTextPos& at, Signature signature)
{
    if (!typingText())
    {
        // An answer that came after a modal keymap stopped inserting.
        return;
    }
    mCards.showSignature(at, std::move(signature));
}

void ALCodeEditor::hideSignature()
{
    mCards.hideSignature();
}

bool ALCodeEditor::signatureShown() const
{
    return mCards.signatureFor(caret());
}

void ALCodeEditor::drawSignature(const LLRect& text)
{
    const Signature* shown = mCards.signature();
    if (!shown || shown->label.empty())
    {
        return;
    }
    const S32        SIGNATURE_PAD = ALCodeCards::SIGNATURE_PAD;
    const Signature& sig   = *shown;
    const LLFontGL*  font  = getFont();
    const F32        alpha = getDrawContext().mAlpha;
    const S32        row_h = layout().rowHeight();
    const S32        line_h = font->getLineHeight();
    const bool       docs  = !sig.documentation.empty();
    const std::string doc_line = docs ? sig.documentation.substr(0, sig.documentation.find('\n')) : std::string();
    const S32        wanted = llmax(font->getWidth(sig.label), docs ? font->getWidth(doc_line) : 0) + 2 * SIGNATURE_PAD;
    const S32        height = line_h * (docs ? 2 : 1) + 2 * SIGNATURE_PAD;

    // Above the caret's row, left with the call's column, kept inside the
    // view; under the row where above would run off the top.
    S32       row;
    const ALTextPos at   = mCards.signatureAt();
    const F32       x    = layout().xOf(at.line, at.column, &row);
    const S32       top  = screenTopOf(text, at.line, row);
    const LLRect    box  = ALCodeCards::signatureBox(wanted, height, static_cast<S32>(static_cast<F32>(text.mLeft) - scrollX() + x), top, row_h, getLocalRect());
    const S32       room = box.getWidth() - 2 * SIGNATURE_PAD;

    const LLColor4 ink    = textColor() % alpha;
    const LLColor4 active = mBracketMatchColor.get() % alpha;
    const LLColor4 faint  = lineNumberColor() % alpha;
    gl_rect_2d(box, paint(Paint::Widget) % alpha, true);
    gl_rect_2d(box, paint(Paint::WidgetBorder) % alpha, false);

    // The label in three pieces, the active parameter in its own colour.
    const F32 baseline = static_cast<F32>(box.mTop - SIGNATURE_PAD - llround(font->getAscenderHeight()));
    S32       begin = -1, end = -1;
    if (sig.active >= 0 && sig.active < static_cast<S32>(sig.parameters.size()))
    {
        begin = sig.parameters[sig.active].first;
        end   = sig.parameters[sig.active].second;
    }
    // Where the label is longer than the room, it is scrolled so that the
    // parameter being filled in is in the box. The head of a signature is
    // the part already typed, so it is the part to give up; what runs off
    // the left edge under the clip reads as more of it being there.
    const F32 label_w = static_cast<F32>(font->getWidth(sig.label));
    const F32 through = end > 0 ? static_cast<F32>(font->getWidth(sig.label.substr(0, static_cast<size_t>(end)))) : label_w;
    const F32 shift   = ALCodeCards::labelShift(label_w, through, static_cast<F32>(room));
    F32 pen = static_cast<F32>(box.mLeft + SIGNATURE_PAD) - shift;
    auto piece = [&](S32 from, S32 to, const LLColor4& color) {
        if (to <= from)
        {
            return;
        }
        const std::string part = sig.label.substr(from, to - from);
        font->renderUTF8(part, 0, pen, baseline, color, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        pen += static_cast<F32>(font->getWidth(part));
    };
    {
        LLLocalClipRect clip(LLRect(box.mLeft + SIGNATURE_PAD, box.mTop - 1, box.mRight - SIGNATURE_PAD, box.mBottom + 1));
        if (begin >= 0 && end > begin && end <= static_cast<S32>(sig.label.size()))
        {
            piece(0, begin, ink);
            piece(begin, end, active);
            piece(end, static_cast<S32>(sig.label.size()), ink);
        }
        else
        {
            piece(0, static_cast<S32>(sig.label.size()), ink);
        }
    }
    if (docs)
    {
        // The documentation's first line reads from its own start, so it
        // ends in an ellipsis rather than being scrolled.
        font->renderUTF8(doc_line, 0, static_cast<F32>(box.mLeft + SIGNATURE_PAD), baseline - static_cast<F32>(line_h), faint,
                         LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, room, nullptr, true);
    }
}
