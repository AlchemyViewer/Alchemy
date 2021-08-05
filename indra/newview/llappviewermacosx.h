/**
 * @file llappviewermacosx.h
 * @brief The LLAppViewerMacOSX class declaration
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */ 

#ifndef LL_LLAPPVIEWERMACOSX_H
#define LL_LLAPPVIEWERMACOSX_H

#ifndef LL_LLAPPVIEWER_H
#include "llappviewer.h"
#endif

class LLAppViewerMacOSX final : public LLAppViewer
{
public:
	LLAppViewerMacOSX();
    ~LLAppViewerMacOSX() override;

	//
	// Main application logic
	//
    bool init();			// Override to do application initialization

    void setCrashUserMetadata(const LLUUID& user_id, const std::string& avatar_name) override;
    
protected:
    bool restoreErrorTrap() override;
    void initCrashReporting(bool reportFreeze) override;

	std::string generateSerialNumber();
    bool initParseCommandLine(LLCommandLineParser& clp) override;
};

#endif // LL_LLAPPVIEWERMACOSX_H
