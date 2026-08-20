/*
Copyright (c) 2019 - 2020 Georg Lehmann

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
*/

// ALTERED SOURCE VERSION -- modified by the Alchemy Viewer Project, 2026.
//
// A .cube is a file a user downloads from the internet and points us at, so it
// is untrusted input, and the original parser treated it as though it were not.
// The changes here, in the order they bite:
//
//  - `writeColor` indexed `colorCube` with no bounds check. A file with no
//    LUT_3D_SIZE line leaves the vector empty and `size` zero, and the first
//    data row then writes through a null-ish pointer. Now bounds-checked, and
//    data rows before a size are rejected outright.
//  - `std::stoi`/`std::stof` throw on malformed text and nothing caught them,
//    so a corrupt file propagated an exception out of setupGradingLUT. Parsing
//    now fails to an empty cube instead.
//  - A cube was kept however few entries it had. The allocation is prefilled
//    with the maximum value, so a truncated file produced a LUT that turned
//    everything above the truncation point white. Completeness is now required.
//  - Data rows were detected with `find_first_of("0123456789") == 0`, which
//    rejects the leading whitespace, sign or decimal point that legal .cube
//    files use. A rejected row does not just lose itself -- every later row
//    shifts into its slot, so one indented line skews the whole cube.
//  - `clampTripel` computed `255 * (x / (max - min))`: it never subtracted the
//    domain minimum, so a non-zero DOMAIN_MIN was applied wrongly, and despite
//    the name it did not clamp, so an out-of-domain value wrapped on the cast
//    to unsigned char -- 1.1 became 280 became 24, turning a highlight black.
//  - Quantisation truncated rather than rounded, biasing every entry down by up
//    to one step.
//
// The output is also 16 bits per channel rather than 8. A .cube carries floats,
// and flattening them to 256 levels before the hardware interpolates between
// them is where LUT banding comes from.

#include "linden_common.h"

#include "lutcube.h"

#include "llfile.h"

#include <cmath>
#include <istream>
#include <sstream>

namespace
{
    /// Widest cube we will allocate. Entries are RGBA at 16 bits per channel,
    /// so a side of 128 is already 16 MiB, and authoring tools emit 17, 33 or
    /// 65 with the odd 96 or 128; the point is to have an upper bound at all,
    /// so a garbage size cannot ask for a huge allocation.
    constexpr int MAX_LUT_SIZE = 128;
    constexpr int MIN_LUT_SIZE = 2;

    /// True if @a text begins something that could be a number, once leading
    /// whitespace is gone. Sign and a bare decimal point both count: .cube
    /// values are floats and negatives are legal on both sides of the domain.
    bool startsNumber(const std::string& text)
    {
        if (text.empty())
        {
            return false;
        }
        const char c = text[0];
        return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
    }
}

LutCube::LutCube(const std::string& file)
{
    llifstream cubeStream(file);
    if (!cubeStream.good())
    {
        LL_WARNS() << "lut cube file does not exist" << LL_ENDL;
        return;
    }

    parse(cubeStream);
}

void LutCube::parse(std::istream& stream)
{
    std::string line;

    // stoi/stof throw, and every one of them is reading text we did not write.
    // One catch around the whole parse is enough: `failed` is sticky and the
    // completeness check below rejects a cube that stopped early anyway.
    try
    {
        while (std::getline(stream, line))
        {
            parseLine(line);
            if (failed)
            {
                break;
            }
        }
    }
    catch (const std::exception& e)
    {
        LL_WARNS() << "malformed lut cube: " << e.what() << LL_ENDL;
        failed = true;
    }

    // A cube is all or nothing. The allocation is prefilled with the maximum
    // value, so keeping a short one would silently blow out everything past
    // the last entry that was read.
    const bool complete = sawSize && (entryCount == size * size * size);
    if (failed || !complete)
    {
        if (!failed)
        {
            LL_WARNS() << "lut cube is incomplete: expected " << (size * size * size) << " entries, read "
                       << entryCount << LL_ENDL;
        }
        colorCube.clear();
        size = 0;
    }
}

