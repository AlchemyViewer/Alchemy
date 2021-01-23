/**
* @file alcinematicmode.h
* @brief Cinematic UI Mode for Metaverse clients
*
* $LicenseInfo:firstyear=2017&license=viewerlgpl$
* Copyright (C) 2017-2020 XenHat <me@xenh.at>
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
* $/LicenseInfo$
**/

#ifndef AL_CINEMATICMODE_H
#define AL_CINEMATICMODE_H
#pragma once

// Hides various UI Elements to provide a more cinematic experience
class ALCinematicMode
{
	static bool _enabled;
public:
	static void toggle();
	static bool isEnabled() { return _enabled; };
};
#endif