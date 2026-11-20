/**
 * @file allslcommentcode.cpp
 * @brief LSL a comment switched off, converted to SLua where it stands and kept commented.
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

#include "allslcommentcode.h"

#include "llprofiler.h"

#include <boost/unordered/unordered_flat_set.hpp>
#include <tailslide/logger.hh>

#include <algorithm>
#include <string>

namespace
{
    using Comment = ALLSLComments::Comment;
    using Unit    = ALLSLComments::Unit;
    using Outcome = ALLSLCommentCode::Outcome;

    std::vector<std::string_view> linesOf(std::string_view text)
    {
        std::vector<std::string_view> out;
        for (size_t from = 0; from <= text.size();)
        {
            const size_t cut = std::min(text.find('\n', from), text.size());
            out.push_back(text.substr(from, cut - from));
            from = cut + 1;
        }
        return out;
    }

    bool blank(std::string_view text)
    {
        return text.find_first_not_of(" \t\r") == std::string_view::npos;
    }

    // A line as two conversions are compared: its blanks at either end gone,
    // and each run of them inside one space, so that a line lined up again
    // (Writer::lineUp) is the line it was.
    std::string squeezed(std::string_view line)
    {
        std::string out;
        bool        gap = false;
        for (char c : line)
        {
            if (c == ' ' || c == '\t' || c == '\r')
            {
                gap = !out.empty();
                continue;
            }
            if (gap)
            {
                out += ' ';
                gap = false;
            }
            out += c;
        }
        return out;
    }

    // What a note over a line of the SLua begins with.
    constexpr std::string_view NOTE = "-- LSL:";

    // The comment after the code on a line of SLua, past its strings: from
    // its --, or nothing.
    std::string_view trailingComment(std::string_view line)
    {
        char quote = 0;
        for (size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (quote)
            {
                i += c == '\\' ? 1 : 0;
                quote = c == quote ? 0 : quote;
            }
            else if (c == '"' || c == '\'' || c == '`')
            {
                quote = c;
            }
            else if (c == '-' && i + 1 < line.size() && line[i + 1] == '-')
            {
                return line.substr(i);
            }
        }
        return {};
    }

    // What of a line of SLua two conversions are compared by: a note, or its
    // code, less any comment, squeezed; nothing for a comment's line.
    std::string compared(std::string_view line)
    {
        std::string out = squeezed(line);
        if (out.rfind(NOTE, 0) == 0)
        {
            return out;
        }
        return squeezed(line.substr(0, line.size() - trailingComment(line).size()));
    }

    std::string_view leadingBlanks(std::string_view text)
    {
        return text.substr(0, std::min(text.find_first_not_of(" \t"), text.size()));
    }

    // Where a byte of the LSL is, by line and column from one.
    struct Pos
    {
        S32  line   = 1;
        S32  column = 1;
        bool operator<(const Pos& o) const { return line != o.line ? line < o.line : column < o.column; }
    };

    // What a trial makes of one unit: what its code is written as where it
    // stood, or why it is not.
    struct Verdict
    {
        Outcome     outcome = Outcome::Refused;
        std::string text;
    };

    class Trials
    {
    public:
        Trials(std::string_view lsl, const ALLSLToSLua::Options& options) : mLsl(lsl), mOptions(options) {}

        ALLSLToSLua::Result run(ALLSLCommentCode::Report* report);

    private:
        // The script converted with the units uncommented, and the stretches
        // `blanked` blanked, each unit's verdict; where it does not parse or
        // resolve, what the parser gave back.
        typedef std::vector<std::pair<size_t, size_t>> Stretches;
        std::vector<Verdict> trial(const std::vector<Unit>& units, ALLSLToSLua::Pass* refused = nullptr, const Stretches& blanked = {});
        // A unit tried alone; where its only error is a declaration of its
        // declaring again what the script declares, tried again in its stead.
        Verdict alone(const Unit& unit);
        // The units tried at once: those that hold what the parser refuses,
        // and those with no code of their own, taken out until the rest
        // parse and each converts, then all taken; where one changes another
        // line, each half tried so. What is not taken is tried alone, but
        // for those the parser refused inside, which alone it would too.
        void together(std::vector<Unit> group, std::vector<std::pair<Unit, std::string>>& taken, boost::unordered_flat_set<size_t>& refused,
                      ALLSLCommentCode::Report* report);
        // A run that is not code whole in pieces: split at its lines of words,
        // or where it has none, a line at a time. Nothing for another unit.
        std::vector<Unit> pieces(const Unit& unit) const;
        // The SLua a unit's code is written as where it stood, from the
        // trial's lines that are its own, commented as the unit was; nothing
        // where it cannot stand there.
        std::string written(const Unit& unit, std::vector<std::string> code) const;
        Pos         at(size_t byte) const;
        std::string sentinel(size_t k) const { return "--" + std::string(1, mSentinel) + std::to_string(k) + mSentinel; }

        std::string_view            mLsl;
        const ALLSLToSLua::Options& mOptions;
        std::vector<Comment>        mComments;
        std::vector<size_t>         mLineStarts;
        // The comments of every unit, each written as its sentinel in every
        // conversion that does not try it, so that what it is written as
        // there is the same whatever is tried.
        std::vector<size_t>         mUnitComments;
        // The bytes the script has none of: the toggle's level mark the writer
        // takes, a sentinel's brackets, and the mark of a comment inside what
        // a trial uncommented.
        char                        mToggle   = 0;
        char                        mSentinel = 0;
        char                        mInner    = 0;
        // The script with every unit's comments as sentinels, its lines.
        ALLSLToSLua::Result           mBase;
        std::vector<std::string_view> mBaseLines;
        S32                           mConversions = 0;
    };

    Pos Trials::at(size_t byte) const
    {
        const auto   line  = std::upper_bound(mLineStarts.begin(), mLineStarts.end(), byte);
        const size_t index = static_cast<size_t>(line - mLineStarts.begin());
        return Pos{ static_cast<S32>(index), static_cast<S32>(byte - mLineStarts[index - 1]) + 1 };
    }

    std::vector<Verdict> Trials::trial(const std::vector<Unit>& units, ALLSLToSLua::Pass* refused, const Stretches& blanked)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("comment code: a trial");
        std::vector<Verdict> out(units.size());
        std::string          source(mLsl);
        for (const auto& [begin, end] : blanked)
        {
            for (size_t i = begin; i < end && i < source.size(); ++i)
            {
                source[i] = source[i] == '\n' || source[i] == '\r' ? source[i] : ' ';
            }
        }
        ALLSLToSLua::Pass pass;
        pass.inner = mInner;
        // The unit each comment is in, by its index in `units`.
        std::vector<S32> owner(mComments.size(), -1);
        for (size_t i = 0; i < units.size(); ++i)
        {
            ALLSLComments::uncomment(source, mComments, units[i]);
            pass.tried.emplace_back(units[i].begin, units[i].end);
            for (size_t k = units[i].first; k <= units[i].last; ++k)
            {
                owner[k] = static_cast<S32>(i);
            }
        }
        for (size_t k : mUnitComments)
        {
            if (owner[k] < 0)
            {
                pass.comments.push_back({ mComments[k].begin, sentinel(k), false });
            }
        }
        ++mConversions;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(source, mOptions, pass);
        if (!r.converted)
        {
            if (refused)
            {
                *refused = std::move(pass);
            }
            return out;
        }
        const std::vector<std::string_view> lines = linesOf(r.text);
        // Each unit's spans: the first and last line of SLua they give, and
        // whether one runs on past its last line. Units are in the order
        // written, so each span's is found by where it begins.
        std::vector<Pos> begins(units.size());
        std::vector<Pos> ends(units.size());
        for (size_t i = 0; i < units.size(); ++i)
        {
            begins[i] = { units[i].line, units[i].column };
            ends[i]   = at(units[i].end);
        }
        std::vector<S32>  lo(units.size(), static_cast<S32>(lines.size()));
        std::vector<S32>  hi(units.size(), -1);
        std::vector<bool> spills(units.size(), false);
        for (const ALLSLToSLua::Span& span : r.spans)
        {
            const Pos  starts = { span.lslFirst + 1, span.lslColumn + 1 };
            const auto after  = std::upper_bound(begins.begin(), begins.end(), starts);
            if (after == begins.begin())
            {
                continue;
            }
            const size_t i = static_cast<size_t>(after - begins.begin()) - 1;
            if (!(starts < ends[i]))
            {
                continue;
            }
            spills[i] = spills[i] || span.lslLast + 1 > units[i].endLine;
            lo[i]     = std::min(lo[i], span.sluaFirst);
            hi[i]     = std::max(hi[i], span.sluaLast);
        }
        // Each unit's own comments, wherever the trial wrote them: the lines
        // that carry its mark, and the line each mark is on.
        std::vector<S32>                                first(units.size(), static_cast<S32>(lines.size()));
        std::vector<S32>                                last(units.size(), -1);
        std::vector<std::vector<std::pair<S32, size_t>>> marks(lines.size());
        for (size_t l = 0; l < lines.size(); ++l)
        {
            const std::string_view line = lines[l];
            for (size_t at = line.find(mInner); at != std::string_view::npos;)
            {
                const size_t close = line.find(mInner, at + 1);
                if (close == std::string_view::npos)
                {
                    break;
                }
                const size_t i = static_cast<size_t>(std::strtoul(std::string(line.substr(at + 1, close - at - 1)).c_str(), nullptr, 10));
                if (i < units.size())
                {
                    first[i] = std::min(first[i], static_cast<S32>(l));
                    last[i]  = std::max(last[i], static_cast<S32>(l));
                    marks[l].emplace_back(static_cast<S32>(i), at);
                }
                at = line.find(mInner, close + 1);
            }
        }
        std::vector<bool> mine(lines.size(), false);
        for (size_t i = 0; i < units.size(); ++i)
        {
            const Unit& u = units[i];
            Verdict&    v = out[i];
            if (spills[i])
            {
                v.outcome = Outcome::Spills;
                continue;
            }
            if (hi[i] < 0)
            {
                v.outcome = Outcome::NoStatement;
                continue;
            }
            // Its lines, and nothing of the script's between.
            const S32                from = std::min(lo[i], first[i]);
            const S32                to   = std::max(hi[i], last[i]);
            std::vector<std::string> code;
            bool                     own = true;
            for (S32 l = from; l <= to && own; ++l)
            {
                const size_t           at      = static_cast<size_t>(l);
                const std::string_view line    = lines[at];
                const bool             carries = std::any_of(marks[at].begin(), marks[at].end(), [i](const auto& m) { return m.first == static_cast<S32>(i); });
                own = (l >= lo[i] && l <= hi[i]) || blank(line) || carries;
                std::string kept(line);
                for (auto m = marks[at].rbegin(); m != marks[at].rend(); ++m)
                {
                    kept.erase(m->second, kept.find(mInner, m->second + 1) + 1 - m->second);
                }
                code.push_back(std::move(kept));
            }
            if (!own)
            {
                v.outcome = Outcome::Spills;
                continue;
            }
            for (S32 l = from; l <= to; ++l)
            {
                mine[static_cast<size_t>(l)] = true;
            }
            v.text    = written(u, std::move(code));
            v.outcome = v.text.empty() ? Outcome::Unplaced : Outcome::Converted;
        }
        // Every other line's code and notes as the script without them writes
        // them. Where its comments go is not compared: a comment over code
        // uncommented may be given to that code rather than to what holds it,
        // and the comments are written where the plain conversion has them.
        std::vector<std::string> rest;
        for (size_t l = 0; l < lines.size(); ++l)
        {
            std::string line = compared(lines[l]);
            if (!mine[l] && !line.empty())
            {
                rest.push_back(std::move(line));
            }
        }
        // Less what was blanked: each one's lines, and the notes over them.
        std::vector<bool> gone(mBaseLines.size(), false);
        for (const auto& [begin, end] : blanked)
        {
            const Pos  starts = at(begin);
            const auto span   = std::find_if(mBase.spans.begin(), mBase.spans.end(), [&starts](const ALLSLToSLua::Span& each) {
                return each.lslFirst + 1 == starts.line && each.lslColumn + 1 == starts.column;
            });
            if (span == mBase.spans.end())
            {
                for (Verdict& v : out)
                {
                    v.outcome = v.outcome == Outcome::Converted ? Outcome::Changes : v.outcome;
                }
                return out;
            }
            for (S32 l = span->sluaFirst; l <= span->sluaLast && l < static_cast<S32>(gone.size()); ++l)
            {
                gone[static_cast<size_t>(l)] = true;
            }
            for (S32 l = span->sluaFirst - 1; l >= 0 && compared(mBaseLines[static_cast<size_t>(l)]).rfind(NOTE, 0) == 0; --l)
            {
                gone[static_cast<size_t>(l)] = true;
            }
        }
        std::vector<std::string> base;
        for (size_t l = 0; l < mBaseLines.size(); ++l)
        {
            std::string line = gone[l] ? std::string() : compared(mBaseLines[l]);
            if (!line.empty())
            {
                base.push_back(std::move(line));
            }
        }
        if (rest != base)
        {
            for (Verdict& v : out)
            {
                v.outcome = v.outcome == Outcome::Converted ? Outcome::Changes : v.outcome;
            }
        }
        return out;
    }

    Verdict Trials::alone(const Unit& unit)
    {
        ALLSLToSLua::Pass refused;
        Verdict           v = trial({ unit }, &refused).front();
        if (v.outcome != Outcome::Refused || refused.twice.empty() || mConversions > ALLSLCommentCode::MAX_TRIALS)
        {
            return v;
        }
        // Each declaration twice, one of them the unit's and the other not:
        // that other blanked, so that the unit's declares in its stead.
        Stretches  live;
        const auto inside = [&unit](size_t begin, size_t end) { return begin >= unit.begin && end <= unit.end; };
        const auto apart  = [&unit](size_t begin, size_t end) { return end <= unit.begin || begin >= unit.end; };
        for (const ALLSLToSLua::Pass::Twice& twice : refused.twice)
        {
            if (inside(twice.begin, twice.end) && apart(twice.firstBegin, twice.firstEnd))
            {
                live.emplace_back(twice.firstBegin, twice.firstEnd);
            }
            else if (inside(twice.firstBegin, twice.firstEnd) && apart(twice.begin, twice.end))
            {
                live.emplace_back(twice.begin, twice.end);
            }
            else
            {
                return v;
            }
        }
        return trial({ unit }, nullptr, live).front();
    }

    std::string Trials::written(const Unit& unit, std::vector<std::string> code) const
    {
        // Its lines as deep as the shallowest of them, which goes at the
        // comment's own depth.
        size_t base = std::string::npos;
        for (std::string& line : code)
        {
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                base = std::min(base, line.find_first_not_of(' '));
            }
        }
        for (std::string& line : code)
        {
            line = line.empty() ? line : line.substr(base);
        }
        const auto joined = [](const std::vector<std::string>& lines) {
            std::string out;
            for (const std::string& line : lines)
            {
                out += (out.empty() && &line == &lines.front() ? "" : "\n") + line;
            }
            return out;
        };
        if (unit.shape == Unit::Shape::Trailing)
        {
            // After the code on its line, as the LSL had it: a line of code.
            const auto spoken = std::count_if(code.begin(), code.end(), [](const std::string& line) { return !line.empty(); });
            if (spoken != 1)
            {
                return {};
            }
            const auto line = std::find_if(code.begin(), code.end(), [](const std::string& each) { return !each.empty(); });
            return "--" + std::string(leadingBlanks(ALLSLComments::body(mLsl, mComments[unit.first]))) + *line;
        }
        if (unit.shape == Unit::Shape::Lines)
        {
            // Each a line comment, after the blanks the LSL had after its
            // marks: the fewest of them, where the run had code over lines.
            std::string_view spacing;
            bool             any = false;
            for (size_t k = unit.first; k <= unit.last; ++k)
            {
                const std::string_view text = ALLSLComments::body(mLsl, mComments[k]);
                if (!blank(text) && (!any || leadingBlanks(text).size() < spacing.size()))
                {
                    spacing = leadingBlanks(text);
                    any     = true;
                }
            }
            for (std::string& line : code)
            {
                line = line.empty() ? "--" : "--" + std::string(spacing) + line;
            }
            return joined(code);
        }
        // A block at a level nothing in it closes; the toggle's at the level
        // the writer marks, which it works out once it is all written.
        const std::string_view text   = ALLSLComments::body(mLsl, mComments[unit.first]);
        const size_t           cut    = std::min(text.find('\n'), text.size());
        const std::string_view head   = text.substr(0, cut);
        const std::string_view tail   = text.substr(text.rfind('\n') == std::string_view::npos ? 0 : text.rfind('\n') + 1);
        const bool             opens  = !blank(head);
        const bool             closes = !blank(tail);
        const bool             toggle = unit.shape == Unit::Shape::Toggle;
        std::string            bracket;
        if (toggle && mToggle)
        {
            bracket = std::string(1, mToggle);
        }
        else
        {
            const std::string all = joined(code);
            while (all.find("]" + bracket + "]") != std::string::npos)
            {
                bracket += "=";
            }
        }
        std::vector<std::string> out;
        size_t                   from = 0;
        if (opens && !code.empty())
        {
            out.push_back("--[" + bracket + "[" + std::string(leadingBlanks(head)) + code.front());
            from = 1;
        }
        else
        {
            out.push_back("--[" + bracket + "[");
        }
        out.insert(out.end(), code.begin() + static_cast<std::ptrdiff_t>(from), code.end());
        const std::string closer = (toggle ? "--]" : "]") + bracket + "]";
        if (closes && (out.size() > 1 || opens))
        {
            const std::string_view after = tail.substr(tail.find_last_not_of(" \t\r") + 1);
            std::string            gap(after.substr(0, after.find_first_of("\r")));
            if (gap.empty() && !toggle && !out.back().empty() && out.back().back() == ']')
            {
                gap = " ";
            }
            out.back() += gap + closer;
        }
        else
        {
            out.push_back(closer);
        }
        return joined(out);
    }

    void Trials::together(std::vector<Unit> group, std::vector<std::pair<Unit, std::string>>& taken, boost::unordered_flat_set<size_t>& refused,
                          ALLSLCommentCode::Report* report)
    {
        while (group.size() > 1 && mConversions <= ALLSLCommentCode::MAX_TRIALS)
        {
            ALLSLToSLua::Pass          refusal;
            const std::vector<Verdict> verdicts = trial(group, &refusal);
            const auto&                errors   = refusal.errors;
            if (!errors.empty())
            {
                // Each place the parser refused, in the unit it is in, or the
                // nearest before it: only what is tried next follows from it,
                // since whatever is taken is taken from a trial that parsed.
                // One refused inside, but for a name declared again, which
                // another declaration may be (CC10), alone would be too: its
                // code does not parse, or names what nothing declares.
                std::vector<bool> out(group.size(), false);
                std::vector<Pos>  begins;
                for (const Unit& u : group)
                {
                    begins.push_back({ u.line, u.column });
                }
                for (const ALLSLToSLua::Pass::Error& error : errors)
                {
                    const Pos    at    = { error.line + 1, error.column + 1 };
                    const auto   after = std::upper_bound(begins.begin(), begins.end(), at);
                    const size_t which = after == begins.begin() ? 0 : static_cast<size_t>(after - begins.begin()) - 1;
                    out[which]         = true;
                    const bool inside  = !(at < begins[which]) && at < this->at(group[which].end);
                    if (inside && error.code != Tailslide::E_DUPLICATE_DECLARATION)
                    {
                        refused.insert(group[which].first);
                    }
                }
                std::vector<Unit> rest;
                for (size_t i = 0; i < group.size(); ++i)
                {
                    if (!out[i])
                    {
                        rest.push_back(group[i]);
                    }
                }
                group = std::move(rest);
                continue;
            }
            // Where one changed another line, which one cannot be told: each
            // half tried apart. Where none did, those with no code of their
            // own are taken out, and the rest tried again.
            if (std::any_of(verdicts.begin(), verdicts.end(), [](const Verdict& v) { return v.outcome == Outcome::Changes; }))
            {
                const auto half = group.begin() + static_cast<std::ptrdiff_t>(group.size() / 2);
                together(std::vector<Unit>(group.begin(), half), taken, refused, report);
                together(std::vector<Unit>(half, group.end()), taken, refused, report);
                return;
            }
            if (!std::all_of(verdicts.begin(), verdicts.end(), [](const Verdict& v) { return v.outcome == Outcome::Converted; }))
            {
                std::vector<Unit> rest;
                for (size_t i = 0; i < group.size(); ++i)
                {
                    if (verdicts[i].outcome == Outcome::Converted)
                    {
                        rest.push_back(group[i]);
                    }
                }
                group = std::move(rest);
                continue;
            }
            // Every one converts: all taken.
            for (size_t i = 0; i < group.size(); ++i)
            {
                taken.emplace_back(group[i], verdicts[i].text);
                if (report)
                {
                    report->tried.push_back({ group[i], Outcome::Converted, verdicts[i].text });
                }
            }
            return;
        }
    }

    std::vector<Unit> Trials::pieces(const Unit& unit) const
    {
        if (unit.shape != Unit::Shape::Lines || unit.first == unit.last)
        {
            return {};
        }
        // Split at its lines of words, each stretch between them whole; or
        // where there are none, a line at a time.
        std::vector<Unit> out = ALLSLComments::runOf(unit, mLsl, mComments, false);
        if (out.size() == 1 && out.front().first == unit.first && out.front().last == unit.last)
        {
            out = ALLSLComments::runOf(unit, mLsl, mComments, true);
        }
        return out;
    }

    ALLSLToSLua::Result Trials::run(ALLSLCommentCode::Report* report)
    {
        ALLSLToSLua::Pass plain;
        mComments                     = ALLSLComments::scan(mLsl);
        const std::vector<Unit> candidates = ALLSLComments::units(mLsl, mComments);
        mToggle                       = ALLSLComments::freeByte(mLsl);
        const std::vector<char> marks = ALLSLComments::freeBytes(mLsl, 2, mToggle);
        if (candidates.empty() || marks.size() < 2)
        {
            return ALLSLToSLua::convert(mLsl, mOptions, plain);
        }
        mSentinel = marks[0];
        mInner    = marks[1];
        mLineStarts.push_back(0);
        for (size_t i = 0; i < mLsl.size(); ++i)
        {
            if (mLsl[i] == '\n')
            {
                mLineStarts.push_back(i + 1);
            }
        }
        ALLSLToSLua::Pass base;
        for (const Unit& u : candidates)
        {
            for (size_t k = u.first; k <= u.last; ++k)
            {
                mUnitComments.push_back(k);
                base.comments.push_back({ mComments[k].begin, sentinel(k), false });
            }
        }
        ++mConversions;
        mBase = ALLSLToSLua::convert(mLsl, mOptions, base);
        if (!mBase.converted)
        {
            return mBase;
        }
        mBaseLines = linesOf(mBase.text);

        // In rounds: the units tried together, what was not taken tried
        // alone, and the pieces of the runs that are not code whole the next
        // round's units.
        std::vector<std::pair<Unit, std::string>> taken;
        S32                                       left  = 0;
        std::vector<Unit>                         round = candidates;
        while (!round.empty())
        {
            boost::unordered_flat_set<size_t> refused;
            const size_t                      before = taken.size();
            together(round, taken, refused, report);
            boost::unordered_flat_set<size_t> together_taken;
            for (size_t t = before; t < taken.size(); ++t)
            {
                together_taken.insert(taken[t].first.first);
            }
            std::vector<Unit> next;
            for (const Unit& u : round)
            {
                if (together_taken.contains(u.first))
                {
                    continue;
                }
                Verdict v;
                v.outcome = Outcome::Capped;
                if (refused.contains(u.first))
                {
                    v.outcome = Outcome::Refused;
                }
                else if (mConversions <= ALLSLCommentCode::MAX_TRIALS)
                {
                    v = alone(u);
                }
                if (v.outcome == Outcome::Converted)
                {
                    if (report)
                    {
                        report->tried.push_back({ u, v.outcome, v.text });
                    }
                    taken.emplace_back(u, std::move(v.text));
                    continue;
                }
                const std::vector<Unit> split = v.outcome == Outcome::Capped ? std::vector<Unit>() : pieces(u);
                if (!split.empty())
                {
                    next.insert(next.end(), split.begin(), split.end());
                    continue;
                }
                ++left;
                if (report)
                {
                    report->tried.push_back({ u, v.outcome });
                }
            }
            round = std::move(next);
        }

        ALLSLToSLua::Pass last;
        for (const auto& [u, text] : taken)
        {
            last.comments.push_back({ mComments[u.first].begin, text, true });
            for (size_t k = u.first + 1; k <= u.last; ++k)
            {
                last.comments.push_back({ mComments[k].begin, std::string(), false });
            }
        }
        ++mConversions;
        ALLSLToSLua::Result r = ALLSLToSLua::convert(mLsl, mOptions, last);
        r.commentsConverted   = static_cast<S32>(taken.size());
        r.commentsLeft        = left;
        if (report)
        {
            report->conversions = mConversions;
            std::sort(report->tried.begin(), report->tried.end(),
                      [](const ALLSLCommentCode::Tried& a, const ALLSLCommentCode::Tried& b) { return a.unit.begin < b.unit.begin; });
        }
        return r;
    }
}

// static
ALLSLToSLua::Result ALLSLCommentCode::convert(std::string_view lsl, const ALLSLToSLua::Options& options, Report* report)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    Trials trials(lsl, options);
    return trials.run(report);
}

// static
const char* ALLSLCommentCode::nameOf(Outcome outcome)
{
    switch (outcome)
    {
        case Outcome::Converted: return "converted";
        case Outcome::Refused: return "does not parse or resolve";
        case Outcome::NoStatement: return "no statement of its own";
        case Outcome::Spills: return "reaches past itself";
        case Outcome::Changes: return "changes other lines";
        case Outcome::Unplaced: return "cannot stand where it was";
        case Outcome::Capped: return "past the trials";
    }
    return "";
}
