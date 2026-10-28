/**
 * @file alcvarref.cpp
 * @brief A setting found by name.
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

#include "alcvarref.h"

#include "alcvarconvert.h"
#include "alcvars.h"

const ALCVarDesc& ALCVarRef::desc() const
{
    return ALCVars::instance().desc(mIndex);
}

std::string_view ALCVarRef::name() const
{
    return mAlias ? mAlias->name : desc().name;
}

ALCVarType ALCVarRef::type() const
{
    return mAlias ? ALCVarConvert::aliasType(*mAlias, desc()) : desc().type;
}

ALCVarValue ALCVarRef::get() const
{
    ALCVarValue value = ALCVars::instance().get(mIndex);
    if (mAlias)
    {
        if (auto form = ALCVarConvert::toAlias(value, *mAlias, desc()))
        {
            return std::move(*form);
        }
    }
    return value;
}

std::optional<ALCVarValue> ALCVarRef::getAs(ALCVarType type) const
{
    return ALCVarConvert::toType(get(), type);
}

LLSD ALCVarRef::getLLSD() const
{
    return ALCVarValues::toLLSD(get());
}

std::optional<ALCVarValue> ALCVarRef::toSetting(const ALCVarValue& value) const
{
    const auto form = ALCVarConvert::toType(value, type());
    if (!form)
    {
        return std::nullopt;
    }
    return mAlias ? ALCVarConvert::fromAlias(*form, *mAlias, desc()) : form;
}

bool ALCVarRef::set(const ALCVarValue& value) const
{
    const auto setting = toSetting(value);
    return setting && ALCVars::instance().set(mIndex, *setting);
}

bool ALCVarRef::setLLSD(const LLSD& value) const
{
    return set(ALCVarValue(value));
}

bool ALCVarRef::setSession(const ALCVarValue& value) const
{
    const auto setting = toSetting(value);
    return setting && ALCVars::instance().setSession(mIndex, *setting);
}

void ALCVarRef::reset() const
{
    ALCVars::instance().reset(mIndex);
}

bool ALCVarRef::isUserSet() const
{
    return ALCVars::instance().isUserSet(mIndex);
}

ALCVarConnection ALCVarRef::onChange(ALCVarListener listener) const
{
    if (!mAlias)
    {
        return ALCVars::instance().listen(mIndex, std::move(listener));
    }
    return ALCVars::instance().listen(
        mIndex, [alias = mAlias, &desc = desc(), listener = std::move(listener)](const ALCVarValue& value,
                                                                               const ALCVarValue& old) {
            const auto form    = ALCVarConvert::toAlias(value, *alias, desc);
            const auto oldForm = ALCVarConvert::toAlias(old, *alias, desc);
            listener(form ? *form : value, oldForm ? *oldForm : old);
        });
}
