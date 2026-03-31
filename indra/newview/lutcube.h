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

#pragma once

class LutCube
{
public:
    std::vector<unsigned char> colorCube;
    int                        size = 0;

    LutCube(const std::string& file);
    LutCube() = default;

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

    void writeColor(int x, int y, int z, unsigned char r, unsigned char g, unsigned char b);

    void parseLine(std::string line);

    // splits a tripel of floats
    void splitTripel(std::string tripel, float& x, float& y, float& z);

    void clampTripel(float x, float y, float z, unsigned char& outX, unsigned char& outY, unsigned char& outZ);

    // returns the text without leading whitespace
    std::string skipWhiteSpace(std::string text);
};
