/*
 *  LLCalc.cpp
 * Copyright 2008 Aimee Walton.
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2008, Linden Research, Inc.
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
 *
 */

#include "linden_common.h"

#include "llcalc.h"

#include "llmath.h"

#include "llcalcparser.h"

// Variable names for use in the build floater
const char* LLCalc::X_POS = "px";
const char* LLCalc::Y_POS = "py";
const char* LLCalc::Z_POS = "pz";
const char* LLCalc::X_SCALE = "sx";
const char* LLCalc::Y_SCALE = "sy";
const char* LLCalc::Z_SCALE = "sz";
const char* LLCalc::X_ROT = "rx";
const char* LLCalc::Y_ROT = "ry";
const char* LLCalc::Z_ROT = "rz";
const char* LLCalc::HOLLOW = "hlw";
const char* LLCalc::CUT_BEGIN = "cb";
const char* LLCalc::CUT_END = "ce";
const char* LLCalc::PATH_BEGIN = "pb";
const char* LLCalc::PATH_END = "pe";
const char* LLCalc::TWIST_BEGIN = "tb";
const char* LLCalc::TWIST_END = "te";
const char* LLCalc::X_SHEAR = "shx";
const char* LLCalc::Y_SHEAR = "shy";
const char* LLCalc::X_TAPER = "tpx";
const char* LLCalc::Y_TAPER = "tpy";
const char* LLCalc::RADIUS_OFFSET = "rof";
const char* LLCalc::REVOLUTIONS = "rev";
const char* LLCalc::SKEW = "skw";
const char* LLCalc::X_HOLE = "hlx";
const char* LLCalc::Y_HOLE = "hly";
const char* LLCalc::TEX_U_SCALE = "tsu";
const char* LLCalc::TEX_V_SCALE = "tsv";
const char* LLCalc::TEX_U_OFFSET = "tou";
const char* LLCalc::TEX_V_OFFSET = "tov";
const char* LLCalc::TEX_ROTATION = "trot";
const char* LLCalc::TEX_TRANSPARENCY = "trns";
const char* LLCalc::TEX_GLOW = "glow";


LLCalc* LLCalc::sInstance = nullptr;

LLCalc::LLCalc() : mLastErrorPos(0)
{
	// Init table of constants
	/*setVar("PI", F_PI);
	setVar("TWO_PI", F_TWO_PI);
	setVar("PI_BY_TWO", F_PI_BY_TWO);
	setVar("SQRT_TWO_PI", F_SQRT_TWO_PI);
	setVar("SQRT2", F_SQRT2);
	setVar("SQRT3", F_SQRT3);
	setVar("DEG_TO_RAD", DEG_TO_RAD);
	setVar("RAD_TO_DEG", RAD_TO_DEG);
	setVar("GRAVITY", GRAVITY);*/
}


//static
void LLCalc::cleanUp()
{
	delete sInstance;
	sInstance = nullptr;
}

//static
LLCalc* LLCalc::getInstance()
{
    if (!sInstance)	sInstance = new LLCalc();
	return sInstance;
}

void LLCalc::setVar(const std::string& name, const F32& value)
{
	mVariables[name] = value;
}

void LLCalc::clearVar(const std::string& name)
{
	mVariables.erase(name);
}

void LLCalc::clearAllVariables()
{
	mVariables.clear();
}

/*
void LLCalc::updateVariables(LLSD& vars)
{
	LLSD::map_iterator cIt = vars.beginMap();
	for(; cIt != vars.endMap(); cIt++)
	{
		setVar(cIt->first, (F32)(LLSD::Real)cIt->second);
	}
}
*/

bool LLCalc::evalString(const std::string& expression, F32& result)
{
	

	mLastErrorPos = 0;
	std::string::const_iterator itr = expression.begin();
	expression::grammar<F32,std::string::const_iterator> calc;
	calc.constant.add
				("pi", F_PI)
				("two_pi", F_TWO_PI)
				("pi_by_two", F_PI_BY_TWO)
				("sqrt_two_pi", F_SQRT_TWO_PI)
				("sqrt2", F_SQRT2)
				("sqrt3", F_SQRT3)
				("deg_to_rad", DEG_TO_RAD)
				("rad_to_deg", RAD_TO_DEG)
				("gravity", GRAVITY)
			;
	for(calc_map_t::const_iterator iter = mVariables.begin();
		iter != mVariables.end();
		++iter)
	{
		calc.constant.add(iter->first, iter->second);
		
	}
	
	if (!expression::parse<F32,std::string::const_iterator>(itr, expression.end(), calc, result) || itr !=  expression.end())
	{
		mLastErrorPos = itr - expression.begin();
		LL_INFOS() << "Unhandled syntax error at " << mLastErrorPos << " in expression: " << expression << LL_ENDL;
		return false;
	}
	LL_DEBUGS() << "Math expression: " << expression << " = " << result << LL_ENDL;
	return true;
}
