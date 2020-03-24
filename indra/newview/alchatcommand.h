/**
 * @file alchatcommand.h
 * @brief ALChatCommand header for chat input commands
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Copyright (C) 2013 Drake Arconis
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
 */

#ifndef AL_ALCHATCOMMAND_H
#define AL_ALCHATCOMMAND_H

#include "linden_common.h"

namespace ALChatCommand
{
	bool parseCommand(std::string data);
};

#endif // AL_ALCHATCOMMAND_H
