/**
 * @file allinebreaks.h
 * @brief A text's lines whatever its line endings, and lines joined again.
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

#ifndef AL_ALLINEBREAKS_H
#define AL_ALLINEBREAKS_H

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// A text's lines whatever its line endings -- CRLF and a lone CR read as
// LF -- and lines joined by LF: what a document (ALTextDocument) and a
// comparison of texts (ALTextDiff) both read and write lines by, so that a
// line of one is a line of the other. Header only: a comparison's parts
// are built and tested without the document.
namespace ALLineBreaks
{
    // The breaks counted first, so that the list is made once, and each
    // line made at once, as long as it is: a script loaded is tens of
    // thousands of lines.
    inline void split(std::string_view text, std::vector<std::string>& out)
    {
        out.clear();
        out.reserve(static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1);
        // The next CR, found again only once it is behind: a text with
        // none, or one far on, is not searched for it at every line.
        constexpr size_t NONE    = std::string_view::npos;
        size_t           next_cr = text.find('\r');
        size_t           start   = 0;
        while (true)
        {
            if (next_cr != NONE && next_cr < start)
            {
                next_cr = text.find('\r', start);
            }
            const size_t at = std::min(text.find('\n', start), next_cr);
            if (at == NONE)
            {
                out.emplace_back(text.substr(start));
                return;
            }
            out.emplace_back(text.substr(start, at - start));
            start = at + 1;
            if (text[at] == '\r' && start < text.size() && text[start] == '\n')
            {
                ++start;
            }
        }
    }

    inline std::vector<std::string> split(std::string_view text)
    {
        std::vector<std::string> out;
        split(text, out);
        return out;
    }

    inline std::string join(const std::vector<std::string>& lines)
    {
        size_t size = lines.size();
        for (const std::string& line : lines)
        {
            size += line.size();
        }
        std::string out;
        out.reserve(size);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i)
            {
                out.push_back('\n');
            }
            out += lines[i];
        }
        return out;
    }

    // Text with its line endings as LF; the same lines.
    inline std::string withLineFeeds(std::string_view text)
    {
        if (text.find('\r') == std::string_view::npos)
        {
            return std::string(text);
        }
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] != '\r')
            {
                out.push_back(text[i]);
                continue;
            }
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n')
            {
                ++i;
            }
        }
        return out;
    }
}

#endif // AL_ALLINEBREAKS_H