void LutCube::parseLine(std::string line)
{
    if (line.length() == 0)
    {
        return;
    }
    if (line[0] == '#')
    {
        return;
    }
    if (line.find("LUT_3D_SIZE") != std::string::npos)
    {
        if (sawSize)
        {
            LL_WARNS() << "lut cube declares LUT_3D_SIZE more than once" << LL_ENDL;
            failed = true;
            return;
        }

        line = line.substr(line.find("LUT_3D_SIZE") + 11);
        line = skipWhiteSpace(line);
        size = std::stoi(line);

        if (size < MIN_LUT_SIZE || size > MAX_LUT_SIZE)
        {
            LL_WARNS() << "lut cube declares an unusable LUT_3D_SIZE of " << size << LL_ENDL;
            failed = true;
            size   = 0;
            return;
        }

        sawSize    = true;
        colorCube  = std::vector<unsigned short>(static_cast<size_t>(size) * size * size * 4, 65535);
        return;
    }
    if (line.find("DOMAIN_MIN") != std::string::npos)
    {
        line = line.substr(line.find("DOMAIN_MIN") + 10);
        splitTripel(line, minX, minY, minZ);
        return;
    }
    if (line.find("DOMAIN_MAX") != std::string::npos)
    {
        line = line.substr(line.find("DOMAIN_MAX") + 10);
        splitTripel(line, maxX, maxY, maxZ);
        return;
    }
    if (startsNumber(skipWhiteSpace(line)))
    {
        // Without a size there is nowhere to put this, and the original wrote
        // it anyway.
        if (!sawSize)
        {
            LL_WARNS() << "lut cube has data before LUT_3D_SIZE" << LL_ENDL;
            failed = true;
            return;
        }

        float          x = 0.f, y = 0.f, z = 0.f;
        unsigned short outX, outY, outZ;
        splitTripel(line, x, y, z);
        if (failed)
        {
            // splitTripel rejected the triple without writing the outputs.
            // The cube is already condemned; carrying on would push the
            // initialisers above -- or, unguarded, indeterminate values --
            // through the quantiser for nothing.
            return;
        }
        clampTripel(x, y, z, outX, outY, outZ);
        writeColor(currentX, currentY, currentZ, outX, outY, outZ);
        ++entryCount;
        if (currentX != size - 1)
        {
            currentX++;
        }
        else if (currentY != size - 1)
        {
            currentY++;
            currentX = 0;
        }
        else if (currentZ != size - 1)
        {
            currentZ++;
            currentX = 0;
            currentY = 0;
        }
        return;
    }
}

std::string LutCube::skipWhiteSpace(std::string text)
{
    while (text.size() > 0 && (text[0] == ' ' || text[0] == '\t'))
    {
        text = text.substr(1);
    }
    return text;
}

void LutCube::splitTripel(std::string tripel, float& x, float& y, float& z)
{
    // Read the three as a stream rather than by hand: the original split on the
    // first " \n" it found, which loses a value to a tab, to runs of spaces, or
    // to the trailing \r a CRLF file keeps when it is read on a platform that
    // does not strip it.
    std::istringstream values(tripel);
    float              a = 0.f, b = 0.f, c = 0.f;
    if (!(values >> a >> b >> c))
    {
        LL_WARNS() << "lut cube has a malformed triple: " << tripel << LL_ENDL;
        failed = true;
        return;
    }

    // In practice the extraction above already refuses "nan" and "inf" -- the
    // measured behaviour on MSVC, and what the num_get grammar implies
    // elsewhere. But that is a property of the standard library's parser, not
    // of this code, and a NaN that did get through would sail on: llclamp
    // passes it (both comparisons are false) and the cast to unsigned short is
    // undefined behaviour. Owning the rejection costs three compares.
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
    {
        LL_WARNS() << "lut cube has a non-finite value: " << tripel << LL_ENDL;
        failed = true;
        return;
    }

    x = a;
    y = b;
    z = c;
}

void LutCube::clampTripel(float x, float y, float z, unsigned short& outX, unsigned short& outY, unsigned short& outZ)
{
    // Map the declared domain onto 0..1, then onto the full 16-bit range. Both
    // halves of the first step matter: without the subtraction a non-zero
    // DOMAIN_MIN shifts every entry, and without the clamp an out-of-domain
    // value wraps on the cast and a highlight comes out black.
    auto quantise = [](float value, float low, float high) -> unsigned short
    {
        const float span = high - low;
        const float unit = (span > 0.f) ? ((value - low) / span) : 0.f;
        return static_cast<unsigned short>(llclamp(unit, 0.f, 1.f) * 65535.f + 0.5f);
    };

    outX = quantise(x, minX, maxX);
    outY = quantise(y, minY, maxY);
    outZ = quantise(z, minZ, maxZ);
}

void LutCube::writeColor(int x, int y, int z, unsigned short r, unsigned short g, unsigned short b)
{
    static const int colorSize = 4; // 4 components per point in the cube, rgba

    const size_t locationR = ((static_cast<size_t>(z) * size + y) * size + x) * colorSize;

    if (locationR + 2 >= colorCube.size())
    {
        LL_WARNS() << "lut cube entry out of range at " << x << ", " << y << ", " << z << LL_ENDL;
        failed = true;
        return;
    }

    colorCube[locationR + 0] = r;
    colorCube[locationR + 1] = g;
    colorCube[locationR + 2] = b;
}
