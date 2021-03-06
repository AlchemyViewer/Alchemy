/**
* @file llsearchableui.cpp
*
* $LicenseInfo:firstyear=2019&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2019, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"
#include "llsearchableui.h"

#include "llview.h"
#include "lltabcontainer.h"
#include "llmenugl.h"

ll::prefs::SearchableItem::~SearchableItem()
{}

void ll::prefs::SearchableItem::setNotHighlighted()
{
	mCtrl->setHighlighted( false );
}

bool ll::prefs::SearchableItem::highlightAndHide( LLWString const &aFilter )
{
	if( mCtrl->getHighlighted() )
		return true;

	LLView const *pView = dynamic_cast< LLView const* >( mCtrl );
	if( pView && !pView->getVisible() )
		return false;

	if( aFilter.empty() )
	{
		mCtrl->setHighlighted( false );
		return true;
	}

	if( mLabel.find( aFilter ) != LLWString::npos )
	{
		mCtrl->setHighlighted( true );
		return true;
	}

	return false;
}

ll::prefs::PanelData::~PanelData()
{}

bool ll::prefs::PanelData::highlightAndHide( LLWString const &aFilter )
{
	for (auto& itr : mChildren)
		itr->setNotHighlighted();

	bool bVisible(mChildren.size() == 0 && aFilter.empty());
	for (auto& itr : mChildren)
		bVisible |= itr->highlightAndHide(aFilter);

	for (auto& itr : mChildPanel)
		bVisible |= itr->highlightAndHide(aFilter);

	return bVisible;
}

bool ll::prefs::TabContainerData::highlightAndHide( LLWString const &aFilter )
{
	for (auto& itr : mChildren)
        itr->setNotHighlighted( );

	bool bVisible(false);
	for (auto& itr : mChildren)
        bVisible |= itr->highlightAndHide( aFilter );

	for (auto& itr : mChildPanel)
    {
		bool bPanelVisible = itr->highlightAndHide( aFilter );
		if(itr->mPanel )
			mTabContainer->setTabVisibility(itr->mPanel, bPanelVisible );
		bVisible |= bPanelVisible;
	}

	return bVisible;
}

ll::statusbar::SearchableItem::SearchableItem()
	: mMenu(0)
	, mCtrl(0)
	, mWasHiddenBySearch( false )
{ }

void ll::statusbar::SearchableItem::setNotHighlighted( )
{
	for (auto& itr : mChildren)
        itr->setNotHighlighted( );

	if( mCtrl )
	{
		mCtrl->setHighlighted( false );

		if( mWasHiddenBySearch )
			mMenu->setVisible( TRUE );
	}
}

bool ll::statusbar::SearchableItem::highlightAndHide(LLWString const &aFilter, bool hide)
{
	if ((mMenu && !mMenu->getVisible() && !mWasHiddenBySearch) || dynamic_cast<LLMenuItemTearOffGL*>(mMenu))
		return false;

	setNotHighlighted( );

	if( aFilter.empty() )
	{
		if( mCtrl )
			mCtrl->setHighlighted( false );
		return true;
	}

	bool bHighlighted(!hide);
	if( mLabel.find( aFilter ) != LLWString::npos )
	{
		if( mCtrl )
			mCtrl->setHighlighted( true );
		bHighlighted = true;
	}

	bool bVisible(false);
	for (auto& itr : mChildren)
        bVisible |= itr->highlightAndHide(aFilter, !bHighlighted);

	if (mCtrl && !bVisible && !bHighlighted)
	{
		mWasHiddenBySearch = true;
		mMenu->setVisible(FALSE);
	}
	return bVisible || bHighlighted;
}
