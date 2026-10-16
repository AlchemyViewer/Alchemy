#pragma once
// Shared helpers, included wherever they are needed and read once.
#define COMMON_VERSION 2
string tag() { return __SHORTFILE__ + " " + __ASSETID__; }
