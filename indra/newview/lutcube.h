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
// Changes from the original: the domain offset is now applied and the values
// really are clamped; parsing is hardened against files that would previously
// have written out of bounds or thrown; and the parser can be driven from any
// stream so it can be tested without the filesystem. See lutcube.cpp.

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

class LutCube
{
public:
    /// RGBA at 16 bits per channel, size^3 entries, laid out so x varies
    /// fastest -- which is the order glTexSubImage3D wants. Empty if the file
    /// could not be understood; callers test this and fall back rather than
    /// uploading a half-built cube.
    ///
    /// Sixteen bits rather than eight because a .cube carries floats, and
    /// quantising them to 256 levels before asking the hardware to interpolate
    /// between them is exactly where LUT banding comes from. Normalised
    /// integer rather than half float: the values are bounded by their own
    /// domain, so every bit spent on an exponent would be wasted.
    std::vector<unsigned short> colorCube;
    int                         size = 0;

    LutCube(const std::string& file);
    LutCube() = default;

    /// Parse a .cube from any stream. The file constructor is a thin wrapper
    /// over this; it exists separately so the parser can be exercised from a
    /// string without touching the filesystem.
    ///
    /// One parse per object: nothing here resets the cursor, the size or the
    /// sticky failure flag, so a second call sees the first's state (and
    /// trips the duplicate-LUT_3D_SIZE check at best). Construct a fresh
    /// LutCube per file, which is what every caller and test does.
    ///
    /// A cube is only kept if it is complete: the declared @c LUT_3D_SIZE must
    /// be sane and exactly that many entries must have been read. Anything else
    /// leaves @c colorCube empty. A partial cube is worse than none -- the
    /// unwritten tail reads as white, which is not a subtle failure.
    void parse(std::istream& stream);

private:
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;

    float maxX = 1.0f;
    float maxY = 1.0f;
    float maxZ = 1.0f;

    int currentX = 0;
    int currentY = 0;
    int currentZ = 0;

    /// Data rows accepted so far, checked against size^3 at the end.
    int entryCount = 0;
    /// Whether LUT_3D_SIZE has been seen, so data rows before it are rejected
    /// rather than written into an unallocated cube.
    bool sawSize = false;
    /// Set by anything that makes the cube untrustworthy. Sticky.
    bool failed = false;

    void writeColor(int x, int y, int z, unsigned short r, unsigned short g, unsigned short b);

    void parseLine(std::string line);

    // splits a tripel of floats
    void splitTripel(std::string tripel, float& x, float& y, float& z);

    void clampTripel(float x, float y, float z, unsigned short& outX, unsigned short& outY, unsigned short& outZ);

    // returns the text without leading whitespace
    std::string skipWhiteSpace(std::string text);
};
